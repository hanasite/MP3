/*
 * CmdParser 例程 — FreeRTOS + HAL UART 命令解析
 * 适用: STM32F4xx 系列
 *
 * 前置条件:
 *   1. CubeMX 配好 USART1 (115200 8N1), NVIC 使能中断, 生成 usart.c
 *   2. CubeMX 创建 CMD_Queue: osMessageQueueNew(128, sizeof(uint8_t), ...)
 *   3. CubeMX 创建 UART_TASK
 *   4. usart.c 中添加中断回调 (见下方 USART 中断配套代码)
 */
#include "stm32f4xx_hal.h"
#include "cmsis_os2.h"
#include "usart.h"
#include "Queue.h"
#include "CmdParser.h"
#include <string.h>
#include <stdio.h>

/* ==================== 命令处理函数 ==================== */

static void cmd_help(const char *args, char *reply, int max)
{
    (void)args;
    snprintf(reply, max, "/hello [name]\r\n/add a b\r\n");
}

static void cmd_hello(const char *args, char *reply, int max)
{
    if (args[0])
        snprintf(reply, max, "Hello, %s!\r\n", args);
    else
        snprintf(reply, max, "Hello!\r\n");
}

static void cmd_add(const char *args, char *reply, int max)
{
    int a = 0, b = 0;
    sscanf(args, "%d %d", &a, &b);
    snprintf(reply, max, "%d\r\n", a + b);
}

/* ==================== 命令表 ==================== */

static CmdDef cmds[] = {
    {"/help",  cmd_help},
    {"/hello", cmd_hello},
    {"/add",   cmd_add},
};

/* ==================== UART 任务 ==================== */

void Start_UART_TASK(void *argument)
{
    uint8_t rx;
    char reply[CMD_REPLY_LEN];

    CmdParser_Init();
    CMD_UART1_IT_Start();  /* 启动 UART 中断接收 */

    for (;;) {
        if (osMessageQueueGet(CMD_QueueHandle, &rx, 0, osWaitForever) != osOK)
            continue;

        if (CmdParser_Feed(rx)) {
            char *rsp = CmdParser_Exec(cmds, sizeof(cmds) / sizeof(cmds[0]),
                                       reply, sizeof(reply));
            if (rsp)
                HAL_UART_Transmit(&huart1, (uint8_t *)rsp, strlen(rsp), 100);
        }
    }
}

/*
 * ============ USART 中断配套代码 (添加到 usart.c USER CODE BEGIN 1) ============

#include "cmsis_os2.h"

uint8_t Rx_Data;

void CMD_UART1_IT_Start(void) {
    HAL_UART_Receive_IT(&huart1, &Rx_Data, 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        osMessageQueuePut(CMD_QueueHandle, &Rx_Data, 0, 0);
        HAL_UART_Receive_IT(&huart1, &Rx_Data, 1);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        HAL_UART_Receive_IT(&huart1, &Rx_Data, 1);
    }
}
*/
