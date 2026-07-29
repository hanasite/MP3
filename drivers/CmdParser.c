/*
 * CmdParser — 通用 UART 命令行解析器 (FreeRTOS + HAL)
 * 适用: STM32F4xx 系列
 *
 * 用法:
 *   CmdDef cmds[] = {
 *       {"/hello", cmd_hello},
 *   };
 *   CmdParser_Init();
 *   // 任务循环中: CmdParser_Feed(rx_byte) → CmdParser_Exec(cmds, N, reply, size)
 */
#include "CmdParser.h"
#include <string.h>
#include <stdio.h>

static uint8_t cmd_buf[CMD_BUF_LEN];
static uint8_t cmd_len;

void CmdParser_Init(void)
{
    cmd_len = 0;
}

int CmdParser_Feed(uint8_t byte)
{
    if (byte == '\r' || byte == '\n') {
        if (cmd_len > 0) {
            cmd_buf[cmd_len] = '\0';
            cmd_len = 0;
            return 1;
        }
        return 0;
    }
    if (cmd_len < sizeof(cmd_buf) - 1) {
        cmd_buf[cmd_len++] = byte;
    } else {
        cmd_len = 0;  /* 溢出丢弃 */
    }
    return 0;
}

char *CmdParser_Exec(const CmdDef *cmds, int count, char *reply, int reply_max)
{
    char *cmd = (char *)cmd_buf;

    /* 跳过前导空白 */
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == '\0') return NULL;

    /* 匹配命令表 */
    for (int i = 0; i < count; i++) {
        int len = strlen(cmds[i].name);
        if (strncmp(cmd, cmds[i].name, len) == 0) {
            const char *args = cmd + len;
            while (*args == ' ') args++;  /* 跳过命令名与参数之间的空白 */
            cmds[i].handler(args, reply, reply_max);
            return reply;
        }
    }

    /* 未匹配 — 自动返回错误 */
    snprintf(reply, reply_max, "ERR\r\n");
    return reply;
}
