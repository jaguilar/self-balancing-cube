/**
  ******************************************************************************
  * @file           : mx_hal_def.h
  * @brief          : Exporting peripherals initialization
  *                   Include entry for the target folder to the application.
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
#ifndef MX_HAL_DEF_H
#define MX_HAL_DEF_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/
#include "mx_def.h"
#include "stm32_hal.h"
#include "mx_adc1.h"
#include "mx_adc2.h"
#include "mx_comp1.h"
#include "mx_cordic.h"
#include "mx_cortex_debug.h"
#include "mx_cortex_mpu.h"
#include "mx_cortex_nvic.h"
#include "mx_fdcan1.h"
#include "mx_gpio_default.h"
#include "mx_icache.h"
#include "mx_rcc.h"
#include "mx_spi2.h"
#include "mx_tim1.h"
#include "mx_tim6.h"
#include "mx_tim5.h"
#include "mx_tim2.h"
#include "mx_usart1.h"

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

/* Exported macros -----------------------------------------------------------*/

/* ########### Aliases to initialization functions ########### */

  /* *************************************************************
    ADC1: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_adc1_init
    ************************************************************* */

  /* *************************************************************
    ADC2: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_adc2_init
    ************************************************************* */

  /* *************************************************************
    COMP1: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_comp1_init
    ************************************************************* */

  /* *************************************************************
    CORDIC: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_cordic_init
    ************************************************************* */

  /* *************************************************************
    Cortex_DEBUG: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_cortex_debug_init
    ************************************************************* */

  /* *************************************************************
    Cortex_MPU: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_cortex_mpu_init
    ************************************************************* */

  /* *************************************************************
    Cortex_NVIC: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_cortex_nvic_init
    ************************************************************* */

  /* *************************************************************
    FDCAN1: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_fdcan1_init
    ************************************************************* */

  /* *************************************************************
    gpio_default: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_gpio_default_init
    ************************************************************* */

  /* *************************************************************
    ICACHE: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_icache_init
    ************************************************************* */

  /* *************************************************************
    RCC: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_rcc_init
    ************************************************************* */

  /* *************************************************************
    SPI2: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_spi2_init
    ************************************************************* */

  /* ***********************************************************
      TIM1: aliases for initialization functions
    *********************************************************** */

/**
  * @brief  Initialize the mx_tim1 with HAL layer
  *         Name of the User label:
  *                   motor1
  * @retval hal_tim_handle_t Pointer on the handle on the TIM1 instance
  */
#define motor1_init mx_tim1_init

  /* *************************************************************
    TIM6: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_tim6_init
    ************************************************************* */

  /* *************************************************************
    TIM5: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_tim5_init
    ************************************************************* */

  /* *************************************************************
    TIM2: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_tim2_init
    ************************************************************* */

  /* *************************************************************
    USART1: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_usart1_uart_init
    ************************************************************* */

/* ########################################################### */

/* ########### Aliases to De-Initialization functions ########### */

  /* *************************************************************
    ADC1: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_adc1_deinit
    ************************************************************* */

  /* *************************************************************
    ADC2: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_adc2_deinit
    ************************************************************* */

  /* *************************************************************
    COMP1: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_comp1_deinit
    ************************************************************* */

  /* *************************************************************
    CORDIC: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_cordic_deinit
    ************************************************************* */

  /* *************************************************************
    Cortex_DEBUG: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_cortex_debug_deinit
    ************************************************************* */

  /* *************************************************************
    Cortex_MPU: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_cortex_mpu_deinit
    ************************************************************* */

  /* *************************************************************
    Cortex_NVIC: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_cortex_nvic_deinit
    ************************************************************* */

  /* *************************************************************
    FDCAN1: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_fdcan1_deinit
    ************************************************************* */

  /* *************************************************************
    gpio_default: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_gpio_default_deinit
    ************************************************************* */

  /* *************************************************************
    ICACHE: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_icache_deinit
    ************************************************************* */

  /* *************************************************************
    SPI2: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_spi2_deinit
    ************************************************************* */

  /* ***********************************************************
      TIM1: aliases for De-Initialization functions
    *********************************************************** */

/**
  * @brief  De-Initialize the mx_tim1 with HAL layer
  *         Name of the User label:
  *                   motor1
  */
#define motor1_deinit mx_tim1_deinit

  /* *************************************************************
    TIM6: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_tim6_deinit
    ************************************************************* */

  /* *************************************************************
    TIM5: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_tim5_deinit
    ************************************************************* */

  /* *************************************************************
    TIM2: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_tim2_deinit
    ************************************************************* */

  /* *************************************************************
    USART1: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_usart1_uart_deinit
    ************************************************************* */

/* ########################################################### */

/* ########### Aliases to get HAL handle functions ########### */

  /* *************************************************************
    ADC1: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_adc1_gethandle
    ************************************************************* */

  /* *************************************************************
    ADC2: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_adc2_gethandle
    ************************************************************* */

  /* *************************************************************
    COMP1: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_comp1_gethandle
    ************************************************************* */

  /* *************************************************************
    CORDIC: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_cordic_gethandle
    ************************************************************* */

  /* *************************************************************
    FDCAN1: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_fdcan1_gethandle
    ************************************************************* */

  /* *************************************************************
    ICACHE: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_icache_gethandle
    ************************************************************* */

  /* *************************************************************
    SPI2: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_spi2_gethandle
    ************************************************************* */

  /* ***********************************************************
      TIM1: aliases for get handle functions
    *********************************************************** *//**
  * @brief  Get the HAL handle for TIM1
  *         Name of the User label:
  *                   motor1
  * @retval hal_tim_handle_t Pointer on the handle on the TIM1 instance
  */
#define motor1_gethandle mx_tim1_gethandle

  /* *************************************************************
    TIM6: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_tim6_gethandle
    ************************************************************* */

  /* *************************************************************
    TIM5: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_tim5_gethandle
    ************************************************************* */

  /* *************************************************************
    TIM2: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_tim2_gethandle
    ************************************************************* */

  /* *************************************************************
    USART1: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for mx_usart1_uart_gethandle
    ************************************************************* */

/* ########################################################### */

/* ########### Aliases to get IRQ Handlers functions ########### */
  /* *************************************************************
    TIM5: No software label has been defined for this peripheral instance
      in the STM32CubeMX2 configuration panel.
      As a result, no aliases are generated for TIM5_IRQHandler and TIM5_IRQn
    ************************************************************* */

  /* ########################################################### */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MX_HAL_DEF_H */
