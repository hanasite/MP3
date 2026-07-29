/*
 * MPU6050 6轴传感器驱动 (I2C 接口, HAL 库)
 * 适用: STM32F4xx 系列 (F407/F411/F429 等)
 *
 * 原始驱动来源: 逐飞科技 TC264 开源库 zf_device_mpu6050
 * 移植适配: STM32 HAL 库
 *
 * 用法:
 *   // 硬件 I2C
 *   MPU6050_Init_HW(&hi2c1);
 *
 *   // 软件 I2C
 *   MPU6050_Init_SW(GPIOB, GPIO_PIN_6, GPIOB, GPIO_PIN_7);
 *
 *   定时读取:
 *   MPU6050_GetAcc();
 *   MPU6050_GetGyro();
 *   float acc_x_g = MPU6050_AccTransition(mpu6050_acc_x);
 */
#include "mpu6050.h"

/* ==================== 全局变量 ==================== */
int16_t mpu6050_gyro_x = 0, mpu6050_gyro_y = 0, mpu6050_gyro_z = 0;
int16_t mpu6050_acc_x  = 0, mpu6050_acc_y  = 0, mpu6050_acc_z  = 0;

/* ==================== 接口模式 ==================== */
static uint8_t mpu_mode;            /* 0=SW_I2C, 1=HW_I2C */

/* 硬件 I2C */
static I2C_HandleTypeDef *mpu_hi2c;

/* 软件 I2C 引脚 */
static GPIO_TypeDef *sw_scl_port; static uint16_t sw_scl_pin;
static GPIO_TypeDef *sw_sda_port; static uint16_t sw_sda_pin;

/* 超时计数 */
#define MPU6050_TIMEOUT 0x00FF

/* ==================== 引脚操作 ==================== */
#define SCL_H()  HAL_GPIO_WritePin(sw_scl_port, sw_scl_pin, GPIO_PIN_SET)
#define SCL_L()  HAL_GPIO_WritePin(sw_scl_port, sw_scl_pin, GPIO_PIN_RESET)
#define SDA_H()  HAL_GPIO_WritePin(sw_sda_port, sw_sda_pin, GPIO_PIN_SET)
#define SDA_L()  HAL_GPIO_WritePin(sw_sda_port, sw_sda_pin, GPIO_PIN_RESET)
#define SDA_IN() (HAL_GPIO_ReadPin(sw_sda_port, sw_sda_pin))

/* ==================== 软件 I2C 延时 ==================== */
static void SW_I2C_Delay(void)
{
    for (volatile uint8_t i = 0; i < 30; i++) { __NOP(); }
}

/* ==================== 软件 I2C 协议 ==================== */
static void SW_I2C_Start(void)
{
    SDA_H(); SW_I2C_Delay();
    SCL_H(); SW_I2C_Delay();
    SDA_L(); SW_I2C_Delay();
    SCL_L(); SW_I2C_Delay();
}

static void SW_I2C_Stop(void)
{
    SDA_L(); SW_I2C_Delay();
    SCL_H(); SW_I2C_Delay();
    SDA_H(); SW_I2C_Delay();
}

static uint8_t SW_I2C_WaitAck(void)
{
    uint8_t ack;
    SDA_H(); SW_I2C_Delay();
    SCL_H(); SW_I2C_Delay();
    ack = SDA_IN();
    SCL_L(); SW_I2C_Delay();
    return ack;
}

static void SW_I2C_SendAck(void)
{
    SDA_L(); SW_I2C_Delay();
    SCL_H(); SW_I2C_Delay();
    SCL_L(); SW_I2C_Delay();
    SDA_H();
}

static void SW_I2C_SendNack(void)
{
    SDA_H(); SW_I2C_Delay();
    SCL_H(); SW_I2C_Delay();
    SCL_L(); SW_I2C_Delay();
}

static void SW_I2C_SendByte(uint8_t dat)
{
    for (uint8_t i = 0; i < 8; i++) {
        if (dat & 0x80) SDA_H(); else SDA_L();
        dat <<= 1;
        SW_I2C_Delay();
        SCL_H(); SW_I2C_Delay();
        SCL_L(); SW_I2C_Delay();
    }
    SW_I2C_WaitAck();
}

