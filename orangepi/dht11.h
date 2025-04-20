#ifndef DHT11_H
#define DHT11_H

#include <pthread.h>
#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>

typedef unsigned char uint8;
typedef unsigned int  uint16;
typedef unsigned long uint32;

#define HIGH_TIME 32

extern int pinNumber;          // 用于读取数据的GPIO引脚
extern uint32 databuf;         // 数据存储变量
extern int blockFlag;          // 控制线程的标志

// GPIO初始化函数
void GPIO_init(int gpio_pin);

// DHT11起始信号发送函数
void DHT11_Start_Sig(void);

// 读取传感器数据的线程函数
void* readSensorData(void *arg);

#endif // DHT11_H
