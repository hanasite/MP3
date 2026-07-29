# MP3 Player

STM32F407 MP3 播放器项目，持续施工中。

## 驱动模块 (`drivers/`)

| 模块 | 说明 |
|---|---|
| `ips_screen.h/c` + `font.h/c` | ST7789 彩色 LCD 驱动，SPI 接口，结构体初始化 |
| `mpu6050.h/c` | 6 轴加速度计+陀螺仪，I2C 接口 |
| `OLED.h/c` + `Font.h` | SSD1306 OLED 驱动 |
| `Servo.h/c` | PWM 舵机控制 |
| `CmdParser.h/c` | 串口命令行解析器 |

## 工具

| 脚本 | 用途 |
|---|---|
| `jpg_to_rgb565.ps1` | JPG/PNG → RGB565 C 数组 |
| `rgb565_to_c.ps1` | RGB565 bin → C 数组 |

## 致谢

部分驱动移植自 [逐飞科技 TC264 开源库](https://github.com/seekfree/TC264_Library)（GPL 3.0）。