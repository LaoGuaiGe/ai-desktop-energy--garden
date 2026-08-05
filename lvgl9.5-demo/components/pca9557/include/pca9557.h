/**
 * @file pca9557.h
 * @brief PCA9557 I2C IO 扩展芯片驱动
 * @note 用于控制 LCD_CS、PA_EN、DVP_PWDN 等信号
 *
 * SPDX-FileCopyrightText: 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// PCA9557 寄存器地址
#define PCA9557_REG_INPUT       0x00
#define PCA9557_REG_OUTPUT      0x01
#define PCA9557_REG_POLARITY    0x02
#define PCA9557_REG_CONFIG      0x03

// PCA9557 IO 引脚定义
#define PCA9557_IO_LCD_CS       0   // LCD 片选 (低电平有效)
#define PCA9557_IO_PA_EN        1   // 功放使能
#define PCA9557_IO_DVP_PWDN     2   // 摄像头掉电控制

/**
 * @brief PCA9557 设备句柄
 */
typedef struct {
    i2c_master_dev_handle_t dev_handle;
    uint8_t output_shadow;  // 输出寄存器影子值
} pca9557_dev_t;

/**
 * @brief 初始化 PCA95557
 *
 * @param bus_handle I2C 主总线句柄
 * @param addr PCA9557 的 I2C 地址 (7位地址, 如 0x19)
 * @param[out] out_dev 输出设备句柄
 * @return esp_err_t 成功/失败
 */
esp_err_t pca9557_init(i2c_master_bus_handle_t bus_handle, uint8_t addr,
                       pca9557_dev_t **out_dev);

/**
 * @brief 设置指定 IO 的输出电平
 *
 * @param dev 设备句柄
 * @param bit IO 引脚号 (0-7)
 * @param level 0=低电平, 1=高电平
 * @return esp_err_t 成功/失败
 */
esp_err_t pca9557_set_output(pca9557_dev_t *dev, uint8_t bit, uint8_t level);

/**
 * @brief 释放 PCA9557 设备
 *
 * @param dev 设备句柄
 * @return esp_err_t 成功/失败
 */
esp_err_t pca9557_deinit(pca9557_dev_t *dev);

#ifdef __cplusplus
}
#endif
