package(default_visibility = ["//visibility:public"])

constraint_setting(name = "mcu")

constraint_value(
    name = "stm32g4_mcu",
    constraint_setting = ":mcu",
)

constraint_value(
    name = "stm32c5_mcu",
    constraint_setting = ":mcu",
)

constraint_setting(name = "board")

constraint_value(
    name = "motor_driver_v1",
    constraint_setting = ":board",
)

constraint_value(
    name = "nucleo_c552re",
    constraint_setting = ":board",
)

constraint_setting(name = "hil_wiring")

constraint_value(
    name = "hil_selftest",
    constraint_setting = ":hil_wiring",
)

constraint_value(
    name = "drv8304h_hil_test",
    constraint_setting = ":hil_wiring",
)

platform(
    name = "host_selftest",
    constraint_values = [
        ":hil_selftest",
    ],
    parents = ["@platforms//host"],
)

platform(
    name = "host_drv8304h_hil_test",
    constraint_values = [
        ":drv8304h_hil_test",
    ],
    parents = ["@platforms//host"],
)

platform(
    name = "stm32g4",
    constraint_values = [
        "@platforms//os:none",
        "@platforms//cpu:arm",
        ":stm32g4_mcu",
    ],
)

platform(
    name = "stm32c5",
    constraint_values = [
        "@platforms//os:none",
        "@platforms//cpu:arm",
        ":stm32c5_mcu",
        ":motor_driver_v1",
    ],
)

platform(
    name = "stm32c5_nucleo",
    constraint_values = [
        "@platforms//os:none",
        "@platforms//cpu:arm",
        ":stm32c5_mcu",
        ":nucleo_c552re",
    ],
)
