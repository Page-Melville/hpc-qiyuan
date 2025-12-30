#include "algorithm.h"
#include <iostream>
#include <omp.h> // 必须引入 OpenMP 头文件

// 基于 MurmurHash3 的混淆算法
inline float get_pseudo_random(size_t index) {
    unsigned int x = (unsigned int)index;
    x ^= x >> 16;
    x *= 0x85ebca6b;
    x ^= x >> 13;
    x *= 0xc2b2ae35;
    x ^= x >> 16;
    
    // x 现在是一个乱序的整数
    // 我们把它映射到 [1.0, 1001.0] 的浮点数范围
    // 注意：必须保证 > 0，因为后面要算 log(sqrt(x))
    return (x % 10000) / 10.0f + 1.0f;
}

// === 数据初始化 (修改版) ===
void init_data(float* data, int len, int offset) {
    // OpenMP 并行填充，速度飞快
    #pragma omp parallel for
    for (size_t i = 0; i < len; ++i) {
        // 使用全局索引 (i + offset) 作为种子
        // 这样 Master 和 Worker 生成的数据既随机，又不会重复
        data[i] = get_pseudo_random(i + offset);
    }
}

// 基础版本 - 无加速

float sum(const float data[], const int len) {
    float total = 0.0f;
    for (int i = 0; i < len; ++i) {
        total += transform(data[i]);
    }
    return total;
}

float max(const float data[], const int len) {
    if (len == 0) return 0.0f;
    float max_val = transform(data[0]);
    for (int i = 1; i < len; ++i) {
        float val = transform(data[i]);
        if (val > max_val) {
            max_val = val;
        }
    }
    return max_val;
}

// 归并排序辅助函数：合并两个有序区间
static void merge(float* arr, int l, int m, int r, float* temp) {
    int i = l;
    int j = m + 1;
    int k = 0;

    while (i <= m && j <= r) {
        if (transform(arr[i]) <= transform(arr[j])) {
            temp[k++] = arr[i++];
        } else {
            temp[k++] = arr[j++];
        }
    }
    while (i <= m) temp[k++] = arr[i++];
    while (j <= r) temp[k++] = arr[j++];

    for (i = 0; i < k; ++i) {
        arr[l + i] = temp[i];
    }
}

// 基础版递归
static void merge_sort_recursive(float* arr, int l, int r, float* temp) {
    if (l < r) {
        int m = l + (r - l) / 2;
        merge_sort_recursive(arr, l, m, temp);
        merge_sort_recursive(arr, m + 1, r, temp);
        merge(arr, l, m, r, temp);
    }
}

float sort(const float data[], const int len, float result[]) {
    // 1. 拷贝
    for (int i = 0; i < len; ++i) result[i] = data[i];
    
    // 2. 排序
    float* temp = new float[len];
    merge_sort_recursive(result, 0, len - 1, temp);
    delete[] temp;
    
    return 0.0f;
}

// 加速版本

// 加速版求和
float sumSpeedUp(const float data[], const int len) {
    float total = 0.0f;
    // reduction(+:total) 让每个线程有自己的 total，最后加起来
    #pragma omp parallel for reduction(+:total)
    for (int i = 0; i < len; ++i) {
        total += transform(data[i]);
    }
    return total;
}

// 加速版最大值
float maxSpeedUp(const float data[], const int len) {
    if (len == 0) return 0.0f;
    float max_val = -1e9f; // 初始极小值

    // reduction(max:max_val) OpenMP 3.1+ 支持直接求 max
    #pragma omp parallel for reduction(max:max_val)
    for (int i = 0; i < len; ++i) {
        float val = transform(data[i]);
        if (val > max_val) max_val = val;
    }
    return max_val;
}

// 加速版排序 (任务并行)

const int PARALLEL_THRESHOLD = 32768; // 阈值：任务太小就不分线程了

static void merge_sort_parallel(float* arr, int l, int r, float* temp) {
    if (l < r) {
        // 如果数据量小，直接用单线程递归，避免创建任务的开销
        if (r - l < PARALLEL_THRESHOLD) {
            merge_sort_recursive(arr, l, r, temp);
            return;
        }

        int m = l + (r - l) / 2;

        // 创建两个任务，分别处理左右两边
        #pragma omp task shared(arr, temp)
        merge_sort_parallel(arr, l, m, temp);

        #pragma omp task shared(arr, temp)
        merge_sort_parallel(arr, m + 1, r, temp);

        // 等待两个子任务完成
        #pragma omp taskwait
        
        // 合并结果 (这步依然是串行的，但这已经足够快了)
        merge(arr, l, m, r, temp);
    }
}

float sortSpeedUp(const float data[], const int len, float result[]) {
    // 1. 并行拷贝
    #pragma omp parallel for
    for (int i = 0; i < len; ++i) result[i] = data[i];

    float* temp = new float[len];

    // 2. 启动并行区域
    #pragma omp parallel
    {
        // 只有主线程开始第一个任务，后续任务由递归产生
        #pragma omp single
        {
            merge_sort_parallel(result, 0, len - 1, temp);
        }
    }

    delete[] temp;
    return 0.0f;
}