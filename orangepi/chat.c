#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "chat.h"

// CURL 回调：动态扩展 buffer，安全地拼接响应数据
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsz = size * nmemb;
    struct Memory *mem = (struct Memory *)userp;

    // 为新的数据分配内存
    char *ptr = realloc(mem->data, mem->size + realsz + 1);
    if (!ptr) {
        fprintf(stderr, "内存分配失败\n");
        return 0;
    }
    mem->data = ptr;

    // 将新接收到的数据复制到内存中
    memcpy(&(mem->data[mem->size]), contents, realsz);
    mem->size += realsz;
    mem->data[mem->size] = 0;  // 末尾添加 NUL 字符表示结束
    return realsz;
}

// 获取AI的响应数据
int get_ai_response(const char *query, struct Memory *mem) {
    CURL *curl;
    CURLcode res;

    // 初始化CURL库
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "无法初始化 CURL\n");
        return -1;
    }

    // 设置目标URL和请求数据
    const char *url = "http://192.168.2.118:8000/chat/";  // 你的AI服务地址
    char post_data[512];
    snprintf(post_data, sizeof(post_data), "{\"message\":\"%s\"}", query);

    // 配置CURL的请求参数
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // 为响应数据分配内存
    mem->data = malloc(1);
    mem->size = 0;

    // 设置CURL的回调函数，用来接收响应数据
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)mem);

    // 执行请求
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "请求失败: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_global_cleanup();
        return -1;
    }

    // 清理CURL资源
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    curl_global_cleanup();
    return 0;
}

// 解析AI的响应数据，提取消息和命令
int parse_ai_response(struct Memory *mem, struct AIResponse *response) {
    // 提取 reply 字段中的消息内容
    char *start = strstr(mem->data, "\"reply\":\"");
    if (start) {
        start += strlen("\"reply\":\"");
        char *end = strchr(start, '"');
        if (end) {
            size_t msg_len = end - start;
            strncpy(response->msg, start, msg_len);
            response->msg[msg_len] = '\0';
        }
    }

    // 提取命令字段，通常是控制指令
    char *action_buf = response->cmd;
    char *tok_start = strstr(mem->data, "<|");
    if (tok_start) {
        char *tok_end = strstr(tok_start, "|>");
        if (tok_end && tok_end > tok_start + 2) {
            size_t len = tok_end - tok_start - 2;
            if (len < sizeof(response->cmd)) {
                strncpy(action_buf, tok_start + 2, len);
                action_buf[len] = '\0';
            }
        }
    }

    return 0;
}
