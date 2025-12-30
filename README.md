# HPC 双机实验（中文说明）

该工程实现三个函数：浮点数数组求和、求最大值、排序，支持基础版本与双机加速版本（OpenMP 等）。

主要改动与鲁棒性增强：
- 网络传输使用长度前缀（int32_t，网络字节序）+ 紧随数据的浮点字节流。
- `send_all` / `recv_all` 使用 64KB 分块发送/接收，并处理 `EINTR`、`EAGAIN` 重试。
- 对 socket 设置收发超时（默认 30 秒）。
- `connect_to_worker` 增加最大重试次数（默认 60 次），避免无限阻塞。

重要文件：
- `src/algorithm.h`：核心变换 `transform` 与数据规模宏（`SUBDATANUM`、`MAX_THREADS`、`DATANUM`）定义。
- `src/algorithm.cpp`：实现 `sum` / `max` / `sort`（基础版与加速版），以及 `init_data`（按索引线性初始化，确保两台机器区间无重叠）。
- `src/network.h` / `src/network.cpp`：网络封装，支持发送指令、单个 float、以及大数组（带长度前缀）。
- `src/main.cpp`：运行入口，支持 `--worker` / `--ip=` / `--port=` 和 `--small`（调试用小规模）参数。

运行说明（本机两进程测试示例）：
1. 在一台机器（或本机打开两个终端）先启动 worker：

```bash
# Worker：监听默认端口 8080
./hpc_app --worker --port=8080
```

2. 在另一台机器（或另一个终端）启动 master：

```bash
# Master：连接到 worker
./hpc_app --ip=127.0.0.1 --port=8080
```

3. 若仅用于快速调试（减少内存占用），在两端都加入 `--small`：

```bash
./hpc_app --worker --port=8080 --small
./hpc_app --ip=127.0.0.1 --port=8080 --small
```

教师复现需要修改的位置（常见项）：
- IP / 端口：在 `src/main.cpp` 中通过命令行 `--ip=`、`--port=` 修改。运行默认 IP 为 `127.0.0.1`，端口 `8080`。
- 数据规模：修改 `src/algorithm.h` 中的宏 `SUBDATANUM`（若内存不足请改为 `1000000`）和/或 `MAX_THREADS`，然后重新编译。
- 是否启用 SSE/OpenMP：在 `CMakeLists.txt` 或编译时传入相应编译选项（例如添加 `-fopenmp`）。

额外建议：
- 如果在异构机器（不同字节序）上运行，请注意浮点二进制的字节序兼容性。本实现直接传输 `float` 原始字节，假定运行环境为同构（x86_64）系统。
- 排序合并策略：本实现采用两台机器本地排序后主机归并（`final_merge`）。网络上只传输已排序的浮点数据，避免重复传输原始大数组。

问题反馈：如需我把 README 转为 PDF（用于提交），或添加自动化的本机端到端校验脚本（启动 worker -> master -> 校验排序正确性），我可以继续实现。