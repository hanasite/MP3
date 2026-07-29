/*
 * IPS 彩色LCD通用驱动 (ST7789 主控, SPI 接口, HAL 库)
 * 适用: STM32F4xx 系列 (F407/F411/F429 等)
 *
 * 注意: 主控芯片为 ST7789，支持各种分辨率 (初始化时传入)
 *       原始驱动来源: 逐飞科技 TC264 开源库 zf_device_ips200
 *       移植适配: STM32 HAL 库，结构体初始化风格对齐 CubeMX
 *
 * GPIO/SPI 初始化由 CubeMX 完成，此处仅做句柄/引脚绑定
 */
#include "ips_screen.h"
#include "ips_screen_font.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ==================== 全局变量 ==================== */
       uint16_t ips_screen_width   = 240;
       uint16_t ips_screen_height  = 320;
static uint16_t ips_screen_width_phys;              /* 物理分辨率宽 */
static uint16_t ips_screen_height_phys;             /* 物理分辨率高 */
static uint16_t ips_pencolor       = IPS_COLOR_RED;
static uint16_t ips_bgcolor        = IPS_COLOR_WHITE;
static IPS_Screen_DirTypeDef  ips_display_dir  = IPS_DIR_PORTAIT;
static IPS_Screen_FontTypeDef ips_display_font = IPS_FONT_8X16;

/* ==================== 接口模式 ==================== */
static uint8_t ips_mode;            /* 0=SW_SPI, 1=HW_SPI */
static SPI_HandleTypeDef *ips_hspi;

/* 软件 SPI 引脚 */
static GPIO_TypeDef *sw_scl_port; static uint16_t sw_scl_pin;
static GPIO_TypeDef *sw_sda_port; static uint16_t sw_sda_pin;

/* 公共引脚 */
static GPIO_TypeDef *ips_cs_port;  static uint16_t ips_cs_pin;
static GPIO_TypeDef *ips_dc_port;  static uint16_t ips_dc_pin;
static GPIO_TypeDef *ips_rst_port; static uint16_t ips_rst_pin;
static GPIO_TypeDef *ips_bl_port;  static uint16_t ips_bl_pin;

/* ==================== 引脚操作宏 ==================== */
#define CS_SET()   HAL_GPIO_WritePin(ips_cs_port,  ips_cs_pin,  GPIO_PIN_SET)
#define CS_CLR()   HAL_GPIO_WritePin(ips_cs_port,  ips_cs_pin,  GPIO_PIN_RESET)
#define DC_SET()   HAL_GPIO_WritePin(ips_dc_port,  ips_dc_pin,  GPIO_PIN_SET)
#define DC_CLR()   HAL_GPIO_WritePin(ips_dc_port,  ips_dc_pin,  GPIO_PIN_RESET)
#define RST_SET()  HAL_GPIO_WritePin(ips_rst_port, ips_rst_pin, GPIO_PIN_SET)
#define RST_CLR()  HAL_GPIO_WritePin(ips_rst_port, ips_rst_pin, GPIO_PIN_RESET)
#define BL_SET()   HAL_GPIO_WritePin(ips_bl_port,  ips_bl_pin,  GPIO_PIN_SET)
#define BL_CLR()   HAL_GPIO_WritePin(ips_bl_port,  ips_bl_pin,  GPIO_PIN_RESET)
#define SCL_SET()  HAL_GPIO_WritePin(sw_scl_port, sw_scl_pin, GPIO_PIN_SET)
#define SCL_CLR()  HAL_GPIO_WritePin(sw_scl_port, sw_scl_pin, GPIO_PIN_RESET)
#define SDA_SET()  HAL_GPIO_WritePin(sw_sda_port, sw_sda_pin, GPIO_PIN_SET)
#define SDA_CLR()  HAL_GPIO_WritePin(sw_sda_port, sw_sda_pin, GPIO_PIN_RESET)

/* ==================== 软件 SPI ==================== */
static void SW_SPI_Delay(void)
{
    for (volatile uint8_t i = 0; i < 4; i++) { __NOP(); }
}

static void SW_SPI_WriteByte(uint8_t dat)
{
    for (uint8_t i = 0; i < 8; i++) {
        if (dat & 0x80) SDA_SET(); else SDA_CLR();
        dat <<= 1;
        SW_SPI_Delay();
        SCL_SET(); SW_SPI_Delay();
        SCL_CLR();
    }
}

static void SW_SPI_WriteBytes(const uint8_t *dat, uint32_t len)
{
    while (len--) SW_SPI_WriteByte(*dat++);
}

static void SW_SPI_Write16(uint16_t dat)
{
    SW_SPI_WriteByte(dat >> 8);
    SW_SPI_WriteByte(dat & 0xFF);
}

