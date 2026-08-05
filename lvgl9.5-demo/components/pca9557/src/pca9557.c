/**
 * @file pca9557.c
 * @brief PCA9557 I2C IO 扩展芯片驱动实现
 *
 * SPDX-FileCopyrightText: 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pca9557.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "pca9557";

// I2C 写入寄存器
static esp_err_t write_reg(pca9557_dev_t *dev, uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    return i2c_master_transmit(dev->dev_handle, buf, 2, -1);
}

esp_err_t pca9557_init(i2c_master_bus_handle_t bus_handle, uint8_t addr,
                       pca9557_dev_t **out_dev) {
    esp_err_t ret;

    pca9557_dev_t *dev = calloc(1, sizeof(pca9557_dev_t));
    if (!dev) {
        return ESP_ERR_NO_MEM;
    }

    // 添加 I2C 设备
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400 * 1000,
    };

    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev->dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 添加 I2C 设备失败 (addr=0x%02X): %s", addr, esp_err_to_name(ret));
        free(dev);
        return ret;
    }

    // 初始值: LCD_CS=1(空闲, 高电平), PA_EN=1(使能), 其余=0
    dev->output_shadow = 0x03;

    // 配置方向: bit0,1,2 为输出, 其余保持输入
    // 配置寄存器: 0=输出, 1=输入 → 输出位写 0
    ret = write_reg(dev, PCA9557_REG_CONFIG, 0xF8);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 配置方向失败: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(dev->dev_handle);
        free(dev);
        return ret;
    }

    // 设置初始输出值
    ret = write_reg(dev, PCA9557_REG_OUTPUT, dev->output_shadow);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 设置初始输出失败: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(dev->dev_handle);
        free(dev);
        return ret;
    }

    ESP_LOGI(TAG, "✅ PCA9557 初始化完成 (addr=0x%02X)", addr);
    *out_dev = dev;
    return ESP_OK;
}

esp_err_t pca9557_set_output(pca9557_dev_t *dev, uint8_t bit, uint8_t level) {
    if (!dev || bit > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    // 用影子寄存器更新
    if (level) {
        dev->output_shadow |= (1u << bit);
    } else {
        dev->output_shadow &= ~(1u << bit);
    }

    return write_reg(dev, PCA9557_REG_OUTPUT, dev->output_shadow);
}

esp_err_t pca9557_deinit(pca9557_dev_t *dev) {
    if (!dev) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = i2c_master_bus_rm_device(dev->dev_handle);
    free(dev);
    return ret;
}
