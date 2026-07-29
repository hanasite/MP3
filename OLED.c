/*
 * OLED I2C 驱动 (软件/硬件I2C可选, HAL库)
 * 适用: STM32F4xx 系列 (F407/F411/F429 等)
 * FreeRTOS 兼容 — 硬件I2C超时设10ms, 软件I2C无阻塞
 *
 * 用法:
 *   // 硬件 I2C — CubeMX 配好 I2C1，这里只绑定句柄
 *   OLED_Init_HW(&hi2c1);
 *
 *   // 软件 I2C — CubeMX 配好 PB8/PB9 为开漏输出，这里只绑定引脚
 *   OLED_Init_SW(GPIOB, GPIO_PIN_8, GPIOB, GPIO_PIN_9);
 *
 *   GPIO/I2C 初始化由 CubeMX 完成，此处仅做引脚/句柄绑定
 */
#include "OLED.h"
#define OLED_FONT_DEFINE
#include "OLED_Font.h"

#define OLED_ADDR  0x78

static uint8_t oled_mode;          /* 0=SW, 1=HW */

/* 软件I2C 引脚 */
static GPIO_TypeDef *sw_scl_port;
static uint16_t      sw_scl_pin;
static GPIO_TypeDef *sw_sda_port;
static uint16_t      sw_sda_pin;

/* 硬件I2C 句柄 */
static I2C_HandleTypeDef *oled_hi2c;

/* DWT 微秒延时 — 独立硬件周期计数器，不影响 FreeRTOS SysTick */
static void OLED_DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000UL);
    while ((DWT->CYCCNT - start) < cycles);
}

/* ==================== 软件 I2C ==================== */

#define SCL_H()  HAL_GPIO_WritePin(sw_scl_port, sw_scl_pin, GPIO_PIN_SET)
#define SCL_L()  HAL_GPIO_WritePin(sw_scl_port, sw_scl_pin, GPIO_PIN_RESET)
#define SDA_H()  HAL_GPIO_WritePin(sw_sda_port, sw_sda_pin, GPIO_PIN_SET)
#define SDA_L()  HAL_GPIO_WritePin(sw_sda_port, sw_sda_pin, GPIO_PIN_RESET)

static void SW_I2C_Start(void)
{
    SDA_H(); OLED_DelayUs(5);
    SCL_H(); OLED_DelayUs(5);
    SDA_L(); OLED_DelayUs(5);
    SCL_L(); OLED_DelayUs(5);
}

static void SW_I2C_Stop(void)
{
    SDA_L(); OLED_DelayUs(5);
    SCL_H(); OLED_DelayUs(5);
    SDA_H(); OLED_DelayUs(5);
}

static void SW_I2C_SendByte(uint8_t Byte)
{
    for (uint8_t i = 0; i < 8; i++) {
        (Byte & (0x80 >> i)) ? SDA_H() : SDA_L();
        OLED_DelayUs(5);
        SCL_H(); OLED_DelayUs(5);
        SCL_L(); OLED_DelayUs(5);
    }
    SCL_H(); OLED_DelayUs(5);
    SCL_L(); OLED_DelayUs(5);
}

static void SW_WriteCmd(uint8_t cmd)
{
    SW_I2C_Start();
    SW_I2C_SendByte(0x78);
    SW_I2C_SendByte(0x00);
    SW_I2C_SendByte(cmd);
    SW_I2C_Stop();
}

static void SW_WriteData(uint8_t data)
{
    SW_I2C_Start();
    SW_I2C_SendByte(0x78);
    SW_I2C_SendByte(0x40);
    SW_I2C_SendByte(data);
    SW_I2C_Stop();
}

/* ==================== 硬件 I2C ==================== */

static void HW_WriteCmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    HAL_I2C_Master_Transmit(oled_hi2c, OLED_ADDR, buf, 2, 10);
}

static void HW_WriteData(uint8_t data)
{
    uint8_t buf[2] = {0x40, data};
    HAL_I2C_Master_Transmit(oled_hi2c, OLED_ADDR, buf, 2, 10);
}

/* ==================== 统一接口 ==================== */

static void OLED_WriteCommand(uint8_t cmd)
{
    if (oled_mode) HW_WriteCmd(cmd);
    else           SW_WriteCmd(cmd);
}

static void OLED_WriteData(uint8_t data)
{
    if (oled_mode) HW_WriteData(data);
    else           SW_WriteData(data);
}

/* ==================== OLED 公用函数 ==================== */

void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand(0xB0 | Y);
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));
    OLED_WriteCommand(0x00 | (X & 0x0F));
}

void OLED_Clear(void)
{
    for (uint8_t j = 0; j < 8; j++) {
        OLED_SetCursor(j, 0);
        for (uint8_t i = 0; i < 128; i++)
            OLED_WriteData(0x00);
    }
}

void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);
    for (uint8_t i = 0; i < 8; i++)
        OLED_WriteData(OLED_F8x16[Char - ' '][i]);
    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);
    for (uint8_t i = 0; i < 8; i++)
        OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);
}

void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
    for (uint8_t i = 0; String[i] != '\0'; i++)
        OLED_ShowChar(Line, Column + i, String[i]);
}

static uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
    uint32_t r = 1;
    while (Y--) r *= X;
    return r;
}

void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    for (uint8_t i = 0; i < Length; i++)
        OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
}

void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
    uint32_t n;
    if (Number >= 0) { OLED_ShowChar(Line, Column, '+'); n = Number; }
    else             { OLED_ShowChar(Line, Column, '-'); n = -Number; }
    for (uint8_t i = 0; i < Length; i++)
        OLED_ShowChar(Line, Column + i + 1, n / OLED_Pow(10, Length - i - 1) % 10 + '0');
}

void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    for (uint8_t i = 0; i < Length; i++) {
        uint8_t v = Number / OLED_Pow(16, Length - i - 1) % 16;
        OLED_ShowChar(Line, Column + i, (v < 10) ? (v + '0') : (v - 10 + 'A'));
    }
}

void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    for (uint8_t i = 0; i < Length; i++)
        OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
}

/* ==================== 初始化 ==================== */

/* 引脚/外设初始化由 CubeMX 完成，这里仅绑定 */
static void OLED_Init_Sequence(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    OLED_WriteCommand(0xAE);
    OLED_WriteCommand(0xD5); OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xA8); OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xD3); OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0xA1);
    OLED_WriteCommand(0xC8);
    OLED_WriteCommand(0xDA); OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81); OLED_WriteCommand(0xCF);
    OLED_WriteCommand(0xD9); OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDB); OLED_WriteCommand(0x30);
    OLED_WriteCommand(0xA4);
    OLED_WriteCommand(0xA6);
    OLED_WriteCommand(0x8D); OLED_WriteCommand(0x14);
    OLED_WriteCommand(0xAF);
    OLED_Clear();
}

void OLED_Init_SW(GPIO_TypeDef *scl_port, uint16_t scl_pin,
                  GPIO_TypeDef *sda_port, uint16_t sda_pin)
{
    oled_mode   = 0;
    sw_scl_port = scl_port;
    sw_scl_pin  = scl_pin;
    sw_sda_port = sda_port;
    sw_sda_pin  = sda_pin;
    SCL_H();
    SDA_H();
    OLED_Init_Sequence();
}

void OLED_Init_HW(I2C_HandleTypeDef *hi2c)
{
    oled_mode = 1;
    oled_hi2c = hi2c;
    OLED_Init_Sequence();
}
