#ifndef ALGORITHM_H
#define ALGORITHM_H

#include <cmath>
#include <vector>

// 数据定义
#define MAX_THREADS 64
#define SUBDATANUM 2000000 
#define DATANUM (SUBDATANUM * MAX_THREADS)

// 命令定义
#define CMD_SUM 1
#define CMD_MAX 2
#define CMD_SORT 3
#define CMD_SUM_SPEEDUP 4
#define CMD_MAX_SPEEDUP 5
#define CMD_SORT_SPEEDUP 6

#define CMD_READY 99

// 核心变换函数transform
// 调用次数非常多的短函数使用inline，加快速度
inline float transform(float val) {
    return std::log(std::sqrt(val));
}

// 数据初始化
void init_data(float* data, int len, int offset);

// 无加速版本接口
float sum(const float data[], const int len);
float max(const float data[], const int len);
// result[] 用于存放结果，输入 data 不可修改
float sort(const float data[], const int len, float result[]);

// === 加速版本 ===
float sumSpeedUp(const float data[], const int len);
float maxSpeedUp(const float data[], const int len);
float sortSpeedUp(const float data[], const int len, float result[]);

#endif