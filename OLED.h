/*
 * OLED I2C 驱动 (软件/硬件I2C可选, HAL库)
 * 适用: STM32F4xx 系列 (F407/F411/F429 等)
 * FreeRTOS 兼容
 *
 * 硬件I2C: CubeMX 配好 I2C 外设，调用 OLED_Init_HW(&hi2cx)
 * 软件I2C: CubeMX 配好 SCL/SDA 引脚为开漏输出，调用 OLED_Init_SW(port,pin,port,pin)
 *         (DWT 微秒延时已内置，无需外部初始化)
 */
#ifndef __OLED_H
#define __OLED_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* 软件I2C — 仅绑定引脚，GPIO 由 CubeMX 初始化 */
void OLED_Init_SW(GPIO_TypeDef *scl_port, uint16_t scl_pin,
                  GPIO_TypeDef *sda_port, uint16_t sda_pin);

/* 硬件I2C — 仅绑定句柄，I2C 外设由 CubeMX 初始化 */
void OLED_Init_HW(I2C_HandleTypeDef *hi2c);

void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

#endif
