/**
  * @defgroup BASIC_STDIO_UART_INTERFACE UART Interface (polling transmit and receive)
  * @ingroup BASIC_STDIO_INTERFACE_API
  * @brief UART variant: pObj is a pointer to a configured UART handle; blocking transmit and receive.
  * @{
  */
/**
  ******************************************************************************
  * @file    _basic_stdio_itfio_uart.c
  * @brief   This file contains the io interface functions based on UART.
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

/* Includes ------------------------------------------------------------------*/
#include "basic_stdio_itf_io.h"

/* Private defines -----------------------------------------------------------*/
/* Private macros ------------------------------------------------------------*/
/* Private typedef -----------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/

/*
 * Static (private) variable holding the UART handle to use. Initialized to NULL.
 */
static hal_uart_handle_t *itf_huart = NULL;

/* Private function prototypes -----------------------------------------------*/
/* Functions Definition ------------------------------------------------------*/

/**
  * @brief Initialize UART interface.
  * @param pobj Pointer to a configured UART HAL handle.
  */
void interface_io_Init(void *pobj)
{
  /* Store object pointer for later sends */
  itf_huart = (hal_uart_handle_t *)pobj;
}

/**
  * @brief Send a buffer over UART (polling mode).
  * @param Ptr  Data buffer pointer.
  * @param Size Number of bytes to transmit.
  * @return Bytes transmitted.
  */
uint32_t interface_io_Send(const uint8_t *Ptr, uint16_t Size)
{
  if (itf_huart != NULL)
  {
    if (HAL_UART_Transmit(itf_huart, Ptr, Size, 0xFFFFU) == HAL_OK)
    {
      return Size;
    }
  }
  return 0;
}

/**
  * @brief Receive a buffer over UART (polling mode).
  * @param Ptr  Data buffer pointer.
  * @param Size Number of bytes to receive.
  * @return Bytes received.
  */
uint32_t interface_io_Receive(uint8_t *Ptr, uint16_t Size)
{
  if ((itf_huart != NULL) && (Size != 0U))
  {
    if (HAL_UART_Receive(itf_huart, Ptr, Size, HAL_MAX_DELAY) == HAL_OK)
    {
      return Size;
    }
  }
  return 0;
}

/**
  * @}
  */
