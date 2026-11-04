# Basic STDIO Software Pack

![tag](https://img.shields.io/badge/tag-v2.0.1-brightgreen.svg)
[![release note](https://img.shields.io/badge/release_note-view_html-gold.svg)](https://htmlpreview.github.io/?https://github.com/STMicroelectronics/stm32-util-basic-stdio/blob/main/Release_Notes.html)

## Overview

Basic STDIO is a minimal standard input/output redirection layer for STM32 that provides quick data transmission and reception through UART, ITM (SWO), or a custom interface.
The application remains responsible for formatting transmitted messages and parsing received data.

## Description and Usage

### Description

Basic STDIO is a lightweight trace utility with a small footprint and low integration effort, designed for early board bring-up, firmware prototyping, and basic diagnostic tracing, and easy to integrate, remove, replace, or extend later.

Main features of Basic STDIO:

- Single init entry point
- Supports UART, ITM, or a custom interface
- Standard C TX functions: printf, puts, and putchar
- Standard C RX function: getchar
- Blocking I/O path: TX is sent immediately and RX returns exactly one character per call
- Hides toolchain libc hook differences (_write, __write, fputc, _read, __read, and fgetc)

### Usage

The start-up sequence can be split into three steps:

### Step 1: Basic STDIO initialization

Initialize Basic STDIO with the selected interface handle by calling `BASIC_STDIO_Init(&io_handle)` for UART or custom interfaces, or with NULL when using ITM.

### Step 2: Data transmission

Send data using `printf`, `puts`, or `putchar` over the configured interface.

### Step 3: Data reception

Receive one character using the standard C `getchar()` function.
The selected interface must provide the received data through its RX path. Each call returns exactly one character and blocks until it is available.

Example:

```c
int received = getchar();
if (received != EOF)
{
	printf("received: %c\n", received);
}
```

`getchar()` blocks until a character arrives, so it only returns `EOF` when the interface cannot deliver one at all, typically when `BASIC_STDIO_Init()` has not been called.

The application is responsible for formatting transmitted messages and parsing received data after receiving it with `getchar()`.
