#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#define MAX_PORTS 10

// 串口资源结构体
typedef struct {
    char dev_path[32];
    int fd;
    int is_used;
    pthread_mutex_t lock; // 核心：每个物理串口一个锁
    int timeout_ms;
    int max_retries;
} SerialResource;

// 全局资源池
SerialResource port_pool[MAX_PORTS];
int total_ports = 0;

/**
 * 线程安全的串口数据交换函数
 * 多个线程（如用户请求线程、自动轮询线程）都会调用此函数
 */
int safe_serial_exchange(const char *path, unsigned char *send_buf, int send_len, unsigned char *recv_buf, int max_recv) {
    SerialResource *res = NULL;

    // 1. 找到匹配的串口资源
    for (int i = 0; i < total_ports; i++) {
        if (strcmp(port_pool[i].dev_path, path) == 0) {
            res = &port_pool[i];
            break;
        }
    }
    if (!res) return -1;

    // 2. 加锁：如果其他线程正在操作此串口，本线程会在此阻塞等待
    pthread_mutex_lock(&res->lock);
    res->is_used = 1; // 标记占用

    int retry_count = 0;
    int success = 0;

    while (retry_count <= res->max_retries && !success) {
        printf("[Thread %ld] Operating on %s (Attempt %d)\n", pthread_self(), res->dev_path, retry_count + 1);
        
        // 模拟发送与接收
        // write(res->fd, send_buf, send_len);
        // usleep(res->timeout_ms * 1000);
        // int n = read(res->fd, recv_buf, max_recv);
        
        // 假设这里进行了实测校验（此处仅为逻辑演示）
        if (1 /* 校验成功 */) {
            success = 1;
        } else {
            retry_count++;
        }
    }

    // 3. 释放：解锁并重置占用状态
    res->is_used = 0;
    pthread_mutex_unlock(&res->lock);

    return success ? 0 : -2;
}

// 初始化函数：从 JSON 解析后调用
void init_serial_pool() {
    // 伪代码：解析 JSON 得到 ports 列表
    // 循环初始化：
    for (int i = 0; i < 3; i++) {
        sprintf(port_pool[i].dev_path, "/dev/ttys%d", i);
        pthread_mutex_init(&port_pool[i].lock, NULL);
        port_pool[i].max_retries = 3; 
        total_ports++;
    }
}

/**
 * 线程 A：负责定时轮询传感器
 */
void* thread_polling_sensor(void* arg) {
    unsigned char query_cmd[] = {0x01, 0x01, ...}; // 从 JSON 拿到的报文
    unsigned char buffer[256];
    while(1) {
        safe_serial_exchange("/dev/ttys4", query_cmd, 8, buffer, 256);
        usleep(500000); // 轮询周期 500ms
    }
}

/**
 * 线程 B：负责处理用户点击“开锁”请求
 */
void* thread_user_request(void* arg) {
    unsigned char open_cmd[] = {0x01, 0x05, ...}; // 从 JSON 拿到的开阀报文
    unsigned char buffer[256];
    // 用户随时可能触发，直接调用 safe_serial_exchange
    // 如果此时轮询线程正在占用串口，这里会自动排队等待锁释放
    safe_serial_exchange("/dev/ttys4", open_cmd, 8, buffer, 256);
    return NULL;
}