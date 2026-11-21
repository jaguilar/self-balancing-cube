load(":cc_toolchain_config.bzl", "cc_toolchain_config")
load("@rules_cc//cc:defs.bzl", "cc_toolchain")

package(default_visibility = ["//visibility:public"])

filegroup(name = "empty")

cc_toolchain_config(
    name = "arm_none_eabi_toolchain_config",
    linker_script = "//:STM32G474XX_FLASH.ld",
)

cc_toolchain(
    name = "arm_none_eabi_cc_toolchain",
    toolchain_identifier = "arm-none-eabi-toolchain",
    toolchain_config = ":arm_none_eabi_toolchain_config",
    all_files = "//:STM32G474XX_FLASH.ld",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = "//:STM32G474XX_FLASH.ld",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 0,
)

toolchain(
    name = "arm_none_eabi_toolchain",
    exec_compatible_with = [],
    target_compatible_with = [
        "@platforms//os:none",
        "@platforms//cpu:arm",
        "//bazel/platforms:stm32g4_mcu",
    ],
    toolchain = ":arm_none_eabi_cc_toolchain",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

cc_toolchain_config(
    name = "stm32c5_toolchain_config",
    cpu = "cortex-m33",
    fpu = "fpv5-sp-d16",
    cxx_std = "c++23",
    map_file = "motor-driver.map",
    linker_script = "//src/driver/src/project/user_modifiable:stm32c562xe_flash.ld",
)

cc_toolchain(
    name = "stm32c5_cc_toolchain",
    toolchain_identifier = "stm32c5-toolchain",
    toolchain_config = ":stm32c5_toolchain_config",
    all_files = "//src/driver/src/project/user_modifiable:stm32c562xe_flash.ld",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = "//src/driver/src/project/user_modifiable:stm32c562xe_flash.ld",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 0,
)

toolchain(
    name = "stm32c5_toolchain",
    exec_compatible_with = [],
    target_compatible_with = [
        "@platforms//os:none",
        "@platforms//cpu:arm",
        "//bazel/platforms:stm32c5_mcu",
    ],
    toolchain = ":stm32c5_cc_toolchain",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)
