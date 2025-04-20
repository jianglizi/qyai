#ifndef AUDIO_RECOGNITION_H
#define AUDIO_RECOGNITION_H

#include <stdio.h>

// 函数声明

// 初始化音频设备
int init_audio_device();

// 获取音频数据并保存为文件
int record_audio_to_file(const char *file_path);

// 上传音频文件到 Whisper API
int upload_audio_to_api(const char *audio_file_path);

// 处理 API 响应并输出识别结果
int handle_api_response(const char *response, char *recognized_text);

// 启动实时语音识别
int start_realtime_recognition(const char *file_path, char *recognized_text);

// 清理资源
void cleanup();

#endif
