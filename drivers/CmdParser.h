/*
 * CmdParser — 通用 UART 命令行解析器 (FreeRTOS + HAL)
 * 适用: STM32F4xx 系列
 *
 * 用法:
 *   1. 定义命令表:
 *      void cmd_hello(const char *arg, char *reply, int max) {
 *          snprintf(reply, max, "Hello%s\r\n", arg[0] ? arg : "");
 *      }
 *      CmdDef cmds[] = {
 *          {"/hello", cmd_hello},
 *      };
 *
 *   2. 在 FreeRTOS 任务中:
 *      CmdParser_Init();
 *      CMD_UART1_IT_Start();    // UART 中断接收 (见 usart.c)
 *      char reply[CMD_REPLY_LEN];
 *      uint8_t rx;
 *      for (;;) {
 *          osMessageQueueGet(CMD_QueueHandle, &rx, 0, osWaitForever);
 *          if (CmdParser_Feed(rx)) {
 *              char *rsp = CmdParser_Exec(cmds, count(cmds), reply, sizeof(reply));
 *              if (rsp) HAL_UART_Transmit(&huart1, (uint8_t *)rsp, strlen(rsp), 100);
 *          }
 *      }
 *
 *   格式: /cmd arg1 arg2 ... \n
 */
#ifndef __CMD_PARSER_H
#define __CMD_PARSER_H

#include <stdint.h>

#define CMD_BUF_LEN  128
#define CMD_REPLY_LEN 128

typedef void (*CmdHandler)(const char *args, char *reply, int reply_max);

typedef struct {
    const char *name;       /* 命令名, 如 "/slave" */
    CmdHandler  handler;    /* 回调: args(命令后的参数), reply(回复缓冲区), reply_max */
} CmdDef;

/* 重置解析器状态 */
void CmdParser_Init(void);

/* 喂入一个字节, 返回 1 表示收到完整一行, 可调用 CmdParser_Exec */
int  CmdParser_Feed(uint8_t byte);

/* 执行命令, 返回 reply 指针 (有回复) 或 NULL (无匹配), 不匹配自动填 "ERR\r\n" */
char *CmdParser_Exec(const CmdDef *cmds, int count, char *reply, int reply_max);

#endif
