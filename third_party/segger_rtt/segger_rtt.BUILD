load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "segger_rtt",
    srcs = ["RTT/SEGGER_RTT.c"],
    hdrs = [
        "Config/SEGGER_RTT_Conf.h",
        "RTT/SEGGER_RTT.h",
        "RTT/SEGGER_RTT_ConfDefaults.h",
    ],
    copts = [
        "-Wno-sign-compare",
        "-Wno-unused-function",
    ],
    defines = [
        'SEGGER_RTT_SECTION=\\".rtt.cb\\"',
        'SEGGER_RTT_BUFFER_SECTION=\\".rtt.buf\\"',
        "BUFFER_SIZE_UP=4096",
        "BUFFER_SIZE_DOWN=256",
    ],
    includes = [
        "Config",
        "RTT",
    ],
    visibility = ["//visibility:public"],
)
