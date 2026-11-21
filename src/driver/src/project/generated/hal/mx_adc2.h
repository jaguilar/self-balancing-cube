
/**
  ******************************************************************************
  * @file           : mx_adc2.h
  * @brief          : Header for mx_adc2.c file.
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
#ifndef MX_ADC2_H
#define MX_ADC2_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/
#include "stm32_hal.h"

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

/* Aliases of channels labels */
#define DRV_SENSE_B_CHANNEL HAL_ADC_CHANNEL_2
#define VM_SENSE_CHANNEL HAL_ADC_CHANNEL_6

/** Primary aliases for ADC2_IN2 pin */
#define DRV_SENSE_B_PORT                      HAL_GPIOA
#define DRV_SENSE_B_PIN                       HAL_GPIO_PIN_2

/** Primary aliases for ADC2_IN6 pin */
#define VM_SENSE_PORT                         HAL_GPIOB
#define VM_SENSE_PIN                          HAL_GPIO_PIN_0

/* Exported macros -----------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
/******************************************************************************/
/* Exported functions for ADC in HAL layer */
/******************************************************************************/
/**
  * @brief mx_adc2 init function
  * This function configures the hardware resources used in this example
  * @retval pointer to handle or NULL in case of failure
  */
hal_adc_handle_t *mx_adc2_init(void);

/**
  * @brief  De-initialize adc2 instance and return it.
  */
void mx_adc2_deinit(void);

/**
  * @brief  Get the ADC2 object.
  * @retval Pointer on the ADC2 Handle
  */
hal_adc_handle_t *mx_adc2_gethandle(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MX_ADC2_H */
