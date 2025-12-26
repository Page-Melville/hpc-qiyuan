#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <ctime>
#include <iomanip>
#include "algorithm.h"
#include "network.h"

// 计时辅助
double get_elapsed_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1e6;
}

// 最终归并 (用于 Master 合并结果)
void final_merge(const float* partA, int lenA, const float* partB, int lenB, float* result) {
    int i = 0, j = 0, k = 0;
    while (i < lenA && j < lenB) {
        if (transform(partA[i]) <= transform(partB[j])) {
            result[k++] = partA[i++];
        } else {
            result[k++] = partB[j++];
        }
    }
    while (i < lenA) result[k++] = partA[i++];
    while (j < lenB) result[k++] = partB[j++];
}

// Worker逻辑

void run_worker(int port) {
    int sock = start_server(port);

    int half_len = DATANUM / 2;
    std::cout << "[Worker] Allocating memory..." << std::endl;
    std::vector<float> local_data(half_len);
    init_data(local_data.data(), half_len, half_len); // Worker 偏移量

    std::cout << "[Worker] Ready. Waiting for commands..." << std::endl;

    while (true) {
        // 阻塞等待命令，如果 Master 断开，recv_cmd 会报错或返回 0
        int cmd = recv_cmd(sock);
        
        // 基础版命令
        if (cmd == CMD_SUM) {
            std::cout << "[Worker] CMD_SUM -> Processing..." << std::endl;
            float s = sum(local_data.data(), half_len);
            send_float(sock, s);
        } 
        else if (cmd == CMD_MAX) {
            std::cout << "[Worker] CMD_MAX -> Processing..." << std::endl;
            float m = max(local_data.data(), half_len);
            send_float(sock, m);
        } 
        else if (cmd == CMD_SORT) {
            std::cout << "[Worker] CMD_SORT -> Processing..." << std::endl;
            std::vector<float> sorted_data(half_len);
            sort(local_data.data(), half_len, sorted_data.data());
            std::cout << "[Worker] Sending data..." << std::endl;
            send_data(sock, sorted_data.data(), half_len);
            std::cout << "[Worker] Done." << std::endl;
            // 注意：这里删除了 break，让 Worker 继续服务
        }
        // === 加速版命令 ===
        else if (cmd == CMD_SUM_SPEEDUP) {
            std::cout << "[Worker] CMD_SUM_SPEEDUP -> Processing..." << std::endl;
            float s = sumSpeedUp(local_data.data(), half_len);
            send_float(sock, s);
        }
        else if (cmd == CMD_MAX_SPEEDUP) {
            std::cout << "[Worker] CMD_MAX_SPEEDUP -> Processing..." << std::endl;
            float m = maxSpeedUp(local_data.data(), half_len);
            send_float(sock, m);
        }
        else if (cmd == CMD_SORT_SPEEDUP) {
            std::cout << "[Worker] CMD_SORT_SPEEDUP -> Processing..." << std::endl;
            std::vector<float> sorted_data(half_len);
            sortSpeedUp(local_data.data(), half_len, sorted_data.data()); // 调用加速版
            std::cout << "[Worker] Sending data..." << std::endl;
            send_data(sock, sorted_data.data(), half_len);
            std::cout << "[Worker] Done." << std::endl;
        }
    }
    close_socket(sock);
}


