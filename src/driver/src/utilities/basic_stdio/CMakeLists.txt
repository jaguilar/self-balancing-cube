# origin-pack: STMicroelectronics::basic_stdio@2.1.0
# file-format: 1.0.0
project(STMicroelectronics_basic_stdio_2_1_0)
cmake_minimum_required(VERSION 3.30)
add_library(STMicroelectronics_basic_stdio_2_1_0 INTERFACE)

# List of all CMSIS properties that influence conditions for this package
if(NOT DEFINED CMSIS_Dname)
  set(CMSIS_Dname "CMSIS_Dname not set")
  message(DEBUG "CMSIS_Dname is not set to any value")
endif()

if(NOT DEFINED CMSIS_Dvendor)
  set(CMSIS_Dvendor "CMSIS_Dvendor not set")
  message(DEBUG "CMSIS_Dvendor is not set to any value")
endif()


# Device specific defined symbols








# Enable all components in this package
if(CMSIS_ENTIRE_STMicroelectronics_basic_stdio_2_1_0)
  list(APPEND CMSIS_COMPONENTS_LIST "Cvendor:STMicroelectronics#Cclass:Utility#Cgroup:Basic stdio#Csub:Core#Cversion:1.1.0")
  list(APPEND CMSIS_COMPONENTS_LIST "Cvendor:STMicroelectronics#Cclass:Utility#Cgroup:Basic stdio#Csub:itf_io#Cvariant:ITM#Cversion:1.1.0")
  list(APPEND CMSIS_COMPONENTS_LIST "Cvendor:STMicroelectronics#Cclass:Utility#Cgroup:Basic stdio#Csub:itf_io#Cvariant:UART#Cversion:1.1.0")
  list(APPEND CMSIS_COMPONENTS_LIST "Cvendor:STMicroelectronics#Cclass:Utility#Cgroup:Basic stdio#Csub:itf_io#Cvariant:Template#Cversion:1.1.0")
endif()

# All conditions used by this package

# condition: STMicroelectronics.basic_stdio.2.1.0
# description: 
if((CMSIS_Dname MATCHES "STM32.*" AND CMSIS_Dvendor STREQUAL "STMicroelectronics:13"))
  set(STMicroelectronics.basic_stdio.2.1.0 "1")
  message(DEBUG "CMSIS condition STMicroelectronics.basic_stdio.2.1.0 enabled")
else()
  set(STMicroelectronics.basic_stdio.2.1.0 0)
endif()


# condition: STMicroelectronics.basic_stdio.2.1.0:Basic stdio Config
# description: STMicroelectronics Utility Basic_STDIO Config
set(STMicroelectronics.basic_stdio.2.1.0_Basic_stdio_Config "$<NOT:$<STREQUAL:$<FILTER:${CMSIS_COMPONENTS_LIST},INCLUDE,.*Cclass:Utility#.*Cgroup:Basic stdio#.*Csub:Core(#.*|$)>,>>")
message(DEBUG "CMSIS condition STMicroelectronics.basic_stdio.2.1.0_Basic_stdio_Config enabled")


# condition: STMicroelectronics.basic_stdio.2.1.0:Cortex-M ITM
# description: ARM Cortex-M ITM as low level component
set(STMicroelectronics.basic_stdio.2.1.0_Cortex-M_ITM "$<NOT:$<STREQUAL:$<FILTER:${CMSIS_COMPONENTS_LIST},INCLUDE,.*Cclass:CMSIS#.*Cgroup:CORE(#.*|$)>,>>")
message(DEBUG "CMSIS condition STMicroelectronics.basic_stdio.2.1.0_Cortex-M_ITM enabled")


# condition: STMicroelectronics.basic_stdio.2.1.0:STM32 HAL UART
# description: STMicroelectronics UART HAL Driver as low level component
set(STMicroelectronics.basic_stdio.2.1.0_STM32_HAL_UART "$<NOT:$<STREQUAL:$<FILTER:${CMSIS_COMPONENTS_LIST},INCLUDE,.*Cclass:Device#.*Cgroup:STM32 HAL#.*Csub:UART(#.*|$)>,>>")
message(DEBUG "CMSIS condition STMicroelectronics.basic_stdio.2.1.0_STM32_HAL_UART enabled")


# condition: STMicroelectronics.basic_stdio.2.1.0:interface
# description: selection of io interface is required to use the component
set(STMicroelectronics.basic_stdio.2.1.0_interface "$<OR:$<NOT:$<STREQUAL:$<FILTER:${CMSIS_COMPONENTS_LIST},INCLUDE,.*Cclass:Utility#.*Cgroup:Basic stdio#.*Csub:itf_io#.*Cvariant:UART(#.*|$)>,>>,$<NOT:$<STREQUAL:$<FILTER:${CMSIS_COMPONENTS_LIST},INCLUDE,.*Cclass:Utility#.*Cgroup:Basic stdio#.*Csub:itf_io#.*Cvariant:ITM(#.*|$)>,>>,$<NOT:$<STREQUAL:$<FILTER:${CMSIS_COMPONENTS_LIST},INCLUDE,.*Cclass:Utility#.*Cgroup:Basic stdio#.*Csub:itf_io#.*Cvariant:Template(#.*|$)>,>>>")
message(DEBUG "CMSIS condition STMicroelectronics.basic_stdio.2.1.0_interface enabled")