static uint8_t SW_I2C_ReadByte(uint8_t ack)
{
    uint8_t dat = 0;
    SDA_H();
    for (uint8_t i = 0; i < 8; i++) {
        dat <<= 1;
        SCL_H(); SW_I2C_Delay();
        if (SDA_IN()) dat |= 0x01;
        SCL_L(); SW_I2C_Delay();
    }
    if (ack) SW_I2C_SendAck(); else SW_I2C_SendNack();
    return dat;
}

/* ==================== 软件 I2C 寄存器操作 ==================== */
static void SW_I2C_WriteRegister(uint8_t reg, uint8_t data)
{
    SW_I2C_Start();
    SW_I2C_SendByte(MPU6050_DEV_ADDR << 1);
    SW_I2C_SendByte(reg);
    SW_I2C_SendByte(data);
    SW_I2C_Stop();
}

static uint8_t SW_I2C_ReadRegister(uint8_t reg)
{
    uint8_t dat;
    SW_I2C_Start();
    SW_I2C_SendByte(MPU6050_DEV_ADDR << 1);
    SW_I2C_SendByte(reg);
    SW_I2C_Start();
    SW_I2C_SendByte((MPU6050_DEV_ADDR << 1) | 0x01);
    dat = SW_I2C_ReadByte(0);
    SW_I2C_Stop();
    return dat;
}

static void SW_I2C_ReadRegisters(uint8_t reg, uint8_t *data, uint8_t len)
{
    SW_I2C_Start();
    SW_I2C_SendByte(MPU6050_DEV_ADDR << 1);
    SW_I2C_SendByte(reg);
    SW_I2C_Start();
    SW_I2C_SendByte((MPU6050_DEV_ADDR << 1) | 0x01);
    for (uint8_t i = 0; i < len; i++) {
        data[i] = SW_I2C_ReadByte((i < len - 1) ? 1 : 0);
    }
    SW_I2C_Stop();
}

/* ==================== 硬件 I2C 寄存器操作 ==================== */
static void HW_I2C_WriteRegister(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    HAL_I2C_Master_Transmit(mpu_hi2c, MPU6050_DEV_ADDR << 1, buf, 2, 10);
}

static uint8_t HW_I2C_ReadRegister(uint8_t reg)
{
    uint8_t dat;
    HAL_I2C_Master_Transmit(mpu_hi2c, MPU6050_DEV_ADDR << 1, &reg, 1, 10);
    HAL_I2C_Master_Receive(mpu_hi2c, MPU6050_DEV_ADDR << 1, &dat, 1, 10);
    return dat;
}

static void HW_I2C_ReadRegisters(uint8_t reg, uint8_t *data, uint8_t len)
    HAL_I2C_Master_Transmit(mpu_hi2c, MPU6050_DEV_ADDR << 1, &reg, 1, 10);
    HAL_I2C_Master_Receive(mpu_hi2c, MPU6050_DEV_ADDR << 1, data, len, 10);
}

/* ==================== 统一寄存器操作接口 ==================== */
static void MPU_WriteReg(uint8_t reg, uint8_t data)
{
    if (mpu_mode) HW_I2C_WriteRegister(reg, data);
    else          SW_I2C_WriteRegister(reg, data);
}

static uint8_t MPU_ReadReg(uint8_t reg)
{
    if (mpu_mode) return HW_I2C_ReadRegister(reg);
    else          return SW_I2C_ReadRegister(reg);
}

static void MPU_ReadRegs(uint8_t reg, uint8_t *data, uint8_t len)
{
    if (mpu_mode) HW_I2C_ReadRegisters(reg, data, len);
    else          SW_I2C_ReadRegisters(reg, data, len);
}
/* ==================== 自检 ==================== */
static uint8_t MPU6050_SelfCheck(void)
{
    uint8_t dat = 0;
    uint16_t timeout = 0;

    MPU_WriteReg(MPU6050_PWR_MGMT_1, 0x00);    /* 唤醒 */
    MPU_WriteReg(MPU6050_SMPLRT_DIV, 0x07);    /* 125Hz 采样 */

    while (dat != 0x07) {
        if (timeout++ > MPU6050_TIMEOUT)
            return 1;                           /* 自检失败 */
        HAL_Delay(10);
    }
    return 0;
}

