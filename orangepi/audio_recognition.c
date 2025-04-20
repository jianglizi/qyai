#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <time.h>
#include <fvad.h>

// 定义常量
#define PCM_DEVICE "plughw:3,0"     // 使用 plughw 接口，使 ALSA 自动转换采样率
#define RATE 16000                  // 目标采样率：16kHz
#define DESIRED_PERIOD 320          // 每个 period 320 帧（约 20ms 数据）
#define API_URL "http://192.168.2.118:8000/stt/"  // 根据实际修改API地址
#define WAV_HEADER_SIZE 44         // 新增：WAV文件头大小

// WAV 文件头结构（新增）
#pragma pack(push, 1)
typedef struct {
    char riff[4];                // "RIFF"
    uint32_t overall_size;       // 总文件大小 - 8字节
    char wave[4];                // "WAVE"
    char fmt_chunk_marker[4];    // "fmt "
    uint32_t length_of_fmt;      // fmt数据长度
    uint16_t format_type;        // 格式类型，1代表PCM
    uint16_t channels;           // 声道数
    uint32_t sample_rate;        // 采样率
    uint32_t byterate;           // 每秒字节数
    uint16_t block_align;        // 每帧字节数
    uint16_t bits_per_sample;    // 每个样本的位数
    char data_chunk_header[4];   // "data"
    uint32_t data_size;          // 数据区大小
} WAVHeader;
#pragma pack(pop)

// 全局变量
snd_pcm_t* pcm_handle = NULL;
snd_pcm_uframes_t period_size_glob = 0; // 实际的 period size
Fvad* fvad_instance = NULL;

/* 初始化音频设备和 libfvad */
int init_audio_device() {
    int rc;
    snd_pcm_hw_params_t *params;
    unsigned int rate = RATE;
    snd_pcm_uframes_t desired_period = DESIRED_PERIOD;
    
    // 打开 PCM 设备（使用 plughw 自动转换采样率）
    rc = snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        fprintf(stderr, "无法打开 PCM 设备 %s: %s\n", PCM_DEVICE, snd_strerror(rc));
        return -1;
    }
    
    snd_pcm_hw_params_alloca(&params);
    rc = snd_pcm_hw_params_any(pcm_handle, params);
    if (rc < 0) {
        fprintf(stderr, "无法获取硬件参数: %s\n", snd_strerror(rc));
        return -1;
    }

    // === 关键修改1：设置交错模式 ===
    rc = snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (rc < 0) {
        fprintf(stderr, "无法设置访问模式: %s\n", snd_strerror(rc));
        return -1;
    }

    // 设置采样率
    rc = snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0);
    if (rc < 0) {
        fprintf(stderr, "无法设置采样率: %s\n", snd_strerror(rc));
        return -1;
    }
    
    // 设置音频格式（16位小端）
    rc = snd_pcm_hw_params_set_format(pcm_handle, params, SND_PCM_FORMAT_S16_LE);
    if (rc < 0) {
        fprintf(stderr, "无法设置音频格式: %s\n", snd_strerror(rc));
        return -1;
    }
    
    // 设置为单声道
    rc = snd_pcm_hw_params_set_channels(pcm_handle, params, 1);
    if (rc < 0) {
        fprintf(stderr, "无法设置通道数: %s\n", snd_strerror(rc));
        return -1;
    }
    
    // 设置期望 period size 为 DESIRED_PERIOD（320帧）
    rc = snd_pcm_hw_params_set_period_size_near(pcm_handle, params, &desired_period, 0);
    if (rc < 0) {
        fprintf(stderr, "无法设置 period size: %s\n", snd_strerror(rc));
        return -1;
    }
    
    // === 关键修改2：获取实际设置的period size ===
    snd_pcm_hw_params_get_period_size(params, &period_size_glob, 0);
    printf("实际period size: %lu 帧\n", (unsigned long)period_size_glob);
    
    // 设置缓冲区大小，例如为 period 的 4 倍
    snd_pcm_uframes_t buffer_size = period_size_glob * 4;
    rc = snd_pcm_hw_params_set_buffer_size_near(pcm_handle, params, &buffer_size);
    if (rc < 0) {
        fprintf(stderr, "无法设置缓冲区大小: %s\n", snd_strerror(rc));
        return -1;
    }
    
    rc = snd_pcm_hw_params(pcm_handle, params);
    if (rc < 0) {
        fprintf(stderr, "无法设置硬件参数: %s\n", snd_strerror(rc));
        return -1;
    }
    
    printf("设备采样率：%d Hz, period size：%lu, 缓冲区大小：%lu\n",
           RATE, (unsigned long)period_size_glob, (unsigned long)buffer_size);
    
    rc = snd_pcm_prepare(pcm_handle);
    if (rc < 0) {
        fprintf(stderr, "无法准备 PCM 设备: %s\n", snd_strerror(rc));
        return -1;
    }
    
    // 初始化 libfvad 实例
    fvad_instance = fvad_new();
    if (!fvad_instance) {
        fprintf(stderr, "无法创建 VAD 实例\n");
        return -1;
    }
    if (fvad_set_mode(fvad_instance, 3) != 0) {
        fprintf(stderr, "设置 VAD 模式失败\n");
        return -1;
    }
    if (fvad_set_sample_rate(fvad_instance, RATE) != 0) {
        fprintf(stderr, "无法设置 VAD 采样率为 %dHz\n", RATE);
        return -1;
    }
    
    return 0;
}

