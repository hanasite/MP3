/*
 * Servo — 舵机 PWM 驱动 (HAL + FreeRTOS)
 * 适用: STM32F4xx 系列
 *
 * 50Hz 公式: f = TIM_CLK / (PSC+1) / (ARR+1)
 * CubeMX 配置: ARR=9999, 则 PSC = TIM_CLK / 500000 - 1
 *   例: F407 APB1 Timer=84MHz → PSC=167, ARR=9999 → 50Hz
 *        F407 APB1 Timer=50MHz → PSC= 99, ARR=9999 → 50Hz
 *
 * 用法:
 *   Servo_Init(&htim4, TIM_CHANNEL_1);     // 绑定定时器句柄和通道
 *   Servo_SetAngle(90);                    // 绝对角度 0~180
 *   Servo_AddAngle(10);                    // 增量, 自动回绕
 */
#ifndef __SERVO_H
#define __SERVO_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* 角度 → CCR (0°=0.5ms=250, 180°=2.5ms=1250, ARR=9999) */
#define ANGLE_TO_CCR(angle) ((uint32_t)(250 + (angle) * 50 / 9))

void    Servo_Init(TIM_HandleTypeDef *htim, uint32_t channel);
void    Servo_SetAngle(uint8_t angle);
void    Servo_AddAngle(int16_t delta);
uint8_t Servo_GetAngle(void);

#endif
