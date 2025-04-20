#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wiringPi.h>

#include "chat.h"
#include "audio_recognition.h"

// 控制GPIO（用于模拟实际动作）
void control_gpio(const char *action) {
    if (strcmp(action, "light_on") == 0) {
        digitalWrite(6, HIGH); // 假设用 BCM 6 来控制灯
        printf("GPIO 6 点亮\n");
    } else if (strcmp(action, "light_off") == 0) {
        digitalWrite(6, LOW); // 假设用 BCM 6 来控制灯
        printf("GPIO 6 关闭\n");
    } else {
        printf("未识别动作：%s\n", action);
    }
}

int main() {
    // 初始化 GPIO
    if (wiringPiSetup() == -1) {
        fprintf(stderr, "wiringPiSetupGpio 初始化失败\n");
        return 1;
    }
    pinMode(6, OUTPUT);

    // char user_input[256];
    struct Memory mem;
    struct AIResponse response;

    // while (1) {
    //     printf("请输入指令: ");
    //     if (!fgets(user_input, sizeof(user_input), stdin)) break;
    //     user_input[strcspn(user_input, "\n")] = 0;

    //     if (get_ai_response(user_input, &mem) == 0) {
    //         // 解析AI响应
    //         parse_ai_response(&mem, &response);

    //         // 输出 AI 回答和动作
    //         printf("回答：%s\n", response.msg);
    //         if (response.cmd[0]) {
    //             printf("动作：%s\n", response.cmd);
    //             control_gpio(response.cmd);
    //         } else {
    //             printf("动作：无\n");
    //         }
    //     }

    //     free(mem.data);
    // }

    // return 0;

    if (init_audio_device() != 0) {
        fprintf(stderr, "初始化音频设备失败\n");
        return -1;
    }
    
    const char *audio_file = "recorded_audio.wav";
    // 录制音频（可添加定时停止或其它退出条件）
    
    printf("程序运行\n");
    // 调用 API 进行识别（示例）
    char recognized_text[1024] = {0};
    int i=0;
    while (1)
    {   
        if (start_realtime_recognition(audio_file, recognized_text) == 0) {
            printf("识别结果: %s\n", recognized_text);

            if (get_ai_response(recognized_text, &mem) == 0 && strcmp(recognized_text, "") != 0 && !(sizeof(recognized_text)<2) ) {

                printf("上传到ai进行对话\n");

                // 解析AI响应
                parse_ai_response(&mem, &response);
        
                // 输出 AI 回答和动作
                printf("\nAI回答：%s \n \n", response.msg);
                
                if (response.cmd[0]) {
                    printf("动作：%s\n", response.cmd);
                    control_gpio(response.cmd);
                } else {
                    printf("动作：无\n");
                }
            } else{
                printf("不进行ai对话\n");
            }
        } else {
            printf("识别失败\n");
        }
        i++;
    }

    free(mem.data);
    cleanup();
    return 0;
}
