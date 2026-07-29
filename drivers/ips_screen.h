/*
 * ===========================================================================
 *  IPS 彩色LCD通用驱动 — ST7789 主控 / SPI 接口 / STM32 HAL 库
 * ===========================================================================
 *
 *  适用: STM32F4xx (F407/F411/F429 等)
 *  支持分辨率: 240x240, 240x320, 135x240 等 (初始化时传入)
 *
 *  原始驱动: 逐飞科技 TC264 开源库 zf_device_ips200
 *  移植适配: STM32 HAL 库，结构体初始化风格对齐 CubeMX
 *
 * ---------------------------------------------------------------------------
 *  CubeMX 配置
 * ---------------------------------------------------------------------------
 *
 *  [硬件 SPI 模式]
 *    - SPIx: Mode=Full-Duplex Master, NSS=Software, DataSize=8bit,
 *            CPOL=Low, CPHA=1Edge, BaudRate 建议 ≤40MHz
 *    - CS/DC/RST/BL: 全部设为 GPIO_Output, Push-Pull, 初始 High
 *
 *  [软件 SPI 模式]
 *    - SCL/SDA/CS/DC/RST/BL: 全部设为 GPIO_Output, Push-Pull, 初始 High
 *
 * ---------------------------------------------------------------------------
 *  快速上手 — 硬件 SPI
 * ---------------------------------------------------------------------------
 *
 *   // 1. 定义配置结构体，零初始化
 *   IPS_Screen_InitTypeDef ips_cfg = {0};
 *
 *   // 2. 填入参数
 *   ips_cfg.hspi   = &hspi1;           // HAL SPI 句柄
 *   ips_cfg.width  = 240;              // 物理分辨率宽
 *   ips_cfg.height = 320;              // 物理分辨率高
 *   ips_cfg.cs_port  = GPIOB; ips_cfg.cs_pin  = GPIO_PIN_0;
 *   ips_cfg.dc_port  = GPIOB; ips_cfg.dc_pin  = GPIO_PIN_1;
 *   ips_cfg.rst_port = GPIOA; ips_cfg.rst_pin = GPIO_PIN_4;
 *   ips_cfg.bl_port  = GPIOA; ips_cfg.bl_pin  = GPIO_PIN_5;
 *   // scl/sda 无需填写 (hspi != NULL 时忽略)
 *
 *   // 3. 初始化
 *   IPS_Screen_Init(&ips_cfg);
 *
 *   // 4. 开搞
 *   IPS_Screen_Clear();
 *   IPS_Screen_ShowString(0, 0, "Hello IPS!");
 *   IPS_Screen_SetColor(IPS_COLOR_RED, IPS_COLOR_BLACK);
 *   IPS_Screen_ShowInt(0, 20, 12345, 5);
 *
 * ---------------------------------------------------------------------------
 *  快速上手 — 软件 SPI
 * ---------------------------------------------------------------------------
 *
 *   IPS_Screen_InitTypeDef ips_cfg = {0};
 *   ips_cfg.hspi   = NULL;            // NULL → 软件 SPI
 *   ips_cfg.width  = 240;
 *   ips_cfg.height = 240;             // 方屏
 *   ips_cfg.cs_port  = GPIOB;  ips_cfg.cs_pin  = GPIO_PIN_12;
 *   ips_cfg.dc_port  = GPIOB;  ips_cfg.dc_pin  = GPIO_PIN_14;
 *   ips_cfg.rst_port = GPIOB;  ips_cfg.rst_pin = GPIO_PIN_2;
 *   ips_cfg.bl_port  = GPIOB;  ips_cfg.bl_pin  = GPIO_PIN_3;
 *   ips_cfg.scl_port = GPIOB;  ips_cfg.scl_pin = GPIO_PIN_13;
 *   ips_cfg.sda_port = GPIOB;  ips_cfg.sda_pin = GPIO_PIN_15;
 *   IPS_Screen_Init(&ips_cfg);
 *
 * ---------------------------------------------------------------------------
 *  常用 API 速览
 * ---------------------------------------------------------------------------
 *
 *   初始化        IPS_Screen_Init(&cfg)
 *   清屏          IPS_Screen_Clear()
 *   全屏填充      IPS_Screen_Full(IPS_COLOR_BLACK)
 *   旋转方向      IPS_Screen_SetDir(IPS_DIR_CROSSWISE)
 *   设置画笔颜色  IPS_Screen_SetColor(IPS_COLOR_RED, IPS_COLOR_WHITE)
 *   设置字体      IPS_Screen_SetFont(IPS_FONT_6X8)
 *   画点          IPS_Screen_DrawPoint(100, 100, IPS_COLOR_GREEN)
 *   画线          IPS_Screen_DrawLine(0, 0, 100, 100, IPS_COLOR_BLUE)
 *   显示字符      IPS_Screen_ShowChar(0, 0, 'A')
 *   显示字符串    IPS_Screen_ShowString(0, 16, "Hello")
 *   显示整数      IPS_Screen_ShowInt(0, 32, -123, 4)
 *   显示浮点数    IPS_Screen_ShowFloat(0, 48, 3.14, 2, 2)
 *   显示中文      IPS_Screen_ShowChinese(0, 0, 16, font_data, 4, IPS_COLOR_RED)
 *   显示二值图像  IPS_Screen_ShowBinaryImage(0,0, img, 320,240, 320,240)
 *   显示波形      IPS_Screen_ShowWave(0,100, data, 128, 4095, 240, 100)
 *
 *   查询屏幕尺寸  ips_screen_width / ips_screen_height (旋转后自动更新)
 *
 * ---------------------------------------------------------------------------
 *  CubeMX 工程添加步骤
 * ---------------------------------------------------------------------------
 *
 *  1. 将 ips_screen.h, ips_screen.c, ips_screen_font.h, ips_screen_font.c
 *     复制到工程的 Core/Inc 和 Core/Src 目录
 *  2. 在 main.c 中 #include "ips_screen.h"
 *  3. 在 MX_SPIx_Init() / MX_GPIO_Init() 之后调用 IPS_Screen_Init()
 */

