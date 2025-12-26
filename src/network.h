#ifndef NETWORK_H
#define NETWORK_H

#include <string>

// === 基础通信函数 ===

// 启动 Server (Worker)，返回与 Master 建立连接后的 socket 文件描述符
int start_server(int port);

// 启动 Client (Master)，连接指定 IP 和端口，返回 socket 文件描述符
int connect_to_worker(std::string ip, int port);

// 发送一个指令 (如 CMD_SUM)
void send_cmd(int fd, int cmd);

// 接收一个指令
int recv_cmd(int fd);

// 发送/接收 单个浮点数 (用于 Sum/Max 结果)
void send_float(int fd, float val);
float recv_float(int fd);

// 发送/接收 大数组 (用于 Sort 结果)
// 核心难点：处理大数据分片传输
void send_data(int fd, const float* data, int len);
void recv_data(int fd, float* data, int len);

// 关闭连接
void close_socket(int fd);

#endif