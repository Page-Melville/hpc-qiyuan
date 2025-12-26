cmake_minimum_required(VERSION 3.10)

# 项目名称
project(DistributeComputeJob)

# 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 关键设置：开启优化和并行技术
# -O3: 最高级编译器优化
# -fopenmp: 若存在并行指令，开启 OpenMP 多线程支持
# -msse4.2: 开启 SSE 指令集支持，可以一次处理多个数据
# -g: 生成调试信息
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -fopenmp -msse4.2 -g")

# 自动查找当前源文件
file(GLOB SOURCES "src/*.cpp")

# 生成可执行文件
add_executable(hpc_app ${SOURCES})

# 打印一条消息确认配置成功
message(STATUS "Build setup ready. OpenMP and SSE enabled.")