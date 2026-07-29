# MP3 Player

> 大一暑假 MP3 播放器学习项目 — STM32F407 + FatFS + FreeRTOS + LVGL

## 硬件

- MCU: STM32F407VET6
- 屏幕: ST7789 240×240 IPS LCD (SPI)
- 存储: MicroSD (SDIO)
- 传感器: MPU6050 (I2C)
- 音频: VS1053 / PCM5102

## 目录结构

```
├── drivers/          # 外设驱动模块
│   ├── ips_screen.*       ST7789 彩色 LCD
│   ├── ips_screen_font.*  ASCII 字库
│   ├── mpu6050.*          6 轴传感器
│   ├── OLED.*             SSD1306 OLED
│   ├── Servo.*            PWM 舵机
│   └── CmdParser.*        串口命令行
├── Core/             # CubeMX 生成代码
├── FATFS/            # FatFS 文件系统
└── Middlewares/      # FreeRTOS, LVGL
```

## 模块特点

- **IPS 屏幕**: 结构体初始化，HW/SW SPI 自动识别，反相可配，旋转支持
- **MPU6050**: HW/SW I2C 双模式，逐飞风格注释
- **中文字库**: 外部传入字模，不占 Flash

## 工具

| 脚本 | 路径 | 用途 |
|---|---|---|
| jpg_to_rgb565.ps1 | drivers/ | 图片 → RGB565 C 数组 |
| rgb565_to_c.ps1 | drivers/ | bin → C 数组 |

## 致谢

部分驱动移植自 [逐飞科技 TC264 开源库](https://github.com/seekfree/TC264_Library)（GPL 3.0）。