/* ==================== 读取加速度 ==================== */
/**
 * @brief       读取加速度计数据 (X/Y/Z 三轴)
 *              数据存入全局变量 mpu6050_acc_x / _y / _z
 * @param       void
 * @return      void
 * @since       v1.0
 * @sample      MPU6050_GetAcc();
 *              float ax = MPU6050_AccTransition(mpu6050_acc_x);
 * @note        定时调用以获取最新加速度值，建议 ≥10ms 间隔
 */
void MPU6050_GetAcc(void)
{
    uint8_t dat[6];
    MPU_ReadRegs(MPU6050_ACCEL_XOUT_H, dat, 6);
    mpu6050_acc_x = (int16_t)(((uint16_t)dat[0] << 8) | dat[1]);
    mpu6050_acc_y = (int16_t)(((uint16_t)dat[2] << 8) | dat[3]);
    mpu6050_acc_z = (int16_t)(((uint16_t)dat[4] << 8) | dat[5]);
}

/* ==================== 读取陀螺仪 ==================== */
/**
 * @brief       读取陀螺仪数据 (X/Y/Z 三轴)
 *              数据存入全局变量 mpu6050_gyro_x / _y / _z
 * @param       void
 * @return      void
 * @since       v1.0
 * @sample      MPU6050_GetGyro();
 *              float gz = MPU6050_GyroTransition(mpu6050_gyro_z);
 * @note        定时调用以获取最新角速度值，建议 ≥10ms 间隔
 */
void MPU6050_GetGyro(void)
{
    MPU_ReadRegs(MPU6050_GYRO_XOUT_H, dat, 6);
    mpu6050_gyro_x = (int16_t)(((uint16_t)dat[0] << 8) | dat[1]);
    mpu6050_gyro_y = (int16_t)(((uint16_t)dat[2] << 8) | dat[3]);
    mpu6050_gyro_z = (int16_t)(((uint16_t)dat[4] << 8) | dat[5]);
}

/* ==================== 加速度原始值 -> g ==================== */
/**
 * @brief       加速度计原始值转换为重力加速度 (g)
 * @param       acc_value       从 MPU6050_GetAcc() 读取的原始值
 * @return      float           转换后的加速度值，单位 g
 * @since       v1.0
 * @sample      float acc_x_g = MPU6050_AccTransition(mpu6050_acc_x);
 * @note        量程通过 MPU6050_ACC_SAMPLE 宏配置，默认为 ±8g
 */
