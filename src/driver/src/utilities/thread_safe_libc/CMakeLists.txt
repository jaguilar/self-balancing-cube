# origin-pack: STMicroelectronics::thread_safe_libc@2.1.2
# file-format: 1.0.0
project(STMicroelectronics_thread_safe_libc_2_1_2)
cmake_minimum_required(VERSION 3.30)
add_library(STMicroelectronics_thread_safe_libc_2_1_2 INTERFACE)

# List of all CMSIS properties that influence conditions for this package
if(NOT DEFINED CMSIS_Dname)
  set(CMSIS_Dname "CMSIS_Dname not set")
  message(DEBUG "CMSIS_Dname is not set to any value")
endif()

if(NOT DEFINED CMSIS_Dvendor)
  set(CMSIS_Dvendor "CMSIS_Dvendor not set")
  message(DEBUG "CMSIS_Dvendor is not set to any value")
endif()

if(NOT DEFINED CMSIS_Tcompiler)
  set(CMSIS_Tcompiler "CMSIS_Tcompiler not set")
  message(DEBUG "CMSIS_Tcompiler is not set to any value")
endif()


# Device specific defined symbols








# Enable all components in this package
if(CMSIS_ENTIRE_STMicroelectronics_thread_safe_libc_2_1_2)
  list(APPEND CMSIS_COMPONENTS_LIST "Cvendor:STMicroelectronics#Cclass:Utility#Cgroup:Thread-safe libc#Csub:libc#Cversion:1.1.0")
  list(APPEND CMSIS_COMPONENTS_LIST "Cvendor:STMicroelectronics#Cclass:Utility#Cgroup:Thread-safe libc#Csub:user template#Cversion:1.0.1")
endif()

# All conditions used by this package

# condition: STMicroelectronics.thread_safe_libc.2.1.2
# description: 
if((CMSIS_Dname MATCHES "STM32.*" AND CMSIS_Dvendor STREQUAL "STMicroelectronics:13"))
  set(STMicroelectronics.thread_safe_libc.2.1.2 "1")
  message(DEBUG "CMSIS condition STMicroelectronics.thread_safe_libc.2.1.2 enabled")
else()
  set(STMicroelectronics.thread_safe_libc.2.1.2 0)
endif()


# condition: STMicroelectronics.thread_safe_libc.2.1.2:ARMCC
# description: ARMlibc, usually linked by armclang compilers
if(CMSIS_Tcompiler STREQUAL "ARMCC")
  set(STMicroelectronics.thread_safe_libc.2.1.2_ARMCC "1")
  message(DEBUG "CMSIS condition STMicroelectronics.thread_safe_libc.2.1.2_ARMCC enabled")
else()
  set(STMicroelectronics.thread_safe_libc.2.1.2_ARMCC 0)
endif()


# condition: STMicroelectronics.thread_safe_libc.2.1.2:GCC
# description: nanolibc, usually linked by GCC compilers
if(CMSIS_Tcompiler STREQUAL "GCC")
  set(STMicroelectronics.thread_safe_libc.2.1.2_GCC "1")
  message(DEBUG "CMSIS condition STMicroelectronics.thread_safe_libc.2.1.2_GCC enabled")
else()
  set(STMicroelectronics.thread_safe_libc.2.1.2_GCC 0)
endif()


# condition: STMicroelectronics.thread_safe_libc.2.1.2:IAR
# description: Dlib, usually linked by IAR compilers
if(CMSIS_Tcompiler STREQUAL "IAR")
  set(STMicroelectronics.thread_safe_libc.2.1.2_IAR "1")
  message(DEBUG "CMSIS condition STMicroelectronics.thread_safe_libc.2.1.2_IAR enabled")
else()
  set(STMicroelectronics.thread_safe_libc.2.1.2_IAR 0)
endif()


# condition: STMicroelectronics.thread_safe_libc.2.1.2:ThreadSafeLibc Config
# description: STMicroelectronics Utility Thread_Safe_LibC Config
set(STMicroelectronics.thread_safe_libc.2.1.2_ThreadSafeLibc_Config "$<NOT:$<STREQUAL:$<FILTER:${CMSIS_COMPONENTS_LIST},INCLUDE,.*Cclass:Utility#.*Cgroup:Thread-safe libc#.*Csub:libc(#.*|$)>,>>")
message(DEBUG "CMSIS condition STMicroelectronics.thread_safe_libc.2.1.2_ThreadSafeLibc_Config enabled")

# Files and components in this package
if("Cvendor:STMicroelectronics#Cclass:Utility#Cgroup:Thread-safe libc#Csub:libc#Cversion:1.1.0" IN_LIST CMSIS_COMPONENTS_LIST)  # Libc locks using predefined strategies
  message(DEBUG "Using component Utility_Thread_safe_libc_libc_1_1_0")
  target_compile_definitions(STMicroelectronics_thread_safe_libc_2_1_2 INTERFACE "-DCMSIS_USE_Utility_Thread_safe_libc_libc_1_1_0=1")
  target_include_directories(STMicroelectronics_thread_safe_libc_2_1_2 INTERFACE "${CMAKE_CURRENT_LIST_DIR}/.")
  target_sources(STMicroelectronics_thread_safe_libc_2_1_2 INTERFACE "$<${STMicroelectronics.thread_safe_libc.2.1.2_ARMCC}:${CMAKE_CURRENT_LIST_DIR}/ARM/armlib_lock_glue.c>")
  target_sources(STMicroelectronics_thread_safe_libc_2_1_2 INTERFACE "$<${STMicroelectronics.thread_safe_libc.2.1.2_GCC}:${CMAKE_CURRENT_LIST_DIR}/GCC/newlib_lock_glue.c>")
  target_sources(STMicroelectronics_thread_safe_libc_2_1_2 INTERFACE "$<${STMicroelectronics.thread_safe_libc.2.1.2_IAR}:${CMAKE_CURRENT_LIST_DIR}/IAR/dlib_lock_glue.c>")
endif()

if("Cvendor:STMicroelectronics#Cclass:Utility#Cgroup:Thread-safe libc#Csub:user template#Cversion:1.0.1" IN_LIST CMSIS_COMPONENTS_LIST)  # Libc locks using predefined strategies
  message(DEBUG "Using component Utility_Thread_safe_libc_user_template_1_0_1")
  target_compile_definitions(STMicroelectronics_thread_safe_libc_2_1_2 INTERFACE "-DCMSIS_USE_Utility_Thread_safe_libc_user_template_1_0_1=1")
  target_include_directories(STMicroelectronics_thread_safe_libc_2_1_2 INTERFACE "${CMAKE_CURRENT_LIST_DIR}/templates")
endif()

