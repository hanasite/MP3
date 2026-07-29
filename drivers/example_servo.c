/*
 * Servo + CmdParser 完整例程
 * 适用: STM32F4xx, FreeRTOS + HAL
 *
 * CubeMX 配置:
 *   TIM4 → CH1 → PWM Generation, ARR=9999
 *   PSC = TIM_CLK / 500000 - 1   (F407 APB1=84M → PSC=167)
 *
 * 命令:
 *   /servo 90    — 绝对角度
 *   /add 10      — 增量 (回绕)
 *   /pos         — 查询
 */
#include "stm32f4xx_hal.h"
#include "cmsis_os2.h"
#include "usart.h"
#include "tim.h"
#include "Queue.h"
#include "CmdParser.h"
#include "Servo.h"
#include <stdio.h>
#include <string.h>

/* ==================== 命令处理 ==================== */

static void cmd_servo(const char *args, char *reply, int max)
{
    int angle = 0;
    sscanf(args, "%d", &angle);
    Servo_SetAngle((uint8_t)(angle < 0 ? 0 : (angle > 180 ? 180 : angle)));
    snprintf(reply, max, "OK %d\r\n", Servo_GetAngle());
}

static void cmd_add(const char *args, char *reply, int max)
{
    int delta = 0;
    sscanf(args, "%d", &delta);
    Servo_AddAngle((int16_t)delta);
    snprintf(reply, max, "OK %d\r\n", Servo_GetAngle());
}

static void cmd_pos(const char *args, char *reply, int max)
{
    (void)args;
    snprintf(reply, max, "%d\r\n", Servo_GetAngle());
}

static CmdDef cmds[] = {
    {"/servo", cmd_servo},
    {"/add",   cmd_add},
    {"/pos",   cmd_pos},
};

/* ==================== UART 任务 ==================== */

void Start_UART_TASK(void *argument)
{
    uint8_t rx;
    char reply[CMD_REPLY_LEN];

    Servo_Init(&htim4, TIM_CHANNEL_1);  /* 绑定 TIM4 CH1 */
    CmdParser_Init();
    CMD_UART1_IT_Start();

    for (;;) {
        osMessageQueueGet(CMD_QueueHandle, &rx, 0, osWaitForever);
        if (CmdParser_Feed(rx)) {
            char *rsp = CmdParser_Exec(cmds, 3, reply, sizeof(reply));
            if (rsp)
                HAL_UART_Transmit(&huart1, (uint8_t *)rsp, strlen(rsp), 100);
        }
    }
}