float MPU6050_AccTransition(int16_t acc_value)
{
    switch (MPU6050_ACC_SAMPLE) {
        case 0x00: return (float)acc_value / 16384.0f;  /* +-2g  */
        case 0x08: return (float)acc_value / 8192.0f;   /* +-4g  */
        case 0x10: return (float)acc_value / 4096.0f;   /* +-8g  */
        case 0x18: return (float)acc_value / 2048.0f;   /* +-16g */
        default:   return (float)acc_value / 4096.0f;
    }

/* ==================== 陀螺仪原始值 -> dps ==================== */
/**
 * @brief       陀螺仪原始值转换为角速度 (dps)
 * @param       gyro_value      从 MPU6050_GetGyro() 读取的原始值
 * @return      float           转换后的角速度值，单位 dps (度/秒)
 * @since       v1.0
 * @sample      float gyro_z_dps = MPU6050_GyroTransition(mpu6050_gyro_z);
 * @note        量程通过 MPU6050_GYR_SAMPLE 宏配置，默认为 ±2000dps
 */
float MPU6050_GyroTransition(int16_t gyro_value)
{
    switch (MPU6050_GYR_SAMPLE) {
        case 0x00: return (float)gyro_value / 131.0f;   /* +-250dps  */
        case 0x08: return (float)gyro_value / 65.5f;    /* +-500dps  */
        case 0x10: return (float)gyro_value / 32.8f;    /* +-1000dps */
        case 0x18: return (float)gyro_value / 16.4f;    /* +-2000dps */
        default:   return (float)gyro_value / 16.4f;
    }
}

/* ==================== 硬件 I2C 初始化 ==================== */
/**
 * @brief       硬件 I2C 初始化 MPU6050
 *              需 CubeMX 预先配置好 I2C 外设
 * @param       hi2c            HAL I2C 句柄指针，如 &hi2c1
 * @return      uint8_t         0=成功, 1=自检失败(设备未连接或通信异常)
 * @since       v1.0
 * @sample      if (MPU6050_Init_HW(&hi2c1)) {
 *                  printf("MPU6050 自检失败!\r\n");
 *                  while(1);
 *              }
 * @note        I2C 频率建议 100kHz 或 400kHz
 */
uint8_t MPU6050_Init_HW(I2C_HandleTypeDef *hi2c)
{
    mpu_mode = 1;
    mpu_hi2c = hi2c;
    HAL_Delay(100);

    if (MPU6050_SelfCheck())
        return 1;

    MPU_WriteReg(MPU6050_PWR_MGMT_1, 0x00);             /* 唤醒 */
    MPU_WriteReg(MPU6050_SMPLRT_DIV, 0x07);             /* 125Hz */
    MPU_WriteReg(MPU6050_CONFIG, 0x04);                 /* 低通滤波器 */
    MPU_WriteReg(MPU6050_GYRO_CONFIG, MPU6050_GYR_SAMPLE);
    MPU_WriteReg(MPU6050_ACCEL_CONFIG, MPU6050_ACC_SAMPLE);
    MPU_WriteReg(MPU6050_USER_CONTROL, 0x00);
    MPU_WriteReg(MPU6050_INT_PIN_CFG, 0x02);
}

/* ==================== 软件 I2C 初始化 ==================== */
/**
 * @brief       软件 I2C 初始化 MPU6050
 *              需 CubeMX 预先将 SCL/SDA 引脚配置为开漏输出
 * @param       scl_port        SCL 时钟线 GPIO 端口，如 GPIOB
 * @param       scl_pin         SCL 时钟线 GPIO 引脚，如 GPIO_PIN_6
 * @param       sda_port        SDA 数据线 GPIO 端口，如 GPIOB
 * @param       sda_pin         SDA 数据线 GPIO 引脚，如 GPIO_PIN_7
 * @return      uint8_t         0=成功, 1=自检失败(设备未连接或通信异常)
 * @since       v1.0
 * @sample      if (MPU6050_Init_SW(GPIOB, GPIO_PIN_6, GPIOB, GPIO_PIN_7)) {
 *                  printf("MPU6050 自检失败!\r\n");
 *                  while(1);
 *              }
 * @note        软件 I2C 速度较慢，高实时性场景建议使用硬件 I2C
 */
uint8_t MPU6050_Init_SW(GPIO_TypeDef *scl_port, uint16_t scl_pin,
                         GPIO_TypeDef *sda_port, uint16_t sda_pin)
{
    mpu_mode    = 0;
    sw_scl_port = scl_port;
    sw_scl_pin  = scl_pin;
    sw_sda_port = sda_port;
    sw_sda_pin  = sda_pin;

    SCL_H();
    SDA_H();
    HAL_Delay(100);

    if (MPU6050_SelfCheck())
        return 1;

    MPU_WriteReg(MPU6050_PWR_MGMT_1, 0x00);
    MPU_WriteReg(MPU6050_SMPLRT_DIV, 0x07);
    MPU_WriteReg(MPU6050_CONFIG, 0x04);
    MPU_WriteReg(MPU6050_GYRO_CONFIG, MPU6050_GYR_SAMPLE);
    MPU_WriteReg(MPU6050_ACCEL_CONFIG, MPU6050_ACC_SAMPLE);
    MPU_WriteReg(MPU6050_USER_CONTROL, 0x00);
    MPU_WriteReg(MPU6050_INT_PIN_CFG, 0x02);
    return 0;
}
