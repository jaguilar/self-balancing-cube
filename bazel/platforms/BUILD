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
