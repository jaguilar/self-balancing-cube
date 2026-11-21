/**
  ******************************************************************************
  * @file           : mx_adc2.c
  * @brief          : ADC2 Peripheral initialization
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

/* Includes ------------------------------------------------------------------*/
#include "mx_adc2.h"
/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private functions prototype------------------------------------------------*/
/* Exported variables by reference--------------------------------------------*/
static hal_adc_handle_t hADC2;

/******************************************************************************/
/* Exported functions for ADC2 in HAL layer */
/******************************************************************************/
hal_adc_handle_t *mx_adc2_init(void)
{
  if (HAL_ADC_Init(&hADC2, HAL_ADC2) != HAL_OK)
  {
    return NULL;
  }

  HAL_RCC_ADC12_EnableClock();

  if (HAL_RCC_ADCDAC_SetKernelClkSource(HAL_RCC_ADCDAC_CLK_SRC_PSIS) != HAL_OK)
  {
    return NULL;
  }

  hal_adc_config_t adc_config;

  adc_config.resolution          = HAL_ADC_RESOLUTION_12_BIT;
  adc_config.sampling_mode       = HAL_ADC_SAMPLING_MODE_NORMAL;
  HAL_ADC_SetConfig(&hADC2, &adc_config);

  /****************************************************************************/
  /* Configuration of basic features (mandatory)                              */
  /****************************************************************************/

/* ==================== Group Regular ====================*/
  hal_adc_reg_config_t reg_config;
  reg_config.trigger_src        = HAL_ADC_REG_TRIG_SOFTWARE;
  reg_config.sequencer_length   = 1;
  reg_config.sequencer_discont  = HAL_ADC_REG_SEQ_DISCONT_DISABLE;
  reg_config.continuous         = HAL_ADC_REG_CONV_SINGLE;
  reg_config.overrun            = HAL_ADC_REG_OVR_DATA_OVERWRITTEN;
  HAL_ADC_REG_SetConfig(&hADC2, &reg_config);

/* Section commented due to information missing in CubeMX2 interface
  adc_channel_config.group           = ...; // Channel has not been added in sequencer
  adc_channel_config.sequencer_rank  = ...; // Channel has not been added in sequencer
  adc_channel_config.sampling_time   = ...; // Channel has not been added in sequencer
  adc_channel_config.input_mode      = ...; // Channel has not been added in sequencer
  HAL_ADC_SetConfigChannel(&hADC2, HAL_ADC_CHANNEL_x , &adc_channel_config);
*/

/* ==================== Group Injected ====================*/
  hal_adc_inj_config_t inj_config;
  inj_config.trigger_src        = HAL_ADC_INJ_TRIG_SOFTWARE;
  inj_config.sequencer_length   = 1;
  inj_config.sequencer_discont  = HAL_ADC_INJ_SEQ_DISCONT_DISABLE;
  HAL_ADC_INJ_SetConfig(&hADC2, &inj_config);

/* Section commented due to information missing in CubeMX2 interface
  adc_channel_config.group           = ...; // Channel has not been added in sequencer
  adc_channel_config.sequencer_rank  = ...; // Channel has not been added in sequencer
  adc_channel_config.sampling_time   = ...; // Channel has not been added in sequencer
  adc_channel_config.input_mode      = ...; // Channel has not been added in sequencer
  HAL_ADC_SetConfigChannel(&hADC2, HAL_ADC_CHANNEL_x , &adc_channel_config);
*/

  /****************************************************************************/
  /* Configuration of additional features (optional)                          */
  /****************************************************************************/

  /* ### ADC2 GPIO Configuration ########################### */
  /* GPIO Clocks activation */
  HAL_RCC_GPIOA_EnableClock();

  HAL_RCC_GPIOB_EnableClock();

  hal_gpio_config_t  gpio_config;

  /**
    [GPIO Pin] ------> [Signal Name] ------> [Labels]

       PA2     ------>   ADC2_IN2   ------>  DRV_SENSE_B
    **/
  gpio_config.mode        = HAL_GPIO_MODE_ANALOG;
  gpio_config.pull        = HAL_GPIO_PULL_NO;
  HAL_GPIO_Init(DRV_SENSE_B_PORT, DRV_SENSE_B_PIN, &gpio_config);

  /**
    [GPIO Pin] ------> [Signal Name] ------> [Labels]

       PB0     ------>   ADC2_IN6   ------>  VM_SENSE
    **/
  gpio_config.mode        = HAL_GPIO_MODE_ANALOG;
  gpio_config.pull        = HAL_GPIO_PULL_NO;
  HAL_GPIO_Init(VM_SENSE_PORT, VM_SENSE_PIN, &gpio_config);

  return &hADC2;
}

void mx_adc2_deinit(void)
{
  (void)HAL_ADC_DeInit(&hADC2);

  /* De-initialize all GPIOA pins associated with ADC2 */
  HAL_GPIO_DeInit(DRV_SENSE_B_PORT, DRV_SENSE_B_PIN);

  /* De-initialize all GPIOB pins associated with ADC2 */
  HAL_GPIO_DeInit(VM_SENSE_PORT, VM_SENSE_PIN);
}

hal_adc_handle_t *mx_adc2_gethandle(void)
{
  return &hADC2;
}
