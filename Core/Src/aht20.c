#include "aht20.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>
bool AHT20_Init(I2C_HandleTypeDef *hi2c)
{
    uint8_t init_cmd[3] = {0xBE, 0x08, 0x00};

    // 發送初始化命令
    if (HAL_I2C_Master_Transmit(hi2c, AHT20_ADDR, init_cmd, 3, 100) != HAL_OK)
        return false;

    osDelay(50);   // AHT20 需要時間進入工作模式

    return true;
}

bool AHT20_Read(I2C_HandleTypeDef *hi2c, float *temp, float *humi)
{
    uint8_t measure_cmd[3] = {0xAC, 0x33, 0x00};
    uint8_t recv[6]; // AHT20 讀取通常需要 6 個位元組

    // 1. 發送量測指令
    if (HAL_I2C_Master_Transmit(hi2c, AHT20_ADDR, measure_cmd, 3, 100) != HAL_OK)
        return false;

    // 2. 根據規格書，發送指令後需等待至少 80ms
    osDelay(80);

    // 3. 讀取資料
    if (HAL_I2C_Master_Receive(hi2c, AHT20_ADDR, recv, 6, 100) != HAL_OK)
        return false;

    // 4. 解析資料 (保留您原有的邏輯)
    uint32_t raw_humi = ((uint32_t)recv[1] << 12) |
                        ((uint32_t)recv[2] << 4) |
                        ((recv[3] & 0xF0) >> 4);

    uint32_t raw_temp = ((uint32_t)(recv[3] & 0x0F) << 16) |
                        ((uint32_t)recv[4] << 8) |
                        recv[5];

    *humi = ((float)raw_humi * 100.0f) / 1048576.0f;
    *temp = ((float)raw_temp * 200.0f) / 1048576.0f - 50.0f;

    return true;
}