// Master逻辑
void run_master(std::string ip, int port) {
    std::cout << "=== Running as MASTER ===" << std::endl;
    int half_len = DATANUM / 2;
    std::vector<float> local_data(half_len);
    init_data(local_data.data(), half_len, 0);

    int sock = connect_to_worker(ip, port);
    
    // 变量定义
    double t_basic_sum, t_speed_sum;
    double t_basic_max, t_speed_max;
    double t_basic_sort, t_speed_sort;
    struct timespec start, end;
    
    // 缓冲区
    std::vector<float> local_sorted(half_len);
    std::vector<float> remote_sorted(half_len);
    std::vector<float> final_res(DATANUM);


    // Round 1: 基础版本 (Basic)

    std::cout << "\n-------------------------------------------" << std::endl;
    std::cout << "=== Round 1: Basic Version (Single Thread) ===" << std::endl;
    std::cout << "-------------------------------------------" << std::endl;
    
    // 1. SUM
    std::cout << "[Basic] SUM...  " << std::flush;
    clock_gettime(CLOCK_MONOTONIC, &start);
    send_cmd(sock, CMD_SUM);
    float s1 = sum(local_data.data(), half_len);
    float s2 = recv_float(sock);
    float total_s = s1 + s2;
    clock_gettime(CLOCK_MONOTONIC, &end);
    t_basic_sum = get_elapsed_ms(start, end);
    std::cout << "Time: " << t_basic_sum << " ms | Result: " << total_s << std::endl;

    // 2. MAX
    std::cout << "[Basic] MAX...  " << std::flush;
    clock_gettime(CLOCK_MONOTONIC, &start);
    send_cmd(sock, CMD_MAX);
    float m1 = max(local_data.data(), half_len);
    float m2 = recv_float(sock);
    float total_m = (transform(m1) > transform(m2)) ? m1 : m2;
    clock_gettime(CLOCK_MONOTONIC, &end);
    t_basic_max = get_elapsed_ms(start, end);
    std::cout << "Time: " << t_basic_max << " ms | Result: " << total_m << std::endl;

    // 3. SORT
    std::cout << "[Basic] SORT... " << std::flush;
    clock_gettime(CLOCK_MONOTONIC, &start);
    send_cmd(sock, CMD_SORT);
    sort(local_data.data(), half_len, local_sorted.data()); // 慢速
    recv_data(sock, remote_sorted.data(), half_len);
    final_merge(local_sorted.data(), half_len, remote_sorted.data(), half_len, final_res.data());
    clock_gettime(CLOCK_MONOTONIC, &end);
    t_basic_sort = get_elapsed_ms(start, end);
    std::cout << "Time: " << t_basic_sort << " ms" << std::endl;



    // Round 2: 加速版本 (SpeedUp)

    std::cout << "\n-------------------------------------------" << std::endl;
    std::cout << "=== Round 2: SpeedUp Version (OpenMP + SSE) ===" << std::endl;
    std::cout << "-------------------------------------------" << std::endl;

    // 1. SUM SpeedUp
    std::cout << "[Fast]  SUM...  " << std::flush;
    clock_gettime(CLOCK_MONOTONIC, &start);
    send_cmd(sock, CMD_SUM_SPEEDUP); // 发送新命令
    float fs1 = sumSpeedUp(local_data.data(), half_len); // 快速
    float fs2 = recv_float(sock);
    float f_total_s = fs1 + fs2;
    clock_gettime(CLOCK_MONOTONIC, &end);
    t_speed_sum = get_elapsed_ms(start, end);
    std::cout << "Time: " << t_speed_sum << " ms | Result: " << f_total_s << std::endl;

    // 2. MAX SpeedUp
    std::cout << "[Fast]  MAX...  " << std::flush;
    clock_gettime(CLOCK_MONOTONIC, &start);
    send_cmd(sock, CMD_MAX_SPEEDUP); // 发送新命令
    float fm1 = maxSpeedUp(local_data.data(), half_len); // 快速
    float fm2 = recv_float(sock);
    float f_total_m = (transform(fm1) > transform(fm2)) ? fm1 : fm2;
    clock_gettime(CLOCK_MONOTONIC, &end);
    t_speed_max = get_elapsed_ms(start, end);
    std::cout << "Time: " << t_speed_max << " ms | Result: " << f_total_m << std::endl;

    // 3. SORT SpeedUp
    std::cout << "[Fast]  SORT... " << std::flush;
    clock_gettime(CLOCK_MONOTONIC, &start);
    send_cmd(sock, CMD_SORT_SPEEDUP); // 发送新命令
    sortSpeedUp(local_data.data(), half_len, local_sorted.data()); // 快速
    recv_data(sock, remote_sorted.data(), half_len);
    final_merge(local_sorted.data(), half_len, remote_sorted.data(), half_len, final_res.data());
    clock_gettime(CLOCK_MONOTONIC, &end);
    t_speed_sort = get_elapsed_ms(start, end);
    std::cout << "Time: " << t_speed_sort << " ms" << std::endl;

    // ============================================
    // 最终结果
    // ============================================
    std::cout << "\n===========================================" << std::endl;
    std::cout << "             Final Report                  " << std::endl;
    std::cout << "===========================================" << std::endl;
    std::cout << "Task   | Basic (ms) | SpeedUp (ms) | SpeedUp Ratio" << std::endl;
    std::cout << "-------|------------|--------------|--------------" << std::endl;
    std::cout << "SUM    | " << std::setw(10) << t_basic_sum << " | " << std::setw(12) << t_speed_sum << " | " << std::fixed << std::setprecision(2) << t_basic_sum / t_speed_sum << "x" << std::endl;
    std::cout << "MAX    | " << std::setw(10) << t_basic_max << " | " << std::setw(12) << t_speed_max << " | " << std::fixed << std::setprecision(2) << t_basic_max / t_speed_max << "x" << std::endl;
    std::cout << "SORT   | " << std::setw(10) << t_basic_sort << " | " << std::setw(12) << t_speed_sort << " | " << std::fixed << std::setprecision(2) << t_basic_sort / t_speed_sort << "x" << std::endl;
    
    close_socket(sock);
}

int main(int argc, char* argv[]) {
    std::string mode = "master";
    std::string ip = "127.0.0.1";
    int port = 8080;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--worker") == 0) mode = "worker";
        else if (strncmp(argv[i], "--ip=", 5) == 0) ip = argv[i] + 5;
        else if (strncmp(argv[i], "--port=", 7) == 0) port = std::atoi(argv[i] + 7);
    }

    if (mode == "worker") run_worker(port);
    else run_master(ip, port);

    return 0;
}