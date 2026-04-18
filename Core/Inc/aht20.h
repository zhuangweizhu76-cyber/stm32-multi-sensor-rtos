#ifndef __AHT20_H__
#define __AHT20_H__

#include "stm32f4xx_hal.h"
#include <stdbool.h>

#define AHT20_ADDR 0x38 << 1   // 7-bit address shifted for HAL

bool AHT20_Init(I2C_HandleTypeDef *hi2c);
bool AHT20_Read(I2C_HandleTypeDef *hi2c, float *temp, float *humi);

#endif
