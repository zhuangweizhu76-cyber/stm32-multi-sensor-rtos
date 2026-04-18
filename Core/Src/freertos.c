/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "cmsis_os.h"
#include "adc.h"
#include "i2c.h"
#include "usart.h"
#include "main.h"
#include "aht20.h"
#include <stdio.h>
#include <stdlib.h>
typedef struct {
  float temp;
  float humi;
  uint32_t mq2;
  uint32_t light;
} SensorData_t;

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
osThreadId defaultTaskHandle;
osThreadId SensorTaskHandle;
osThreadId WifiTaskHandle;
osMessageQId sensorQueueHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);
void StartSensorTask(void const * argument);
void StartWifiTask(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* definition and creation of sensorQueue */
  osMessageQDef(sensorQueue, 4, uint32_t);
  sensorQueueHandle = osMessageCreate(osMessageQ(sensorQueue), NULL);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 128);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* definition and creation of SensorTask */
  osThreadDef(SensorTask, StartSensorTask, osPriorityNormal, 0, 128);
  SensorTaskHandle = osThreadCreate(osThread(SensorTask), NULL);

  /* definition and creation of WifiTask */
  osThreadDef(WifiTask, StartWifiTask, osPriorityNormal, 0, 256);
  WifiTaskHandle = osThreadCreate(osThread(WifiTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
extern ADC_HandleTypeDef hadc1;
extern I2C_HandleTypeDef hi2c1;
extern osMessageQId sensorQueueHandle;
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartSensorTask */
/**
* @brief Function implementing the SensorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSensorTask */
void StartSensorTask(void const * argument)
{
  /* 注意：data 必須是 static，這樣它的地址在任務切換時才不會消失 */
  static SensorData_t data;
  AHT20_Init(&hi2c1);
  float t, h;
  ADC_ChannelConfTypeDef sConfig = {0};
  osDelay(200);
  for (;;)
  {
	  if (AHT20_Read(&hi2c1, &t, &h))
	      {
	        data.temp = t;
	        data.humi = h;
	      }
	      else
	      {
	        printf("[AHT20] read fail\r\n");
	      }
	  /* ---------- MQ2 : ADC_CHANNEL_0 (PA0) ---------- */
	        sConfig.Channel = ADC_CHANNEL_0;
	        sConfig.Rank = 1;
	        sConfig.SamplingTime = ADC_SAMPLETIME_144CYCLES;

	        HAL_ADC_ConfigChannel(&hadc1, &sConfig);
	        HAL_ADC_Start(&hadc1);
	        HAL_ADC_PollForConversion(&hadc1, 20);
	        data.mq2 = HAL_ADC_GetValue(&hadc1);
	        HAL_ADC_Stop(&hadc1);
	  /* ---------- Light : ADC_CHANNEL_1 (PA1) ---------- */
	      sConfig.Channel = ADC_CHANNEL_1;

	      HAL_ADC_ConfigChannel(&hadc1, &sConfig);
	      HAL_ADC_Start(&hadc1);
	      HAL_ADC_PollForConversion(&hadc1, 20);
	      data.light = HAL_ADC_GetValue(&hadc1);
	      HAL_ADC_Stop(&hadc1);

	      /* ---------- Send to Queue ---------- */
	      osMessagePut(sensorQueueHandle, (uint32_t)&data, 0);

	      osDelay(1000);   // 1 Hz
  }


}

/* USER CODE BEGIN Header_StartWifiTask */
/**
* @brief Function implementing the WifiTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartWifiTask */
void StartWifiTask(void const * argument)
{
  for (;;)
  {
    osEvent evt = osMessageGet(sensorQueueHandle, osWaitForever);
    if (evt.status == osEventMessage)
    {
      // 取得地址並轉型
      SensorData_t *r = (SensorData_t *)evt.value.p;



      printf("T=%.2fC H=%.2f%% MQ2=%lu Light=%lu\r\n",r->temp,r->humi,
             r->mq2, r->light);
    }
  }
}


/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
