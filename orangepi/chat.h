#ifndef CHAT_H
#define CHAT_H

#include <stddef.h>

// 用于保存AI回应的结构体
struct Memory {
    char *data;
    size_t size;
};

// 用于表示AI回应的结构体，包括消息和命令
struct AIResponse {
    char msg[1024];  // 存储AI的回答
    char cmd[64];    // 存储指令（如控制命令）
};

// 函数声明
int get_ai_response(const char *query, struct Memory *mem);
int parse_ai_response(struct Memory *mem, struct AIResponse *response);

#endif // CHAT_H
