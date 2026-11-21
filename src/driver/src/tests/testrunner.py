"""Host-side UART/OpenOCD runner for STM32C5 CppUTest firmware."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import termios
import time

_BAUD_MAP = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
    230400: termios.B230400,
    460800: termios.B460800,
    921600: termios.B921600,
}

_READY_RE = re.compile(r"^READY\s+([0-9a-fA-F]+)\s*$")
_RESULT_RE = re.compile(r"^RESULT\s+(-?\d+)\s*$")
_RAN_RE = re.compile(r"\b(\d+)\s+ran\b")


class _SerialPort:
    """Minimal raw 8N1 UART wrapper using Python stdlib termios."""

    def __init__(self, device: str, baud: int) -> None:
        if baud not in _BAUD_MAP:
            raise ValueError(f"Unsupported baud rate: {baud}")
        self._fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            attrs = termios.tcgetattr(self._fd)
            # Raw 8N1, no flow control, local receiver enabled.
            attrs[0] = 0  # iflag
            attrs[1] = 0  # oflag
            attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
            attrs[3] = 0  # lflag
            attrs[4] = _BAUD_MAP[baud]
            attrs[5] = _BAUD_MAP[baud]
            attrs[6][termios.VMIN] = 0
            attrs[6][termios.VTIME] = 0
            termios.tcsetattr(self._fd, termios.TCSANOW, attrs)
            termios.tcflush(self._fd, termios.TCIOFLUSH)
        except Exception:
            os.close(self._fd)
            raise
        self._buf = bytearray()

    def close(self) -> None:
        os.close(self._fd)

    def __enter__(self) -> _SerialPort:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def drain(self) -> None:
        termios.tcflush(self._fd, termios.TCIFLUSH)
        self._buf.clear()
        while True:
            r, _, _ = select.select([self._fd], [], [], 0.02)
            if not r:
                break
            chunk = os.read(self._fd, 4096)
            if not chunk:
                break

    def write(self, data: bytes) -> None:
        view = memoryview(data)
        while view:
            written = os.write(self._fd, view)
            view = view[written:]

    def read_lines_until(
        self,
        stop_predicate,
        timeout_s: float,
    ) -> tuple[list[str], object]:
        deadline = time.monotonic() + timeout_s
        lines: list[str] = []

        while True:
            while b"\n" in self._buf:
                raw_line, _, rest = self._buf.partition(b"\n")
                self._buf = bytearray(rest)
                line = raw_line.decode("utf-8", errors="replace").rstrip("\r")
                lines.append(line)
                match = stop_predicate(line)
                if match is not None:
                    return lines, match

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                if self._buf:
                    lines.append(self._buf.decode("utf-8", errors="replace"))
                    self._buf.clear()
                return lines, None

            r, _, _ = select.select([self._fd], [], [], min(remaining, 0.1))
            if r:
                chunk = os.read(self._fd, 4096)
                if chunk:
                    self._buf.extend(chunk)


def _flash_firmware(elf_path: Path) -> None:
    print(f"[testrunner] Flashing firmware: {elf_path}", flush=True)
    cmd = [
        "openocd",
        "-f",
        "interface/stlink-dap.cfg",
        "-c",
        "transport select dapdirect_swd",
        "-f",
        "target/stm32c5x.cfg",
        "-c",
        f"program {elf_path} verify reset exit",
    ]
    subprocess.run(cmd, check=True)


def _wait_for_ready(ser: _SerialPort, timeout_s: float) -> str | None:
    _, match = ser.read_lines_until(_READY_RE.match, timeout_s)
    if match is None:
        return None
    return match.group(1).lower()


def _parse_cli_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--test-filter", "--filter", dest="test_filter", default=None)
    parser.add_argument("--port", default=None)
    parser.add_argument("--timeout", type=float, default=None)
    args, _ = parser.parse_known_args()
    return args


def run_test(
    suite: str,
    *,
    test_filter: str | None = None,
    timeout_s: float = 10.0,
    port: str | None = None,
    baud: int = 115200,
) -> str:
    """Ensure matching firmware is flashed, run a CppUTest suite over UART, and return transcript."""
    cli = _parse_cli_args()
    effective_filter = test_filter or cli.test_filter or os.environ.get("TESTBRIDGE_TEST_ONLY")
    effective_port = port or cli.port or os.environ.get("UART_PORT", "/dev/ttyACM0")
    effective_timeout = cli.timeout if cli.timeout is not None else timeout_s

    elf_env = os.environ.get("MCU_FIRMWARE_ELF")
    stamp_env = os.environ.get("MCU_FIRMWARE_STAMP")
    if not elf_env or not stamp_env:
        raise RuntimeError("MCU_FIRMWARE_ELF and MCU_FIRMWARE_STAMP must be set")

    elf_path = Path(elf_env)
    expected_stamp = Path(stamp_env).read_text(encoding="ascii").strip().lower()

    with _SerialPort(effective_port, baud) as ser:
        ser.drain()
        ser.write(b"RESET\n")
        running_stamp = _wait_for_ready(ser, timeout_s=2.0)

        if running_stamp != expected_stamp:
            print(
                f"[testrunner] Buildstamp mismatch (board={running_stamp!r}, expected={expected_stamp!r})",
                flush=True,
            )
            _flash_firmware(elf_path)
            ser.drain()
            ser.write(b"RESET\n")
            running_stamp = _wait_for_ready(ser, timeout_s=3.0)
            if running_stamp != expected_stamp:
                raise AssertionError(
                    f"Board failed to report expected buildstamp {expected_stamp!r} after flash "
                    f"(got {running_stamp!r})"
                )
        else:
            print(f"[testrunner] Firmware buildstamp {expected_stamp} matches; skipping flash.", flush=True)

        cmd = f"RUN {suite}.{effective_filter}\n" if effective_filter else f"RUN {suite}\n"
        print(f"[testrunner] Sending: {cmd.strip()}", flush=True)
        ser.write(cmd.encode("ascii"))

        lines, result_match = ser.read_lines_until(_RESULT_RE.match, effective_timeout)
        transcript = "\n".join(lines)
        print(transcript, flush=True)

        if result_match is None:
            raise TimeoutError(
                f"Timed out after {effective_timeout}s waiting for RESULT from {cmd.strip()}"
            )

        failures = int(result_match.group(1))
        if failures != 0:
            raise AssertionError(f"CppUTest suite {suite!r} failed with {failures} failure(s)")

        ran_match = _RAN_RE.search(transcript)
        if ran_match is None or int(ran_match.group(1)) < 1:
            raise AssertionError(
                f"No tests ran for command {cmd.strip()!r}; check suite/test name spelling."
            )

        return transcript