/* ==================== 硬件 SPI ==================== */
static void HW_SPI_WriteByte(uint8_t dat)
{
    HAL_SPI_Transmit(ips_hspi, &dat, 1, 10);
}

static void HW_SPI_WriteBytes(const uint8_t *dat, uint32_t len)
{
    HAL_SPI_Transmit(ips_hspi, (uint8_t *)dat, len, 10);
}

static void HW_SPI_Write16(uint16_t dat)
{
    uint8_t buf[2] = {dat >> 8, dat & 0xFF};
    HAL_SPI_Transmit(ips_hspi, buf, 2, 10);
}

/* ==================== 统一写入接口 ==================== */
static void IPS_WriteByte(uint8_t dat)
{
    if (ips_mode) HW_SPI_WriteByte(dat);
    else          SW_SPI_WriteByte(dat);
}

static void IPS_WriteBytes(const uint8_t *dat, uint32_t len)
{
    if (ips_mode) HW_SPI_WriteBytes(dat, len);
    else          SW_SPI_WriteBytes(dat, len);
}

static void IPS_Write16(uint16_t dat)
{
    if (ips_mode) HW_SPI_Write16(dat);
    else          SW_SPI_Write16(dat);
}
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     向 ST7789 写入命令
// 参数说明     cmd             命令字节
// 返回参数     void
// 使用示例     IPS_WriteCommand(0x2A);
// 备注信息     内部调用，自动控制 DC=0
//-------------------------------------------------------------------------------------------------------------------
static void IPS_WriteCommand(uint8_t cmd)
{
    DC_CLR(); CS_CLR();
    IPS_WriteByte(cmd);
    CS_SET(); DC_SET();
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     向 ST7789 写入 8bit 数据
// 参数说明     dat             数据字节
// 返回参数     void
// 使用示例     IPS_WriteData8(0x05);
// 备注信息     内部调用，自动控制 DC=1
//-------------------------------------------------------------------------------------------------------------------
static void IPS_WriteData8(uint8_t dat)
{
    CS_CLR();
    IPS_WriteByte(dat);
    CS_SET();
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     向 ST7789 写入 16bit 数据 (RGB565像素，MSB先发)
// 参数说明     dat             16bit 数据
// 返回参数     void
// 使用示例     IPS_WriteData16(0xF800);
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
static void IPS_WriteData16(uint16_t dat)
{
    CS_CLR();
    IPS_Write16(dat);
    CS_SET();
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     设置 ST7789 显示窗口区域
// 参数说明     x1              起始 X 坐标
// 参数说明     y1              起始 Y 坐标
// 参数说明     x2              结束 X 坐标
// 参数说明     y2              结束 Y 坐标
// 返回参数     void
// 使用示例     IPS_SetRegion(0, 0, 239, 319);
// 备注信息     内部调用，越界坐标自动忽略
//-------------------------------------------------------------------------------------------------------------------
static void IPS_SetRegion(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    if (x1 >= ips_screen_width)  return;
    if (y1 >= ips_screen_height) return;
    if (x2 >= ips_screen_width)  return;
    if (y2 >= ips_screen_height) return;

    IPS_WriteCommand(0x2A);
    IPS_WriteData16(x1);
    IPS_WriteData16(x2);

    IPS_WriteCommand(0x2B);
    IPS_WriteData16(y1);
    IPS_WriteData16(y2);

    IPS_WriteCommand(0x2C);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     批量写入 uint16 像素数组 (转为字节流分块发送)
// 参数说明     *dat            像素数据指针
// 参数说明     len             像素个数
// 返回参数     void
// 使用示例     IPS_WriteData16Array(line_buf, 240);
// 备注信息     内部调用，分块传输控制栈内存
//-------------------------------------------------------------------------------------------------------------------
static void IPS_WriteData16Array(const uint16_t *dat, uint32_t len)
{
    static uint8_t byte_buf[640];
    CS_CLR();
    while (len > 0) {
        uint32_t chunk = (len > 320) ? 320 : len;
        for (uint32_t i = 0; i < chunk; i++) {
            byte_buf[i * 2]     = dat[i] >> 8;
            byte_buf[i * 2 + 1] = dat[i] & 0xFF;
        }
        IPS_WriteBytes(byte_buf, chunk * 2);
        dat += chunk;
        len -= chunk;
    }
    CS_SET();
}
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕清屏
// 参数说明     void
// 返回参数     void
// 使用示例     IPS_Screen_Clear();
// 备注信息     将整个屏幕填充为当前背景色
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_Clear(void)
{
    static uint16_t line_buf[320];
    for (uint16_t i = 0; i < ips_screen_width; i++)
        line_buf[i] = ips_bgcolor;

    IPS_SetRegion(0, 0, ips_screen_width - 1, ips_screen_height - 1);
    for (uint16_t j = 0; j < ips_screen_height; j++)
        IPS_WriteData16Array(line_buf, ips_screen_width);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕全屏填充指定颜色
// 参数说明     color           颜色 格式 RGB565，可使用 IPS_COLOR_xxx 宏
// 返回参数     void
// 使用示例     IPS_Screen_Full(IPS_COLOR_BLACK);
// 备注信息     将整个屏幕填充为指定颜色
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_Full(uint16_t color)
{
    static uint16_t line_buf[320];
    for (uint16_t i = 0; i < ips_screen_width; i++)
        line_buf[i] = color;

    IPS_SetRegion(0, 0, ips_screen_width - 1, ips_screen_height - 1);
    for (uint16_t j = 0; j < ips_screen_height; j++)
        IPS_WriteData16Array(line_buf, ips_screen_width);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     设置 IPS 屏幕显示方向
// 参数说明     dir             显示方向 参见 IPS_Screen_DirTypeDef 枚举
// 返回参数     void
// 使用示例     IPS_Screen_SetDir(IPS_DIR_CROSSWISE);
// 备注信息     旋转后 ips_screen_width / ips_screen_height 自动交换
//              此函数仅在 init 时调用有效 (写入 ST7789 MADCTL 寄存器)
//              运行时调用只切换软件坐标系，不会重新初始化屏幕
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_SetDir(IPS_Screen_DirTypeDef dir)
{
    ips_display_dir = dir;
    switch (dir) {
        case IPS_DIR_PORTAIT:
        case IPS_DIR_PORTAIT_180:
            ips_screen_width  = ips_screen_width_phys;
            ips_screen_height = ips_screen_height_phys;
            break;
        case IPS_DIR_CROSSWISE:
        case IPS_DIR_CROSSWISE_180:
            ips_screen_width  = ips_screen_height_phys;
            ips_screen_height = ips_screen_width_phys;
            break;
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     设置 IPS 屏幕显示字体
// 参数说明     font            字体选择 参见 IPS_Screen_FontTypeDef 枚举
// 返回参数     void
// 使用示例     IPS_Screen_SetFont(IPS_FONT_6X8);
// 备注信息     设置后即时生效，影响后续所有字符显示
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_SetFont(IPS_Screen_FontTypeDef font)
{
    ips_display_font = font;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     设置 IPS 屏幕画笔颜色和背景颜色
// 参数说明     pen             画笔颜色 (前景色) 格式 RGB565
// 参数说明     bgcolor         背景颜色          格式 RGB565
// 返回参数     void
// 使用示例     IPS_Screen_SetColor(IPS_COLOR_RED, IPS_COLOR_WHITE);
// 备注信息     设置后即时生效，影响后续所有绘图和字符显示
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_SetColor(uint16_t pen, uint16_t bgcolor)
{
    ips_pencolor = pen;
    ips_bgcolor  = bgcolor;
}
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕画点
// 参数说明     x               坐标 X 轴，取值范围 [0, ips_screen_width - 1]
// 参数说明     y               坐标 Y 轴，取值范围 [0, ips_screen_height - 1]
// 参数说明     color           颜色 格式 RGB565
// 返回参数     void
// 使用示例     IPS_Screen_DrawPoint(120, 160, IPS_COLOR_RED);
// 备注信息     越界坐标自动忽略
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= ips_screen_width)  return;
    if (y >= ips_screen_height) return;
    IPS_SetRegion(x, y, x, y);
    IPS_WriteData16(color);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕画线
// 参数说明     x_start         起始 X 坐标 取值范围 [0, ips_screen_width - 1]
// 参数说明     y_start         起始 Y 坐标 取值范围 [0, ips_screen_height - 1]
// 参数说明     x_end           结束 X 坐标 取值范围 [0, ips_screen_width - 1]
// 参数说明     y_end           结束 Y 坐标 取值范围 [0, ips_screen_height - 1]
// 参数说明     color           颜色 格式 RGB565
// 返回参数     void
// 使用示例     IPS_Screen_DrawLine(0, 0, 100, 100, IPS_COLOR_GREEN);
// 备注信息     使用浮点斜率算法，精度优于 Bresenham 但速度稍慢
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_DrawLine(uint16_t x_start, uint16_t y_start,
                         uint16_t x_end, uint16_t y_end, uint16_t color)
{
    if (x_start >= ips_screen_width)  return;
    if (y_start >= ips_screen_height) return;
    if (x_end   >= ips_screen_width)  return;
    if (y_end   >= ips_screen_height) return;

    int16_t x_dir = (x_start < x_end) ? 1 : -1;
    int16_t y_dir = (y_start < y_end) ? 1 : -1;
    float temp_rate, temp_b;

    if (x_start != x_end) {
        temp_rate = (float)(y_start - y_end) / (float)(x_start - x_end);
        temp_b = (float)y_start - (float)x_start * temp_rate;
    } else {
        while (y_start != y_end) {
            IPS_Screen_DrawPoint(x_start, y_start, color);
            y_start += y_dir;
        }
        IPS_Screen_DrawPoint(x_start, y_start, color);
        return;
    }

    if (fabsf(y_start - y_end) > fabsf(x_start - x_end)) {
        while (y_start != y_end) {
            IPS_Screen_DrawPoint(x_start, y_start, color);
            y_start += y_dir;
            x_start = (int16_t)(((float)y_start - temp_b) / temp_rate);
        }
        IPS_Screen_DrawPoint(x_start, y_start, color);
    } else {
        while (x_start != x_end) {
            IPS_Screen_DrawPoint(x_start, y_start, color);
            x_start += x_dir;
            y_start = (int16_t)((float)x_start * temp_rate + temp_b);
        }
        IPS_Screen_DrawPoint(x_start, y_start, color);
    }
}
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕显示一个 ASCII 字符
// 参数说明     x               坐标 X 轴，取值范围 [0, ips_screen_width - 1]
// 参数说明     y               坐标 Y 轴，取值范围 [0, ips_screen_height - 1]
// 参数说明     dat             需要显示的字符 (ASCII 码 32-126 为可见字符)
// 返回参数     void
// 使用示例     IPS_Screen_ShowChar(0, 0, 'A');
// 备注信息     字符尺寸取决于当前字体设置 (6x8 或 8x16)
//              颜色使用当前画笔/背景色
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_ShowChar(uint16_t x, uint16_t y, char dat)
{
    if (x >= ips_screen_width)  return;
    if (y >= ips_screen_height) return;

    uint8_t i, j;

    switch (ips_display_font) {
        case IPS_FONT_6X8: {
            static uint16_t buf[48];
            IPS_SetRegion(x, y, x + 5, y + 7);
            for (i = 0; i < 6; i++) {
                uint8_t temp_top = ascii_font_6x8[dat - 32][i];
                for (j = 0; j < 8; j++) {
                    buf[i + j * 6] = (temp_top & 0x01) ? ips_pencolor : ips_bgcolor;
                    temp_top >>= 1;
                }
            }
            IPS_WriteData16Array(buf, 48);
        } break;
        case IPS_FONT_8X16: {
            static uint16_t buf[128];
            IPS_SetRegion(x, y, x + 7, y + 15);
            for (i = 0; i < 8; i++) {
                uint8_t temp_top    = ascii_font_8x16[dat - 32][i];
                uint8_t temp_bottom = ascii_font_8x16[dat - 32][i + 8];
                for (j = 0; j < 8; j++) {
                    buf[i + j * 8] = (temp_top & 0x01) ? ips_pencolor : ips_bgcolor;
                    temp_top >>= 1;
                }
                for (j = 0; j < 8; j++) {
                    buf[i + j * 8 + 64] = (temp_bottom & 0x01) ? ips_pencolor : ips_bgcolor;
                    temp_bottom >>= 1;
                }
            }
            IPS_WriteData16Array(buf, 128);
        } break;
        default: break;
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕显示字符串
// 参数说明     x               坐标 X 轴，取值范围 [0, ips_screen_width - 1]
// 参数说明     y               坐标 Y 轴，取值范围 [0, ips_screen_height - 1]
// 参数说明     *dat            需要显示的字符串 (以 '\0' 结尾)
// 返回参数     void
// 使用示例     IPS_Screen_ShowString(0, 0, "Hello World");
// 备注信息     字符间距取决于当前字体 (6x8=6px, 8x16=8px)，不自动换行
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_ShowString(uint16_t x, uint16_t y, const char *dat)
{
    if (x >= ips_screen_width)  return;
    if (y >= ips_screen_height) return;
    if (dat == NULL)            return;

    uint16_t j = 0;
    while (dat[j] != '\0') {
        if (ips_display_font == IPS_FONT_6X8)
            IPS_Screen_ShowChar(x + 6 * j, y, dat[j]);
        else
            IPS_Screen_ShowChar(x + 8 * j, y, dat[j]);
        j++;
    }
}
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕显示 32 位有符号整数 (自动去除高位无效零)
// 参数说明     x               坐标 X 轴，取值范围 [0, ips_screen_width - 1]
// 参数说明     y               坐标 Y 轴，取值范围 [0, ips_screen_height - 1]
// 参数说明     dat             需要显示的变量 数据类型 int32
// 参数说明     num             需要显示的位数 最大 10
// 返回参数     void
// 使用示例     IPS_Screen_ShowInt(0, 0, x, 5);
// 备注信息     显示带符号，正数前无 '+' 号，负数显示 '-'
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_ShowInt(uint16_t x, uint16_t y, int32_t dat, uint8_t num)
{
    if (x >= ips_screen_width)  return;
    if (y >= ips_screen_height) return;
    if (num == 0 || num > 10)   return;

    int32_t dat_temp = dat, offset = 1;
    if (num < 10) {
        for (uint8_t i = 0; i < num; i++) offset *= 10;
        dat_temp %= offset;
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%*ld", num, (long)dat_temp);
    IPS_Screen_ShowString(x, y, buf);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕显示 32 位无符号整数 (自动去除高位无效零)
// 参数说明     x               坐标 X 轴，取值范围 [0, ips_screen_width - 1]
// 参数说明     y               坐标 Y 轴，取值范围 [0, ips_screen_height - 1]
// 参数说明     dat             需要显示的变量 数据类型 uint32
// 参数说明     num             需要显示的位数 最大 10
// 返回参数     void
// 使用示例     IPS_Screen_ShowUint(0, 0, cnt, 3);
// 备注信息     位数不够时高位截断不显示
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_ShowUint(uint16_t x, uint16_t y, uint32_t dat, uint8_t num)
{
    if (x >= ips_screen_width)  return;
    if (y >= ips_screen_height) return;
    if (num == 0 || num > 10)   return;

    uint32_t dat_temp = dat, offset = 1;
    if (num < 10) {
        for (uint8_t i = 0; i < num; i++) offset *= 10;
        dat_temp %= offset;
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%*lu", num, (unsigned long)dat_temp);
    IPS_Screen_ShowString(x, y, buf);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕显示浮点数 (自动去除高位无效零)
// 参数说明     x               坐标 X 轴，取值范围 [0, ips_screen_width - 1]
// 参数说明     y               坐标 Y 轴，取值范围 [0, ips_screen_height - 1]
// 参数说明     dat             需要显示的变量 数据类型 double
// 参数说明     num             整数部分显示位数 最大 8
// 参数说明     pointnum        小数部分显示位数 最大 6
// 返回参数     void
// 使用示例     IPS_Screen_ShowFloat(0, 0, 3.14159, 2, 3);
// 备注信息     当小数位数较多时可能出现浮点精度丢失导致显示误差，这是浮点数特性
//              正数不显示 '+' 号，负数显示 '-'
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_ShowFloat(uint16_t x, uint16_t y, double dat, uint8_t num, uint8_t pointnum)
{
    if (x >= ips_screen_width)  return;
    if (y >= ips_screen_height) return;
    if (num == 0 || num > 8)    return;
    if (pointnum == 0 || pointnum > 6) return;

    double offset = 1.0;
    for (uint8_t i = 0; i < num; i++) offset *= 10.0;
    double dat_temp = dat - ((int32_t)dat / (int32_t)offset) * offset;

    char fmt[10], buf[17];
    snprintf(fmt, sizeof(fmt), "%%%d.%df", num + pointnum + 1, pointnum);
    snprintf(buf, sizeof(buf), fmt, dat_temp);
    IPS_Screen_ShowString(x, y, buf);
}
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕显示二值图像 (每 8 个像素占一个字节)
// 参数说明     x               图像起始 X 坐标 取值范围 [0, ips_screen_width - 1]
// 参数说明     y               图像起始 Y 坐标 取值范围 [0, ips_screen_height - 1]
// 参数说明     *image          图像数据指针 (每 bit 代表一个像素)
// 参数说明     width           图像实际宽度
// 参数说明     height          图像实际高度
// 参数说明     dis_width       图像显示宽度 取值范围 [0, ips_screen_width]
// 参数说明     dis_height      图像显示高度 取值范围 [0, ips_screen_height]
// 返回参数     void
// 使用示例     IPS_Screen_ShowBinaryImage(0, 0, img, 160, 120, 160, 120);
// 备注信息     适用于摄像头未压缩的二值图像 (如 OV7725)
//              图像自动缩放至 dis_width * dis_height 区域
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_ShowBinaryImage(uint16_t x, uint16_t y, const uint8_t *image,
    uint16_t width, uint16_t height, uint16_t dis_width, uint16_t dis_height)
{
    if (x >= ips_screen_width)  return;
    if (y >= ips_screen_height) return;
    if (image == NULL)          return;

    static uint16_t buf[320];
    IPS_SetRegion(x, y, x + dis_width - 1, y + dis_height - 1);

    for (uint16_t j = 0; j < dis_height; j++) {
        const uint8_t *row = image + j * height / dis_height * width / 8;
        for (uint16_t i = 0; i < dis_width; i++) {
            uint32_t idx = i * width / dis_width;
            uint8_t temp = *(row + idx / 8);
            buf[i] = (0x80 & (temp << (idx % 8))) ? IPS_COLOR_WHITE : IPS_COLOR_BLACK;
        }
        IPS_WriteData16Array(buf, dis_width);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕显示 8bit 灰度图像 (支持阈值二值化)
// 参数说明     x               图像起始 X 坐标 取值范围 [0, ips_screen_width - 1]
// 参数说明     y               图像起始 Y 坐标 取值范围 [0, ips_screen_height - 1]
// 参数说明     *image          图像数据指针 (每字节一个像素 0-255 灰度)
// 参数说明     width           图像实际宽度
// 参数说明     height          图像实际高度
// 参数说明     dis_width       图像显示宽度 取值范围 [0, ips_screen_width]
// 参数说明     dis_height      图像显示高度 取值范围 [0, ips_screen_height]
// 参数说明     threshold       阈值 0-显示原始灰度，非零-二值化阈值
// 返回参数     void
// 使用示例     IPS_Screen_ShowGrayImage(0, 0, img, 188, 120, 188, 120, 0);
// 备注信息     适用于摄像头灰度图像 (如 MT9V03X)
//              threshold = 0 显示 RGB565 伪彩色灰度，> 0 显示黑白二值
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_ShowGrayImage(uint16_t x, uint16_t y, const uint8_t *image,
    uint16_t width, uint16_t height, uint16_t dis_width, uint16_t dis_height, uint8_t threshold)
{
    if (x >= ips_screen_width)  return;
    if (y >= ips_screen_height) return;
    if (image == NULL)          return;

    static uint16_t buf[320];
    IPS_SetRegion(x, y, x + dis_width - 1, y + dis_height - 1);

    for (uint16_t j = 0; j < dis_height; j++) {
        const uint8_t *row = image + j * height / dis_height * width;
        for (uint16_t i = 0; i < dis_width; i++) {
            uint8_t temp = *(row + i * width / dis_width);
            if (threshold == 0) {
                uint16_t c  = (0x001F & (temp >> 3)) << 11;
                c |= ((0x003F & (temp >> 2)) << 5);
                c |= (0x001F & (temp >> 3));
                buf[i] = c;
            } else {
                buf[i] = (temp < threshold) ? IPS_COLOR_BLACK : IPS_COLOR_WHITE;
            }
        }
        IPS_WriteData16Array(buf, dis_width);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕显示 RGB565 彩色图像
// 参数说明     x               图像起始 X 坐标 取值范围 [0, ips_screen_width - 1]
// 参数说明     y               图像起始 Y 坐标 取值范围 [0, ips_screen_height - 1]
// 参数说明     *image          图像数据指针 (uint16 数组，每个元素为 RGB565 颜色)
// 参数说明     width           图像实际宽度
// 参数说明     height          图像实际高度
// 参数说明     dis_width       图像显示宽度 取值范围 [0, ips_screen_width]
// 参数说明     dis_height      图像显示高度 取值范围 [0, ips_screen_height]
// 参数说明     color_mode      字节序模式 0-高字节在前 1-低字节在前
// 返回参数     void
// 使用示例     IPS_Screen_ShowRGB565Image(0, 0, img, 160, 120, 160, 120, 1);
// 备注信息     适用于彩色摄像头 (如 SCC8660)
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_ShowRGB565Image(uint16_t x, uint16_t y, const uint16_t *image,
    uint16_t width, uint16_t height, uint16_t dis_width, uint16_t dis_height, uint8_t color_mode)
{
    if (x >= ips_screen_width)  return;
    if (y >= ips_screen_height) return;
    if (image == NULL)          return;

    static uint16_t buf[320];
    IPS_SetRegion(x, y, x + dis_width - 1, y + dis_height - 1);

    for (uint16_t j = 0; j < dis_height; j++) {
        const uint16_t *row = image + j * height / dis_height * width;
        for (uint16_t i = 0; i < dis_width; i++)
            buf[i] = *(row + i * width / dis_width);
        if (color_mode) {
            CS_CLR();
            IPS_WriteBytes((const uint8_t *)buf, dis_width * 2);
            CS_SET();
        } else {
            IPS_WriteData16Array(buf, dis_width);
        }
    }
}
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕显示波形
// 参数说明     x               波形区域起始 X 坐标 取值范围 [0, ips_screen_width - 1]
// 参数说明     y               波形区域起始 Y 坐标 取值范围 [0, ips_screen_height - 1]
// 参数说明     *wave           波形数据指针
// 参数说明     width           波形实际数据宽度
// 参数说明     value_max       波形数据最大值 (用于归一化 Y 轴)
// 参数说明     dis_width       波形显示宽度 取值范围 [0, ips_screen_width]
// 参数说明     dis_value_max   波形显示高度 取值范围 [0, ips_screen_height]
// 返回参数     void
// 使用示例     IPS_Screen_ShowWave(0, 100, adc_buf, 128, 4095, 240, 100);
// 备注信息     先以背景色清空区域，再用画笔颜色绘制波形曲线
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_ShowWave(uint16_t x, uint16_t y, const uint16_t *wave,
    uint16_t width, uint16_t value_max, uint16_t dis_width, uint16_t dis_value_max)
{
    if (x >= ips_screen_width)  return;
    if (y >= ips_screen_height) return;
    if (wave == NULL)           return;

    static uint16_t buf[320];
    IPS_SetRegion(x, y, x + dis_width - 1, y + dis_value_max - 1);
    for (uint16_t i = 0; i < dis_width; i++) buf[i] = ips_bgcolor;
    for (uint16_t j = 0; j < dis_value_max; j++)
        IPS_WriteData16Array(buf, dis_width);

    for (uint16_t i = 0; i < dis_width; i++) {
        uint32_t widx = i * width / dis_width;
        uint32_t vidx = *(wave + widx) * (dis_value_max - 1) / value_max;
        IPS_Screen_DrawPoint((uint16_t)(i + x),
            (uint16_t)((dis_value_max - 1) - vidx + y), ips_pencolor);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕显示中文字符
// 参数说明     x               坐标 X 轴，取值范围 [0, ips_screen_width - 1]
// 参数说明     y               坐标 Y 轴，取值范围 [0, ips_screen_height - 1]
// 参数说明     size            取模时使用的汉字点阵大小 如 16 表示 16x16
// 参数说明     *chinese_buffer 汉字字模数据指针
// 参数说明     number          需要显示的汉字个数
// 参数说明     color           颜色 格式 RGB565
// 返回参数     void
// 使用示例     IPS_Screen_ShowChinese(0, 0, 16, chinese_font[0], 4, IPS_COLOR_RED);
// 备注信息     使用 PCtoLCD2002 取模，阴码、逐列式、顺向
//              背景色使用当前 ips_bgcolor
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_ShowChinese(uint16_t x, uint16_t y, uint8_t size,
    const uint8_t *chinese_buffer, uint8_t number, uint16_t color)
{
    if (x >= ips_screen_width)  return;
    if (y >= ips_screen_height) return;
    if (chinese_buffer == NULL) return;

    uint8_t temp2 = size / 8;
    const uint8_t *p_data;
    IPS_SetRegion(x, y, number * size - 1 + x, y + size - 1);

    for (int i = 0; i < size; i++) {
        uint8_t cnt = number;
        p_data = chinese_buffer + i * temp2;
        while (cnt--) {
            for (int k = 0; k < temp2; k++) {
                for (int j = 8; j > 0; j--) {
                    uint8_t temp = (*p_data >> (j - 1)) & 0x01;
                    IPS_WriteData16(temp ? color : ips_bgcolor);
                }
                p_data++;
            }
            p_data = p_data - temp2 + temp2 * size;
        }
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     ST7789 初始化寄存器序列
// 参数说明     void
// 返回参数     void
// 使用示例     IPS_InitSequence();
// 备注信息     内部调用，包含复位、Gamma、电源、显示方向等一系列寄存器配置
//-------------------------------------------------------------------------------------------------------------------
static void IPS_InitSequence(void)
{
    BL_SET();
    RST_CLR(); HAL_Delay(5);
    RST_SET(); HAL_Delay(120);

    IPS_WriteCommand(0x11);  /* Sleep out */
    HAL_Delay(120);

    IPS_WriteCommand(0x36);  /* MADCTL 显示方向 */
    switch (ips_display_dir) {
        case IPS_DIR_PORTAIT:       IPS_WriteData8(0x00); break;
        case IPS_DIR_PORTAIT_180:   IPS_WriteData8(0xC0); break;
        case IPS_DIR_CROSSWISE:     IPS_WriteData8(0x70); break;
        case IPS_DIR_CROSSWISE_180: IPS_WriteData8(0xA0); break;
    }

    IPS_WriteCommand(0x3A); IPS_WriteData8(0x05);  /* 16-bit RGB565 */

    IPS_WriteCommand(0xB2);  /* Porch */
    IPS_WriteData8(0x0C); IPS_WriteData8(0x0C); IPS_WriteData8(0x00);
    IPS_WriteData8(0x33); IPS_WriteData8(0x33);

    IPS_WriteCommand(0xB7); IPS_WriteData8(0x35);  /* Gate */
    IPS_WriteCommand(0xBB); IPS_WriteData8(0x29);  /* VCOM */
    IPS_WriteCommand(0xC2); IPS_WriteData8(0x01);  /* LCM */
    IPS_WriteCommand(0xC3); IPS_WriteData8(0x19);  /* VDV/VRH */
    IPS_WriteCommand(0xC4); IPS_WriteData8(0x20);  /* VRH */
    IPS_WriteCommand(0xC5); IPS_WriteData8(0x1A);  /* VDV */
    IPS_WriteCommand(0xC6); IPS_WriteData8(0x1F);  /* Frame rate */

    IPS_WriteCommand(0xD0);  /* Power Control 1 */
    IPS_WriteData8(0xA4); IPS_WriteData8(0xA1);

    IPS_WriteCommand(0xE0);  /* Positive Gamma */
    IPS_WriteData8(0xD0); IPS_WriteData8(0x08); IPS_WriteData8(0x0E);
    IPS_WriteData8(0x09); IPS_WriteData8(0x09); IPS_WriteData8(0x05);
    IPS_WriteData8(0x31); IPS_WriteData8(0x33); IPS_WriteData8(0x48);
    IPS_WriteData8(0x17); IPS_WriteData8(0x14); IPS_WriteData8(0x15);
    IPS_WriteData8(0x31); IPS_WriteData8(0x34);

    IPS_WriteCommand(0xE1);  /* Negative Gamma */
    IPS_WriteData8(0xD0); IPS_WriteData8(0x08); IPS_WriteData8(0x0E);
    IPS_WriteData8(0x09); IPS_WriteData8(0x09); IPS_WriteData8(0x15);
    IPS_WriteData8(0x31); IPS_WriteData8(0x33); IPS_WriteData8(0x48);
    IPS_WriteData8(0x17); IPS_WriteData8(0x14); IPS_WriteData8(0x15);
    IPS_WriteData8(0x31); IPS_WriteData8(0x34);

    IPS_WriteCommand(0x21);  /* Inversion On */
    IPS_WriteCommand(0x29);  /* Display On */

    IPS_Screen_Clear();
}
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IPS 屏幕初始化 (统一入口)
// 参数说明     *config         初始化配置结构体指针 参见 IPS_Screen_InitTypeDef
// 返回参数     void
// 使用示例     IPS_Screen_InitTypeDef cfg = {0};
//              cfg.hspi = &hspi1; cfg.width = 240; cfg.height = 320;
//              cfg.cs_port = GPIOB;  cfg.cs_pin = GPIO_PIN_0;
//              cfg.dc_port = GPIOB;  cfg.dc_pin = GPIO_PIN_1;
//              cfg.rst_port = GPIOA; cfg.rst_pin = GPIO_PIN_4;
//              cfg.bl_port = GPIOA;  cfg.bl_pin = GPIO_PIN_5;
//              IPS_Screen_Init(&cfg);
// 备注信息     通过 config->hspi 是否为 NULL 自动识别硬件/软件 SPI 模式
//              分辨率在初始化时传入，SetDir 时横竖屏分辨率自动交换
//              GPIO/SPI 的底层初始化由 CubeMX 完成，此处仅做句柄/引脚绑定
//-------------------------------------------------------------------------------------------------------------------
void IPS_Screen_Init(const IPS_Screen_InitTypeDef *config)
{
    ips_screen_width_phys  = config->width  > 0 ? config->width  : 240;
    ips_screen_height_phys = config->height > 0 ? config->height : 320;

    ips_cs_port  = config->cs_port;   ips_cs_pin  = config->cs_pin;
    ips_dc_port  = config->dc_port;   ips_dc_pin  = config->dc_pin;
    ips_rst_port = config->rst_port;  ips_rst_pin = config->rst_pin;
    ips_bl_port  = config->bl_port;   ips_bl_pin  = config->bl_pin;

    if (config->hspi != NULL) {
        ips_mode = 1;
        ips_hspi = config->hspi;
    } else {
        ips_mode    = 0;
        sw_scl_port = config->scl_port;  sw_scl_pin = config->scl_pin;
        sw_sda_port = config->sda_port;  sw_sda_pin = config->sda_pin;
        SCL_SET(); SDA_SET();
    }

    CS_SET(); DC_SET(); BL_SET(); RST_SET();

    ips_display_dir = IPS_DIR_PORTAIT;
    ips_screen_width  = ips_screen_width_phys;
    ips_screen_height = ips_screen_height_phys;

    IPS_InitSequence();
}