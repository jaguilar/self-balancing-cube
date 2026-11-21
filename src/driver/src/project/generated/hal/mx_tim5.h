/**
  ******************************************************************************
  * @file           : mx_tim5.h
  * @brief          : Header for mx_tim5.c file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the mx_stm32c5xx_hal_drivers_license.md file
  * in the same directory as the generated code.
  * If no mx_stm32c5xx_hal_drivers_license.md file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef MX_TIM5_H
#define MX_TIM5_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/
#include "stm32_hal.h"

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

/** Primary aliases for TIM5_CH1 pin */
#define ENC_A_PORT                            HAL_GPIOA
#define ENC_A_PIN                             HAL_GPIO_PIN_0

/** Secondary aliases for TIM5_CH1 pin */
#define ENCODER_A_PORT                        HAL_GPIOA
#define ENCODER_A_PIN                         HAL_GPIO_PIN_0

/** Primary aliases for TIM5_CH2 pin */
#define ENC_B_PORT                            HAL_GPIOB
#define ENC_B_PIN                             HAL_GPIO_PIN_5

/** Secondary aliases for TIM5_CH2 pin */
#define ENCODER_B_PORT                        HAL_GPIOB
#define ENCODER_B_PIN                         HAL_GPIO_PIN_5

/** Primary aliases for TIM5_ETR pin */
#define ENC_I_PORT                            HAL_GPIOA
#define ENC_I_PIN                             HAL_GPIO_PIN_4

/** Secondary aliases for TIM5_ETR pin */
#define ENCODER_I_PORT                        HAL_GPIOA
#define ENCODER_I_PIN                         HAL_GPIO_PIN_4

/* Exported macros -----------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
/******************************************************************************/
/* Exported functions for TIM in HAL layer */
/******************************************************************************/
/**
  * @brief  mx_tim5 init function.
  *         This function configures the hardware resources used in this example.
  * @retval Pointer to handle
  * @retval NULL in case of failure
  */
hal_tim_handle_t *mx_tim5_init(void);

/**
  * @brief  De-initialize mx_tim5 instance and return it.
  */
void mx_tim5_deinit(void);

/**
  * @brief  Get the mx_tim5 object.
  * @return Pointer on the mx_tim5 Handle
  */
hal_tim_handle_t *mx_tim5_gethandle(void);

/******************************************************************************/
/*          TIM5 global interrupt is managed directly in user code.           */
/******************************************************************************/
/* void TIM5_IRQHandler(void); */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MX_TIM5_H */
