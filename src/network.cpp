#include "network.h"
#include <iostream>
#include <cstring>
#include <unistd.h>     // close
#include <sys/socket.h> // socket, bind, listen...
#include <arpa/inet.h>  // inet_addr
#include <netinet/in.h>
#include <errno.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <vector>
#include <vector>

// 辅助宏：检查 Socket 错误
void check_error(int res, const char* msg) {
    if (res < 0) {
        perror(msg);
        exit(1);
    }
}

// 配置 socket 超时（秒）并调整缓冲区
static void set_socket_timeout_and_buffers(int fd, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    // 提高 socket 缓冲区，减少内核拷贝与上下文切换
    int buf = 4 * 1024 * 1024; // 4MB
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
}

// 循环发送，确保字节已发送
void send_all(int fd, const void* buffer, size_t length) {
    const char* ptr = static_cast<const char*>(buffer);
    size_t remaining = length;
    const size_t CHUNK = 1024 * 1024; // 1MB 分块
    int retry_count = 0;
    const int MAX_RETRY = 100;
    while (remaining > 0) {
        size_t to_send = std::min(remaining, CHUNK);
#ifdef MSG_NOSIGNAL
        ssize_t sent = send(fd, ptr, to_send, MSG_NOSIGNAL);
#else
        ssize_t sent = send(fd, ptr, to_send, 0);
#endif
        if (sent < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (++retry_count > MAX_RETRY) check_error(-1, "Send failed (EAGAIN)");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            check_error(-1, "Send failed");
        } else if (sent == 0) {
            if (++retry_count > MAX_RETRY) check_error(-1, "Send failed (sent 0)");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        ptr += sent;
        remaining -= sent;
        retry_count = 0;
    }
}

// 循环接收，确保所有字节以收集
void recv_all(int fd, void* buffer, size_t length) {
    char* ptr = static_cast<char*>(buffer);
    size_t remaining = length;
    const size_t CHUNK = 64 * 1024;
    int retry_count = 0;
    const int MAX_RETRY = 100;
    while (remaining > 0) {
        size_t to_recv = std::min(remaining, CHUNK);
        ssize_t received = recv(fd, ptr, to_recv, 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (++retry_count > MAX_RETRY) check_error(-1, "Recv failed (EAGAIN)");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            check_error(-1, "Recv failed (Connection closed?)");
        } else if (received == 0) {
            check_error(-1, "Recv failed (peer closed connection)");
        }
        ptr += received;
        remaining -= received;
        retry_count = 0;
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
    
    // 为连接设置合理超时与较大缓冲区，避免长时间阻塞并提高吞吐
    set_socket_timeout_and_buffers(new_socket, 30);
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
    // 尝试连接，如果 Worker 没开，此处会失败，最多重试若干次
    const int MAX_RETRIES = 60;
    int attempts = 0;
    while (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        ++attempts;
        if (attempts >= MAX_RETRIES) {
            std::cerr << "[Network] connect failed after " << attempts << " attempts." << std::endl;
            check_error(-1, "Connect failed");
        }
        std::cout << "  Retrying in 1s... (" << attempts << ")" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // 连接建立后设置超时与缓冲区
    set_socket_timeout_and_buffers(sock, 30);
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
    using namespace std::chrono;
    auto t0 = high_resolution_clock::now();

    // 发送长度前缀（网络字节序）
    int32_t net_len = htonl(len);
    send_all(fd, &net_len, sizeof(net_len));

    // 分块发送数据并显示进度条
    size_t total_bytes = (size_t)len * sizeof(float);
    const size_t CHUNK = 1024 * 1024; // 1MB
    size_t sent_bytes = 0;
    const int BAR_WIDTH = 50;
    while (sent_bytes < total_bytes) {
        size_t to_send = std::min(CHUNK, total_bytes - sent_bytes);
#ifdef MSG_NOSIGNAL
        ssize_t s = send(fd, reinterpret_cast<const char*>(data) + sent_bytes, to_send, MSG_NOSIGNAL);
#else
        ssize_t s = send(fd, reinterpret_cast<const char*>(data) + sent_bytes, to_send, 0);
#endif
        if (s < 0) {
            if (errno == EINTR) continue;
            check_error(-1, "send_data: send failed");
        }
        sent_bytes += (size_t)s;

        // 打印进度条（覆盖行）
        double progress = (double)sent_bytes / (double)total_bytes;
        int pos = (int)(BAR_WIDTH * progress);
        std::cout << "[Network] Sending: [";
        for (int i = 0; i < BAR_WIDTH; ++i) std::cout << (i < pos ? '=' : (i == pos ? '>' : ' '));
        std::cout << "] " << int(progress * 100.0) << "% (" << (sent_bytes / (1024*1024)) << " MB/" << (total_bytes / (1024*1024)) << " MB)\r" << std::flush;
    }
    std::cout << std::endl;

    auto t1 = high_resolution_clock::now();
    double ms = duration<double, std::milli>(t1 - t0).count();
    double mb = total_bytes / (1024.0 * 1024.0);
    double bw = ms > 0 ? mb / (ms / 1000.0) : 0.0;
    std::cout << "[Network] send_data: sent " << len << " floats (" << mb << " MB) in " << ms << " ms, " << bw << " MB/s" << std::endl;
}

void recv_data(int fd, float* data, int len) {
    using namespace std::chrono;
    auto t0 = high_resolution_clock::now();

    // 先接收长度前缀
    int32_t net_len = 0;
    recv_all(fd, &net_len, sizeof(net_len));
    int32_t remote_len = ntohl(net_len);
    if (remote_len <= 0) {
        std::cerr << "[Network] recv_data: invalid remote_len=" << remote_len << std::endl;
        exit(1);
    }

    if (remote_len != len) {
        std::cerr << "[Network] recv_data: expected len=" << len << " but remote sent=" << remote_len << ", will adjust read." << std::endl;
    }

    int to_read = std::min(remote_len, len);
    size_t total_bytes = (size_t)to_read * sizeof(float);
    const size_t CHUNK = 1024 * 1024; // 1MB
    size_t recvd_bytes = 0;
    const int BAR_WIDTH = 50;
    while (recvd_bytes < total_bytes) {
        size_t to_recv = std::min(CHUNK, total_bytes - recvd_bytes);
        ssize_t r = recv(fd, reinterpret_cast<char*>(data) + recvd_bytes, to_recv, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            check_error(-1, "recv_data: recv failed");
        }
        if (r == 0) {
            check_error(-1, "recv_data: peer closed");
        }
        recvd_bytes += (size_t)r;

        double progress = (double)recvd_bytes / (double)total_bytes;
        int pos = (int)(BAR_WIDTH * progress);
        std::cout << "[Network] Receiving: [";
        for (int i = 0; i < BAR_WIDTH; ++i) std::cout << (i < pos ? '=' : (i == pos ? '>' : ' '));
        std::cout << "] " << int(progress * 100.0) << "% (" << (recvd_bytes / (1024*1024)) << " MB/" << (total_bytes / (1024*1024)) << " MB)\r" << std::flush;
    }
    std::cout << std::endl;

    // 如果远端发送更多数据，消耗剩余字节以保持流同步
    if (remote_len > len) {
        size_t remaining = (size_t)(remote_len - len) * sizeof(float);
        std::vector<char> tmp(remaining);
        recv_all(fd, tmp.data(), remaining);
    }

    auto t1 = high_resolution_clock::now();
    double ms = duration<double, std::milli>(t1 - t0).count();
    double mb = total_bytes / (1024.0 * 1024.0);
    double bw = ms > 0 ? mb / (ms / 1000.0) : 0.0;
    std::cout << "[Network] recv_data: recv " << to_read << " floats (" << mb << " MB) in " << ms << " ms, " << bw << " MB/s" << std::endl;
}

void close_socket(int fd) {
    close(fd);
}