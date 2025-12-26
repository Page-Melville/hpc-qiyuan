#include "network.h"
#include <iostream>
#include <cstring>
#include <unistd.h>     // close
#include <sys/socket.h> // socket, bind, listen...
#include <arpa/inet.h>  // inet_addr
#include <netinet/in.h>

// 辅助宏：检查 Socket 错误
void check_error(int res, const char* msg) {
    if (res < 0) {
        perror(msg);
        exit(1);
    }
}

// 循环发送，确保字节已发送
void send_all(int fd, const void* buffer, size_t length) {
    const char* ptr = static_cast<const char*>(buffer);
    size_t remaining = length;
    while (remaining > 0) {
        ssize_t sent = send(fd, ptr, remaining, 0);
        if (sent <= 0) {
            check_error(-1, "Send failed");
        }
        ptr += sent;
        remaining -= sent;
    }
}

// 循环接收，确保所有字节以收集
void recv_all(int fd, void* buffer, size_t length) {
    char* ptr = static_cast<char*>(buffer);
    size_t remaining = length;
    while (remaining > 0) {
        ssize_t received = recv(fd, ptr, remaining, 0);
        if (received <= 0) {
            check_error(-1, "Recv failed (Connection closed?)");
        }
        ptr += received;
        remaining -= received;
    }
}

// 1. Worker: 启动服务器
int start_server(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    check_error(server_fd, "Socket creation failed");

    // 允许端口复用（防止关掉程序后还要等几分钟才能重用端口）
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // 监听所有网卡
    address.sin_port = htons(port);       // 端口号转网络字节序

    check_error(bind(server_fd, (struct sockaddr*)&address, sizeof(address)), "Bind failed");
    check_error(listen(server_fd, 1), "Listen failed"); // 只允许 1 个连接

    std::cout << "[Network] Worker listening on port " << port << "..." << std::endl;

    // 阻塞等待 Master 连接
    int new_socket = accept(server_fd, nullptr, nullptr);
    check_error(new_socket, "Accept failed");
    
    std::cout << "[Network] Master connected!" << std::endl;
    
    return new_socket;
}

// 2. Master: 连接服务器
int connect_to_worker(std::string ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    check_error(sock, "Socket creation failed");

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // 将字符串 IP 转为二进制
    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "[Error] Invalid IP address" << std::endl;
        exit(1);
    }

    std::cout << "[Network] Connecting to " << ip << ":" << port << "..." << std::endl;
    // 尝试连接，如果 Worker 没开，此处会失败
    while (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cout << "  Retrying in 1s..." << std::endl;
        sleep(1);
    }
    
    std::cout << "[Network] Connected to Worker!" << std::endl;
    return sock;
}

// 数据传输封装

void send_cmd(int fd, int cmd) {
    send_all(fd, &cmd, sizeof(cmd));
}

int recv_cmd(int fd) {
    int cmd;
    recv_all(fd, &cmd, sizeof(cmd));
    return cmd;
}

void send_float(int fd, float val) {
    send_all(fd, &val, sizeof(float));
}

float recv_float(int fd) {
    float val;
    recv_all(fd, &val, sizeof(float));
    return val;
}

void send_data(int fd, const float* data, int len) {
    // 发送 huge array
    send_all(fd, data, len * sizeof(float));
}

void recv_data(int fd, float* data, int len) {
    recv_all(fd, data, len * sizeof(float));
}

void close_socket(int fd) {
    close(fd);
}