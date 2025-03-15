#include "pch.h"
#include <iostream>
#include <sstream>
#include <string>
#include <fstream>
#include <map>


// Для корректной работы freeaddrinfo в MinGW
#define _WIN32_WINNT 0x501
#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

using std::cerr;
using std::string;

// Функция для чтения файла
string read_file(const string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open file: " + path);
    }
    return string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// Функция для определения MIME-типа по расширению файла
string get_mime_type(const string& path) {
    static const std::map<string, string> mime_types = {
        {".html", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".png", "image/png"},
        {".gif", "image/gif"},
        {".txt", "text/plain"},
    };

    size_t dot_pos = path.find_last_of('.');
    if (dot_pos == string::npos) {
        return "application/octet-stream"; // Если расширение не найдено
    }

    string extension = path.substr(dot_pos);
    auto it = mime_types.find(extension);
    if (it != mime_types.end()) {
        return it->second;
    }
    return "application/octet-stream"; // По умолчанию
}

// Функция для отправки файла клиенту
void send_file(int client_socket, const string& file_path) {
    try {
        string file_content = read_file(file_path);
        string mime_type = get_mime_type(file_path);

        string response = "HTTP/1.1 200 OK\r\n"
            "Content-Type: " + mime_type + "\r\n"
            "Content-Length: " + std::to_string(file_content.length()) + "\r\n\r\n" +
            file_content;

        send(client_socket, response.c_str(), response.length(), 0);
    }
    catch (const std::exception& e) {
        string error_response = "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: " + std::to_string(strlen(e.what())) + "\r\n\r\n" +
            e.what();
        send(client_socket, error_response.c_str(), error_response.length(), 0);
    }
}

int main() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        cerr << "WSAStartup failed: " << result << "\n";
        return result;
    }

    struct addrinfo* addr = NULL;
    struct addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    result = getaddrinfo("127.0.0.1", "8002", &hints, &addr);
    if (result != 0) {
        cerr << "getaddrinfo failed: " << result << "\n";
        WSACleanup();
        return 1;
    }

    int listen_socket = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (listen_socket == INVALID_SOCKET) {
        cerr << "Error at socket: " << WSAGetLastError() << "\n";
        freeaddrinfo(addr);
        WSACleanup();
        return 1;
    }

    result = bind(listen_socket, addr->ai_addr, (int)addr->ai_addrlen);
    if (result == SOCKET_ERROR) {
        cerr << "bind failed with error: " << WSAGetLastError() << "\n";
        freeaddrinfo(addr);
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "listen failed with error: " << WSAGetLastError() << "\n";
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    const int max_client_buffer_size = 1024;
    char buf[max_client_buffer_size];
    int client_socket = INVALID_SOCKET;

    for (;;) {
        client_socket = accept(listen_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) {
            cerr << "accept failed: " << WSAGetLastError() << "\n";
            closesocket(listen_socket);
            WSACleanup();
            return 1;
        }

        result = recv(client_socket, buf, max_client_buffer_size, 0);
        if (result == SOCKET_ERROR) {
            cerr << "recv failed: " << result << "\n";
            closesocket(client_socket);
        }
        else if (result == 0) {
            cerr << "connection closed...\n";
        }
        else if (result > 0) {
            buf[result] = '\0';
            string request(buf);

            // Определяем запрашиваемый файл
            size_t start = request.find(' ') + 1;
            size_t end = request.find(' ', start);
            string path = request.substr(start, end - start);

            // По умолчанию отправляем index.html, если запрошен корневой путь
            if (path == "/") {
                path = "/index.html";
            }

            // Убираем начальный слэш
            if (path[0] == '/') {
                path = path.substr(1);
            }

            // Отправляем файл
            send_file(client_socket, path);
            closesocket(client_socket);
        }
    }

    closesocket(listen_socket);
    freeaddrinfo(addr);
    WSACleanup();
    return 0;
}