#ifndef __IPS_SCREEN_H
#define __IPS_SCREEN_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ==================== 结构体初始化 ==================== */
typedef struct {
    SPI_HandleTypeDef   *hspi;          /* HAL SPI 句柄，NULL = 软件 SPI */
    uint16_t             width;         /* 物理分辨率宽，如 240 */
    uint16_t             height;        /* 物理分辨率高，如 320 */

    /* 公共控制引脚 (硬件/软件 SPI 均需) */
    GPIO_TypeDef        *cs_port;       uint16_t cs_pin;
    GPIO_TypeDef        *dc_port;       uint16_t dc_pin;
    GPIO_TypeDef        *rst_port;      uint16_t rst_pin;
    GPIO_TypeDef        *bl_port;       uint16_t bl_pin;

    /* 软件 SPI 引脚 (hspi == NULL 时有效，否则忽略) */
    GPIO_TypeDef        *scl_port;      uint16_t scl_pin;
    GPIO_TypeDef        *sda_port;      uint16_t sda_pin;
} IPS_Screen_InitTypeDef;

/* ==================== RGB565 颜色定义 ==================== */
#define IPS_COLOR_WHITE     (0xFFFF)
#define IPS_COLOR_BLACK     (0x0000)
#define IPS_COLOR_BLUE      (0x001F)
#define IPS_COLOR_PURPLE    (0xF81F)
#define IPS_COLOR_PINK      (0xFE19)
#define IPS_COLOR_RED       (0xF800)
#define IPS_COLOR_MAGENTA   (0xF81F)
#define IPS_COLOR_GREEN     (0x07E0)
#define IPS_COLOR_CYAN      (0x07FF)
#define IPS_COLOR_YELLOW    (0xFFE0)
#define IPS_COLOR_BROWN     (0xBC40)
#define IPS_COLOR_GRAY      (0x8430)

/* ==================== 显示方向枚举 ==================== */
typedef enum {
    IPS_DIR_PORTAIT       = 0,         /* 竖屏 */
    IPS_DIR_PORTAIT_180   = 1,         /* 竖屏 旋转180 */
    IPS_DIR_CROSSWISE     = 2,         /* 横屏 */
    IPS_DIR_CROSSWISE_180 = 3,         /* 横屏 旋转180 */
} IPS_Screen_DirTypeDef;

/* ==================== 字体大小枚举 ==================== */
typedef enum {
    IPS_FONT_6X8    = 0,               /* 6x8 字体 */
    IPS_FONT_8X16   = 1,               /* 8x16 字体 */
} IPS_Screen_FontTypeDef;

/* ==================== 全局变量 ==================== */
extern uint16_t ips_screen_width;
extern uint16_t ips_screen_height;

/* ==================== 初始化 ==================== */
void IPS_Screen_Init(const IPS_Screen_InitTypeDef *config);

/* ==================== 基础绘图 ==================== */
void IPS_Screen_Clear(void);
void IPS_Screen_Full(uint16_t color);
void IPS_Screen_SetDir(IPS_Screen_DirTypeDef dir);
void IPS_Screen_SetFont(IPS_Screen_FontTypeDef font);
void IPS_Screen_SetColor(uint16_t pen, uint16_t bgcolor);
void IPS_Screen_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
void IPS_Screen_DrawLine(uint16_t x_start, uint16_t y_start,
                         uint16_t x_end, uint16_t y_end, uint16_t color);

/* ==================== 字符显示 ==================== */
void IPS_Screen_ShowChar(uint16_t x, uint16_t y, char dat);
void IPS_Screen_ShowString(uint16_t x, uint16_t y, const char *dat);
void IPS_Screen_ShowInt(uint16_t x, uint16_t y, int32_t dat, uint8_t num);
void IPS_Screen_ShowUint(uint16_t x, uint16_t y, uint32_t dat, uint8_t num);
void IPS_Screen_ShowFloat(uint16_t x, uint16_t y, double dat, uint8_t num, uint8_t pointnum);

/* ==================== 图像显示 ==================== */
void IPS_Screen_ShowBinaryImage(uint16_t x, uint16_t y, const uint8_t *image,
    uint16_t width, uint16_t height, uint16_t dis_width, uint16_t dis_height);
void IPS_Screen_ShowGrayImage(uint16_t x, uint16_t y, const uint8_t *image,
    uint16_t width, uint16_t height, uint16_t dis_width, uint16_t dis_height, uint8_t threshold);
void IPS_Screen_ShowRGB565Image(uint16_t x, uint16_t y, const uint16_t *image,
    uint16_t width, uint16_t height, uint16_t dis_width, uint16_t dis_height, uint8_t color_mode);

/* ==================== 波形/中文 ==================== */
void IPS_Screen_ShowWave(uint16_t x, uint16_t y, const uint16_t *wave,
    uint16_t width, uint16_t value_max, uint16_t dis_width, uint16_t dis_value_max);
void IPS_Screen_ShowChinese(uint16_t x, uint16_t y, uint8_t size,
    const uint8_t *chinese_buffer, uint8_t number, uint16_t color);

#endif