/**
  ******************************************************************************
  * @file    basic_stdio_itfio_itm.c
  * @brief   This file contains the interface io functions based on ITM.
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
 * Mailbox written by the debug probe to inject characters; CMSIS only declares it extern.
 */
volatile int32_t ITM_RxBuffer = ITM_RXBUFFER_EMPTY;

/* Private function prototypes -----------------------------------------------*/
/* Functions Definition ------------------------------------------------------*/

/**
  * @defgroup BASIC_STDIO_ITM_INTERFACE ITM Interface
  * @ingroup BASIC_STDIO_INTERFACE_API
  * @{
  */

/**
  * @brief Initialize ITM interface.
  * @param pobj Reserved, pass NULL.
  */
void interface_io_Init(void *pobj)
{
  /* Empty */
  (void)(pobj); /* prevent "unused variable" warnings */
}

/**
  * @brief Send a buffer over ITM stimulus port.
  * @param Ptr  Pointer to data buffer.
  * @param Size Number of bytes to transfer.
  * @return Bytes written.
  */
uint32_t interface_io_Send(const uint8_t *Ptr, uint16_t Size)
{
  /* Read volatile registers once to avoid toolchain/MISRA 13.5 side-effect warnings */
  const uint32_t demcr = DCB->DEMCR;
  const uint32_t tcr   = ITM->TCR;
  const uint32_t ter   = ITM->TER;

  if (
    ((demcr & DCB_DEMCR_TRCENA_Msk) == 0UL)
    || ((tcr & ITM_TCR_ITMENA_Msk) == 0UL)
    || ((ter & (1UL << 0)) == 0UL)
  )
  {
    return 0;
  }

  for (uint32_t i = 0; i < Size; i++)
  {
    (void)ITM_SendChar(Ptr[i]);
  }
  return Size;
}

/**
  * @brief Receive a buffer from ITM stimulus port.
  * @param Ptr  Data buffer pointer.
  * @param Size Number of bytes to receive.
  * @return Bytes received.
  * @note Blocks until the debugger has injected Size characters into the mailbox:
  *       returning 0 would latch an end of file condition on stdin.
  */
uint32_t interface_io_Receive(uint8_t *Ptr, uint16_t Size)
{
  uint32_t received = 0U;

  while (received < Size)
  {
    /* ITM_ReceiveChar() returns -1 when the debugger has not injected any character. */
    const int32_t character = ITM_ReceiveChar();
    if (character >= 0)
    {
      Ptr[received] = (uint8_t)character;
      received++;
    }
  }
  return received;
}

/**
  * @}
  */
