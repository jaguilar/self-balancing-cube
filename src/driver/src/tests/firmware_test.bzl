"""Bazel rules for cross-compiling, stamping, and running on-target STM32C5 tests."""

load("@rules_python//python:defs.bzl", "py_test")

def _stm32c5_nucleo_transition_impl(settings, attr):
    return {"//command_line_option:platforms": "//bazel/platforms:stm32c5_nucleo"}

_stm32c5_nucleo_transition = transition(
    implementation = _stm32c5_nucleo_transition_impl,
    inputs = [],
    outputs = ["//command_line_option:platforms"],
)

def _stamped_firmware_impl(ctx):
    elf_in = ctx.attr.firmware[0][DefaultInfo].files.to_list()[0]
    out_elf = ctx.actions.declare_file(ctx.label.name + ".elf")
    out_stamp = ctx.actions.declare_file(ctx.label.name + ".stamp")

    ctx.actions.run(
        executable = ctx.executable._stamper,
        inputs = [elf_in],
        outputs = [out_elf, out_stamp],
        arguments = [elf_in.path, out_elf.path, out_stamp.path],
        mnemonic = "StampFirmwareElf",
        progress_message = "Stamping %s" % ctx.label.name,
    )

    return [
        DefaultInfo(files = depset([out_elf, out_stamp])),
        OutputGroupInfo(
            elf = depset([out_elf]),
            stamp = depset([out_stamp]),
        ),
    ]

stamped_firmware = rule(
    implementation = _stamped_firmware_impl,
    attrs = {
        "firmware": attr.label(
            cfg = _stm32c5_nucleo_transition,
            mandatory = True,
        ),
        "_stamper": attr.label(
            default = Label("//src/driver/src/tests:stamp_elf"),
            executable = True,
            cfg = "exec",
        ),
    },
)

def mcu_py_test(
        name,
        srcs,
        firmware = "//src/driver/src/tests:test_runner_firmware_stamped",
        deps = [],
        data = [],
        tags = [],
        **kwargs):
    """Defines a host-side Python test that executes a CppUTest suite on the MCU."""
    elf_fg = name + "_elf"
    stamp_fg = name + "_stamp"

    native.filegroup(
        name = elf_fg,
        srcs = [firmware],
        output_group = "elf",
    )
    native.filegroup(
        name = stamp_fg,
        srcs = [firmware],
        output_group = "stamp",
    )

    py_test(
        name = name,
        srcs = srcs,
        local = True,
        tags = list(tags) + ["exclusive"],
        data = list(data) + [":" + elf_fg, ":" + stamp_fg, "//scripts:openocd_rtt.sh"],
        deps = list(deps) + ["//src/driver/src/tests:testrunner"],
        env = {
            "MCU_FIRMWARE_ELF": "$(rootpath :" + elf_fg + ")",
            "MCU_FIRMWARE_STAMP": "$(rootpath :" + stamp_fg + ")",
            "OPENOCD_RTT_SCRIPT": "$(rootpath //scripts:openocd_rtt.sh)",
        },
        **kwargs
    )
