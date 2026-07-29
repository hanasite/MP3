/*
 * Servo — 舵机 PWM 驱动 (HAL + FreeRTOS)
 * 适用: STM32F4xx 系列
 *
 * CubeMX 配置:
 *   TIMx → CHx → PWM Generation
 *   50Hz = TIM_CLK / (PSC+1) / (ARR+1)
 *   ARR 固定 9999, PSC = TIM_CLK / 500000 - 1
 */
#include "Servo.h"

static TIM_HandleTypeDef *servo_htim;
static uint32_t           servo_channel;
static uint8_t            servo_angle;

void Servo_Init(TIM_HandleTypeDef *htim, uint32_t channel)
{
    servo_htim    = htim;
    servo_channel = channel;
    servo_angle   = 0;
    HAL_TIM_PWM_Start(servo_htim, servo_channel);
    __HAL_TIM_SET_COMPARE(servo_htim, servo_channel, ANGLE_TO_CCR(0));
}

void Servo_SetAngle(uint8_t angle)
{
    if (angle > 180) angle = 180;
    servo_angle = angle;
    __HAL_TIM_SET_COMPARE(servo_htim, servo_channel, ANGLE_TO_CCR(servo_angle));
}

void Servo_AddAngle(int16_t delta)
{
    int16_t new_angle = (int16_t)servo_angle + delta;
    while (new_angle > 180) new_angle -= 180;
    while (new_angle < 0)   new_angle += 180;
    servo_angle = (uint8_t)new_angle;
    __HAL_TIM_SET_COMPARE(servo_htim, servo_channel, ANGLE_TO_CCR(servo_angle));
}

uint8_t Servo_GetAngle(void)
{
    return servo_angle;
}
