/**
  ******************************************************************************
  * @file    basic_stdio_core.c
  * @brief   This file contains the basic stdio utility functions based on ITM.
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
#include "basic_stdio_core.h"
#include "basic_stdio_itf_io.h"

/* Private defines -----------------------------------------------------------*/
/* Private macros ------------------------------------------------------------*/
/* Private typedef -----------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Functions Definition ------------------------------------------------------*/

/**
  * @addtogroup BASIC_STDIO_API BASIC_STDIO Public API
  * @{
  */

/**
  * @brief Initialize stdio interface redirection to the selected io interface.
  * @param pobj Pointer to the output interface context object.
  * @note This parameter need to be NULL when using ITM which requires no context.
  */
void BASIC_STDIO_Init(void *pobj)
{
  interface_io_Init(pobj);
}

/**
  *  @}
  */

#if defined(WRITE_PROTO)
WRITE_PROTO(file, ptr, len)
{
  (void)(file); /* prevent "unused variable" warnings */
  return interface_io_Send((const uint8_t *)ptr, (uint16_t)len);
}
#else
int fputc(int c, FILE *f)
{
  (void)(f); /* prevent "unused variable" warnings */

  uint32_t res;
  res = interface_io_Send((const uint8_t *)&c, 1U);
  if (res != 1U)
  {
    return EOF;
  }
  return (int)res;
}
#endif /* WRITE_PROTO */

#if defined(READ_PROTO)
READ_PROTO(file, ptr, len)
{
  (void)(file); /* prevent "unused variable" warnings */

  return interface_io_Receive((uint8_t *)ptr, len);
}
#else
int fgetc(FILE *f)
{
  uint8_t character;

  (void)(f); /* prevent "unused variable" warnings */

  if (interface_io_Receive(&character, 1U) != 1U)
  {
    return EOF;
  }
  return (int)character;
}
#endif /* READ_PROTO */