/* 写入 WAV 文件头（新增函数） */
void write_wav_header(FILE *file, uint32_t data_size) {
    WAVHeader header = {0};
    memcpy(header.riff, "RIFF", 4);
    header.overall_size = data_size + WAV_HEADER_SIZE - 8;
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt_chunk_marker, "fmt ", 4);
    header.length_of_fmt = 16;
    header.format_type = 1;
    header.channels = 1;
    header.sample_rate = RATE;
    header.byterate = RATE * 2;        // 16000 * 1 * 2
    header.block_align = 2;
    header.bits_per_sample = 16;
    memcpy(header.data_chunk_header, "data", 4);
    header.data_size = data_size;

    fwrite(&header, sizeof(WAVHeader), 1, file);
}

/* 从 PCM 设备读取音频数据：先检测 0.5s 语音，再持续录制至人声停止 */
int record_audio_to_file(const char *file_path) {
    remove(file_path);
    short *buffer = malloc(period_size_glob * sizeof(short));
    if (!buffer) {
        fprintf(stderr, "内存分配失败\n");
        return -1;
    }

    short *detect_buffer = malloc(25 * period_size_glob * sizeof(short));  // 用于0.5秒的检测
    if (!detect_buffer) {
        fprintf(stderr, "检测缓存分配失败\n");
        free(buffer);
        return -1;
    }

    // printf("开始 0.5 秒语音检测...\n");
    int voice_found = 0;
    size_t detect_frames = 0;

    // 检测阶段：采集 0.5 秒用于 VAD 检测
    for (size_t i = 0; i < 25; ++i) {
        int rc = snd_pcm_readi(pcm_handle, buffer, period_size_glob);
        if (rc < 0) {
            if (rc == -EPIPE) { snd_pcm_prepare(pcm_handle); --i; continue; }
            if (rc == -EAGAIN) { usleep(1000); --i; continue; }
            fprintf(stderr, "读取错误(检测阶段): %s\n", snd_strerror(rc));
            free(buffer);
            free(detect_buffer);
            return -1;
        }
        if (fvad_process(fvad_instance, buffer, rc) == 1) {
            voice_found = 1;
        }
        memcpy(detect_buffer + detect_frames, buffer, rc * sizeof(short));
        detect_frames += rc;
    }

    // 如果没有检测到人声，则重新开始录制
    if (!voice_found) {
        free(detect_buffer);
        free(buffer);
        return record_audio_to_file(file_path);  // 重新开始录制
    }

    // 打开文件以追加内容
    FILE *file = fopen(file_path, "a+b");  // 使用追加模式
    if (!file) {
        fprintf(stderr, "无法打开文件写入: %s\n", file_path);
        free(buffer);
        free(detect_buffer);
        return -1;
    }

    // 写入 WAV 文件头（空头）
    write_wav_header(file, 0);
    uint32_t total_data_size = 0;
    fwrite(detect_buffer, sizeof(short), detect_frames, file);
    total_data_size += detect_frames * sizeof(short);

    free(detect_buffer);
    printf("检测到人声，开始持续录制，直到人声停止...\n");

    // 继续录制直到没有人声
    int silence_counter = 0;  // 连续无声周期计数器
    int max_silence_counter = 5;  // 设置最大无声周期（6个周期，即3秒）
    
    while (1) {
        int rc = snd_pcm_readi(pcm_handle, buffer, period_size_glob);
        if (rc < 0) {
            if (rc == -EPIPE) { snd_pcm_prepare(pcm_handle); continue; }
            if (rc == -EAGAIN) { usleep(1000); continue; }
            fprintf(stderr, "读取错误(录制阶段): %s\n", snd_strerror(rc));
            break;
        }
    
        // 判断是否有人声
        if (fvad_process(fvad_instance, buffer, rc) == 1) {
            // 有人声，继续录制
            fwrite(buffer, sizeof(short), rc, file);
            total_data_size += rc * sizeof(short);
            silence_counter = 0;  // 重置无声计数器
        } else {
            // 无人声，增加无声计数器
            silence_counter++;
            if (silence_counter >= max_silence_counter) {  // 连续3秒无声后停止
                break;
            }
            usleep(500000);  // 延时0.5秒继续检测
        }
    }

    // 更新 WAV 文件头中的数据大小
    fseek(file, 0, SEEK_SET);
    write_wav_header(file, total_data_size);
    fclose(file);
    free(buffer);

    printf("录制完成，共 %.2f 秒音频，保存到 %s\n", total_data_size / (float)(RATE * 2), file_path);
    return 0;
}

