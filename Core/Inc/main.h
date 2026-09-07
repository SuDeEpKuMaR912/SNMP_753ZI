/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define Row1_Pin GPIO_PIN_3
#define Row1_GPIO_Port GPIOE
#define Row2_Pin GPIO_PIN_4
#define Row2_GPIO_Port GPIOE
#define Row3_Pin GPIO_PIN_5
#define Row3_GPIO_Port GPIOE
#define Row4_Pin GPIO_PIN_6
#define Row4_GPIO_Port GPIOE
#define Col1_Pin GPIO_PIN_0
#define Col1_GPIO_Port GPIOF
#define Col1_EXTI_IRQn EXTI0_IRQn
#define Col2_Pin GPIO_PIN_1
#define Col2_GPIO_Port GPIOF
#define Col2_EXTI_IRQn EXTI1_IRQn
#define Col3_Pin GPIO_PIN_2
#define Col3_GPIO_Port GPIOF
#define Col3_EXTI_IRQn EXTI2_IRQn
#define Col4_Pin GPIO_PIN_3
#define Col4_GPIO_Port GPIOF
#define Col4_EXTI_IRQn EXTI3_IRQn
#define Col5_Pin GPIO_PIN_4
#define Col5_GPIO_Port GPIOF
#define Col5_EXTI_IRQn EXTI4_IRQn
#define Col6_Pin GPIO_PIN_5
#define Col6_GPIO_Port GPIOF
#define Col6_EXTI_IRQn EXTI9_5_IRQn
#define Row5_Pin GPIO_PIN_10
#define Row5_GPIO_Port GPIOF

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