# Files and components in this package
if("Cvendor:STMicroelectronics#Cclass:Utility#Cgroup:Basic stdio#Csub:Core#Cversion:1.1.0" IN_LIST CMSIS_COMPONENTS_LIST)  # stm32 Basic_STDIO core part
  message(DEBUG "Using component Utility_Basic_stdio_Core_1_1_0")
  target_compile_definitions(STMicroelectronics_basic_stdio_2_1_0 INTERFACE "$<${STMicroelectronics.basic_stdio.2.1.0_interface}:-DCMSIS_USE_Utility_Basic_stdio_Core_1_1_0=1>")
  target_include_directories(STMicroelectronics_basic_stdio_2_1_0 INTERFACE "$<${STMicroelectronics.basic_stdio.2.1.0_interface}:${CMAKE_CURRENT_LIST_DIR}/.>")
  target_include_directories(STMicroelectronics_basic_stdio_2_1_0 INTERFACE "$<${STMicroelectronics.basic_stdio.2.1.0_interface}:${CMAKE_CURRENT_LIST_DIR}/interface_io>")
  target_sources(STMicroelectronics_basic_stdio_2_1_0 INTERFACE "$<${STMicroelectronics.basic_stdio.2.1.0_interface}:${CMAKE_CURRENT_LIST_DIR}/basic_stdio_core.c>")
endif()

if("Cvendor:STMicroelectronics#Cclass:Utility#Cgroup:Basic stdio#Csub:itf_io#Cvariant:ITM#Cversion:1.1.0" IN_LIST CMSIS_COMPONENTS_LIST)  # ITM-based Basic_STDIO redirection (no configuration)
  message(DEBUG "Using component Utility_Basic_stdio_itf_io_ITM_1_1_0")
  target_compile_definitions(STMicroelectronics_basic_stdio_2_1_0 INTERFACE "$<${STMicroelectronics.basic_stdio.2.1.0_Cortex-M_ITM}:-DCMSIS_USE_Utility_Basic_stdio_itf_io_ITM_1_1_0=1>")
  target_sources(STMicroelectronics_basic_stdio_2_1_0 INTERFACE "$<${STMicroelectronics.basic_stdio.2.1.0_Cortex-M_ITM}:${CMAKE_CURRENT_LIST_DIR}/interface_io/basic_stdio_itfio_itm.c>")
endif()

if("Cvendor:STMicroelectronics#Cclass:Utility#Cgroup:Basic stdio#Csub:itf_io#Cvariant:UART#Cversion:1.1.0" IN_LIST CMSIS_COMPONENTS_LIST)  # UART-based Basic_STDIO redirection (no configuration)
  message(DEBUG "Using component Utility_Basic_stdio_itf_io_UART_1_1_0")
  target_compile_definitions(STMicroelectronics_basic_stdio_2_1_0 INTERFACE "$<${STMicroelectronics.basic_stdio.2.1.0_STM32_HAL_UART}:-DCMSIS_USE_Utility_Basic_stdio_itf_io_UART_1_1_0=1>")
  target_sources(STMicroelectronics_basic_stdio_2_1_0 INTERFACE "$<${STMicroelectronics.basic_stdio.2.1.0_STM32_HAL_UART}:${CMAKE_CURRENT_LIST_DIR}/interface_io/basic_stdio_itfio_uart.c>")
endif()

if("Cvendor:STMicroelectronics#Cclass:Utility#Cgroup:Basic stdio#Csub:itf_io#Cvariant:Template#Cversion:1.1.0" IN_LIST CMSIS_COMPONENTS_LIST)  # No stdio redirection at all: up to you to implement the services based on the templates
  message(DEBUG "Using component Utility_Basic_stdio_itf_io_Template_1_1_0")
  target_compile_definitions(STMicroelectronics_basic_stdio_2_1_0 INTERFACE "-DCMSIS_USE_Utility_Basic_stdio_itf_io_Template_1_1_0=1")
  if(EXISTS ${CMSIS_RTE_FOLDER}/Utility/basic_stdio_itfio_template.c)
    target_sources(STMicroelectronics_basic_stdio_2_1_0 INTERFACE "${CMSIS_RTE_FOLDER}/Utility/basic_stdio_itfio_template.c")
  else()
    target_sources(STMicroelectronics_basic_stdio_2_1_0 INTERFACE "${CMAKE_CURRENT_LIST_DIR}/interface_io/basic_stdio_itfio_template.c")
  endif()
endif()

