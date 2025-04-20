#include "dht11.h"

// DHT11传感器所连接的GPIO引脚
int pinNumber = 6;  // GPIO6引脚用于读取数据
uint32 databuf;
int blockFlag;

// GPIO初始化
void GPIO_init(int gpio_pin)
{
    pinMode(gpio_pin, OUTPUT); // 设置GPIO为输出模式
    digitalWrite(gpio_pin, 1); // 输出高电平
    delay(1000);
}

// DHT11传感器起始信号发送
void DHT11_Start_Sig()
{
    pinMode(pinNumber, OUTPUT);
    digitalWrite(pinNumber, HIGH);
    digitalWrite(pinNumber, LOW);
    delay(25);   // 维持25ms低电平
    digitalWrite(pinNumber, HIGH);
    
    pinMode(pinNumber, INPUT);
    pullUpDnControl(pinNumber, PUD_UP);  // 上拉电阻，增强稳定性
    delayMicroseconds(35);  // 等待35微秒
}

// 读取传感器数据
void* readSensorData(void *arg)
{
    uint8 crc;   
    uint8 i;    
    int attempt = 5;   // 尝试次数

    while (attempt) {
        databuf = 0;    // 清空数据存储buf
        crc = 0;        // 清空校验位数据存储
        DHT11_Start_Sig(); 
        
        // 检测DHT11是否响应
        if (digitalRead(pinNumber) == 0) {
            while (!digitalRead(pinNumber)); // 等待高电平
            
            // 读取32位数据
            for (i = 0; i < 32; i++) {
                while (digitalRead(pinNumber));  // 数据时钟开始
                while (!digitalRead(pinNumber)); // 数据开始
                delayMicroseconds(HIGH_TIME);   // 检测32微秒的高电平
                databuf *= 2;  // 移位
                if (digitalRead(pinNumber) == 1) {
                    databuf++; // 如果是高电平，则该位为1
                }
            }
            
            // 读取校验位
            for (i = 0; i < 8; i++) {
                while (digitalRead(pinNumber));  // 数据时钟开始
                while (!digitalRead(pinNumber)); // 数据开始
                delayMicroseconds(HIGH_TIME);
                crc *= 2;  
                if (digitalRead(pinNumber) == 1) {
                    crc++; // 如果是高电平，则该位为1
                }
            }
            
            // 校验数据的准确性，如果温度大于50°C，则认为数据无效
            if (((databuf >> 8) & 0xff) > 50) {
                attempt--;
                delay(500); // 延迟后重试
                continue;
            } else {
                // 打印数据
                printf("恭喜！传感器数据读取成功！\n");
                printf("湿度: %lu.%lu\n", (databuf >> 24) & 0xff, (databuf >> 16) & 0xff); 
                printf("温度: %lu.%lu\n", (databuf >> 8) & 0xff, databuf & 0xff);
                blockFlag = 0;  // 结束线程
                return (void*)1;
            }
        } else {
            blockFlag = 0;
            printf("抱歉！传感器未响应！\n");
            return (void*)0;
        }
    }

    blockFlag = 0;
    printf("获取数据失败\n");
    return (void*)2;
}
