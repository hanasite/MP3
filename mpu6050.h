/*
 * MPU6050 6轴传感器驱动 (I2C 接口, HAL 库)
 * 适用: STM32F4xx 系列 (F407/F411/F429 等)
 *
 * 原始驱动来源: 逐飞科技 TC264 开源库 zf_device_mpu6050
 * 移植适配: STM32 HAL 库
 *
 * 硬件I2C: CubeMX 配好 I2C 外设，调用 MPU6050_Init_HW(&hi2cx)
 * 软件I2C: CubeMX 配好 SCL/SDA 引脚为开漏输出，调用 MPU6050_Init_SW(port,pin,port,pin)
 */
#ifndef __MPU6050_H
#define __MPU6050_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ==================== MPU6050 寄存器地址 ==================== */
#define MPU6050_DEV_ADDR        (0xD0 >> 1)     /* I2C 7位地址 */

#define MPU6050_SMPLRT_DIV      (0x19)          /* 采样率分频 */
#define MPU6050_CONFIG          (0x1A)          /* 低通滤波器 */
#define MPU6050_GYRO_CONFIG     (0x1B)          /* 陀螺仪配置 */
#define MPU6050_ACCEL_CONFIG    (0x1C)          /* 加速度计配置 */
#define MPU6050_INT_PIN_CFG     (0x37)          /* 中断引脚配置 */
#define MPU6050_ACCEL_XOUT_H    (0x3B)          /* 加速度 X 高字节 */
#define MPU6050_GYRO_XOUT_H     (0x43)          /* 陀螺仪 X 高字节 */
#define MPU6050_USER_CONTROL    (0x6A)          /* 用户控制 */
#define MPU6050_PWR_MGMT_1      (0x6B)          /* 电源管理 */
#define MPU6050_WHO_AM_I        (0x75)          /* 设备 ID (默认 0x68) */

/* ==================== 量程配置 ==================== */
#define MPU6050_ACC_SAMPLE      (0x10)          /* 加速度计: +-8g  (修改此值切换量程) */
/* 0x00 -> +-2g  (16384 LSB/g)   0x08 -> +-4g  (8192 LSB/g)
   0x10 -> +-8g  (4096 LSB/g)    0x18 -> +-16g (2048 LSB/g) */

#define MPU6050_GYR_SAMPLE      (0x18)          /* 陀螺仪: +-2000dps (修改此值切换量程) */
/* 0x00 -> +-250dps  (131.0 LSB/dps)   0x08 -> +-500dps  (65.5 LSB/dps)
   0x10 -> +-1000dps (32.8 LSB/dps)    0x18 -> +-2000dps (16.4 LSB/dps) */

/* ==================== 全局变量 ==================== */
extern int16_t mpu6050_gyro_x, mpu6050_gyro_y, mpu6050_gyro_z;
extern int16_t mpu6050_acc_x,  mpu6050_acc_y,  mpu6050_acc_z;

/* ==================== 初始化 ==================== */

/* 硬件 I2C: 绑定 HAL I2C 句柄 (I2C 外设由 CubeMX 初始化) */
uint8_t MPU6050_Init_HW(I2C_HandleTypeDef *hi2c);

/* 软件 I2C: 绑定 GPIO 引脚 (引脚由 CubeMX 初始化为开漏输出) */
uint8_t MPU6050_Init_SW(GPIO_TypeDef *scl_port, uint16_t scl_pin,
                         GPIO_TypeDef *sda_port, uint16_t sda_pin);

/* ==================== 数据读取 ==================== */
void    MPU6050_GetAcc(void);                           /* 读取加速度计数据 */
void    MPU6050_GetGyro(void);                          /* 读取陀螺仪数据 */

/* ==================== 数据转换 ==================== */
float   MPU6050_AccTransition(int16_t acc_value);       /* 加速度原始值 -> g */
float   MPU6050_GyroTransition(int16_t gyro_value);     /* 陀螺仪原始值 -> dps */

#endif