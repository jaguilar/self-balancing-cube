# Thread Safe LibC Software Pack

![tag](https://img.shields.io/badge/tag-v2.1.1-brightgreen.svg)
[![release note](https://img.shields.io/badge/release_note-view_html-gold.svg)](https://htmlpreview.github.io/?https://github.com/STMicroelectronics/stm32-util-thread-safe-libc/blob/main/Release_Notes.html)

## Overview
This utility implements the mutex locks needed to make libc thread-safe.
Depending on the libc implementation used in the project, a different set of hooks is provided, in accordance with its documentation. Refer to the relevant C source files for further details.

## Description and Usage

### Description

This component is structured around "libc glue" files:
- `ARM/armlib_lock_glue.c`
- `GCC/newlib_lock_glue.c`
- `IAR/dlib_lock_glue.c`
These files implement the necessary APIs needed by their libc flavor to ensure thread-safety.
They rely on `stm32_lock.h` (located at the root of the component) for a basic set of "common" APIs to reuse.
This header also handles the configuration of the component (strategy selection).
It may, if so configured, delegate work to `template/stm32_lock_user.h`,
which contains stubbed implementations of the required primitives, to be filled in by the user.

### Usage

At runtime, the user code must not interact with this component directly, but rather let libc manage it on it autonomously.

The strategy can be explicitly selected by defining exactly one of the following macros in the project:
- `STM32_THREAD_SAFE_USER_LOCKS=1`
  user‑defined implementation of the locking mechanism (template provided)
2. `STM32_THREAD_SAFE_BAREMETAL_ALLOW_LOCKS=1`
  bare-metal; allow lock usage from interrupts
3. `STM32_THREAD_SAFE_BAREMETAL_DENY_LOCKS=1`
  bare-metal; deny lock usage from interrupts (thread is hung on use)
4. `STM32_THREAD_SAFE_FREERTOS_ALLOW_LOCKS=1`
  FreeRTOS; allow lock usage from interrupts
5. `STM32_THREAD_SAFE_FREERTOS_DENY_LOCKS=1`
  FreeRTOS; deny lock usage from interrupts (thread is hung on use)