/* 以下函数为 API 示例部分（可根据实际需求调整） */

size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    // 计算收到的数据大小
    size_t real_size = size * nmemb;

    // 将响应数据追加到 response_data
    strncat((char*)userdata, (char*)ptr, real_size);

    // 打印接收到的响应数据（调试时可以检查）
    printf("Received response: %s\n", (char*)userdata);

    return real_size;
}

int handle_api_response(const char *response, char *recognized_text) {
    printf("语音转文字API响应: %s\n", response);  // 打印原始的 API 响应

    struct json_object *parsed_json;
    struct json_object *text;

    // 解析 JSON 响应
    parsed_json = json_tokener_parse(response);
    if (json_object_object_get_ex(parsed_json, "text", &text)) {
        snprintf(recognized_text, 1024, "%s", json_object_get_string(text));
        return 0;
    } else {
        printf("API响应中未找到 'text' 字段: %s\n", response);  // 提示找不到 "text" 字段
        return -1;
    }
}


// 上传文件进行识别
int upload_audio_to_api(const char *audio_file_path, char *response_data) {
    CURL *curl;
    CURLcode res;
    FILE *file = fopen(audio_file_path, "rb");
    if (!file) {
        fprintf(stderr, "无法打开文件: %s\n", audio_file_path);
        return -1;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    fseek(file, 0, SEEK_SET);  // 将文件指针重新指向文件开头

    // 初始化 cURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl) {
        struct curl_httppost *post = NULL;  // 用于构建 multipart/form-data 请求
        struct curl_httppost *last = NULL;

        // 添加文件数据到 multipart/form-data 请求
        curl_formadd(&post, &last,
                     CURLFORM_COPYNAME, "audio",  // 对应 FastAPI 中的参数名 "audio"
                     CURLFORM_FILE, audio_file_path, // 上传文件路径
                     CURLFORM_CONTENTTYPE, "audio/wav", // 设置正确的内容类型
                     CURLFORM_END);

        // 设置 API URL
        curl_easy_setopt(curl, CURLOPT_URL, API_URL);

        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10); // 设置最大超时为10秒

        // 设置 HTTP POST 表单数据
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, post);

        // 设置响应回调函数，将响应数据写入 response_data
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response_data);

        // 执行请求
        res = curl_easy_perform(curl);
        
        if (res != CURLE_OK) {
            fprintf(stderr, "上传失败: %s\n", curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            fclose(file);
            curl_global_cleanup();
            return -1;
        } else {
            printf("音频上传成功\n");
        }

        // 清理资源
        curl_easy_cleanup(curl);
        curl_formfree(post);
    }

    fclose(file);
    curl_global_cleanup();
    return 0;
}


int start_realtime_recognition(const char *file_path, char *recognized_text) {
    char response_data[2048] = {0};  // 用来存储 API 返回的响应

    if (record_audio_to_file(file_path) != 0) {
        printf("录音失败\n");
        return -1;
    }
    
    if (upload_audio_to_api(file_path, response_data) != 0) {
        printf("音频上传失败\n");
        return -1;
    }

    // 调用 handle_api_response 解析返回的响应
    return handle_api_response(response_data, recognized_text);
}

void cleanup() {
    if (pcm_handle) {
        snd_pcm_drain(pcm_handle);
        snd_pcm_close(pcm_handle);
    }
    if (fvad_instance) {
        fvad_free(fvad_instance);
    }
}

