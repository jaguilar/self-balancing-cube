"""Host-side SEGGER RTT / OpenOCD runner for STM32C5 CppUTest firmware."""

from __future__ import annotations

import argparse
import contextlib
import os
from pathlib import Path
import re
import select
import socket
import subprocess
import tempfile
import time

_READY_RE = re.compile(r"^READY\s+([0-9a-fA-F]+)\s*$")
_RESULT_RE = re.compile(r"^RESULT\s+(-?\d+)\s*$")
_RAN_RE = re.compile(r"\b(\d+)\s+ran\b")
_FAILURE_MARKER = "error: Failure in TEST("
_FAILURE_IDLE_TIMEOUT_S = 0.25

_RTT_ADDR = "0x20000000"
_RTT_SIZE = "0x100"
_DEFAULT_HOST = "127.0.0.1"
_DEFAULT_RTT_PORT = 9090
_DEFAULT_TCL_PORT = 6666


class _RttClient:
    """Minimal TCP socket wrapper for OpenOCD's RTT server."""

    def __init__(self, host: str, port: int) -> None:
        self._host = host
        self._port = port
        self._sock: socket.socket | None = None
        self._buf = bytearray()
        self._connect()

    def _connect(self) -> None:
        sock = socket.create_connection((self._host, self._port), timeout=2.0)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.setblocking(False)
        self._sock = sock

    def reconnect(self) -> None:
        self.close()
        self._buf.clear()
        self._connect()

    def close(self) -> None:
        if self._sock is not None:
            self._sock.close()
            self._sock = None

    def __enter__(self) -> _RttClient:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def drain(self) -> None:
        assert self._sock is not None
        self._buf.clear()
        while True:
            r, _, _ = select.select([self._sock], [], [], 0.05)
            if not r:
                break
            chunk = self._sock.recv(4096)
            if not chunk:
                self.reconnect()
                break

    def write(self, data: bytes) -> None:
        assert self._sock is not None
        view = memoryview(data)
        while view:
            _, w, _ = select.select([], [self._sock], [], 2.0)
            if not w:
                raise TimeoutError("Timed out writing to RTT TCP socket")
            written = self._sock.send(view)
            view = view[written:]

    def read_lines_until(
        self,
        stop_predicate,
        timeout_s: float,
        *,
        early_fail_substring: str | None = None,
        idle_timeout_s: float = _FAILURE_IDLE_TIMEOUT_S,
    ) -> tuple[list[str], object, bool]:
        assert self._sock is not None
        deadline = time.monotonic() + timeout_s
        lines: list[str] = []
        failure_detected = False
        last_activity = time.monotonic()

        while True:
            while b"\n" in self._buf:
                raw_line, _, rest = self._buf.partition(b"\n")
                self._buf = bytearray(rest)
                line = raw_line.decode("utf-8", errors="replace").rstrip("\r")
                lines.append(line)
                if early_fail_substring and not failure_detected and early_fail_substring in line:
                    failure_detected = True
                    last_activity = time.monotonic()
                match = stop_predicate(line)
                if match is not None:
                    return lines, match, failure_detected

            now = time.monotonic()
            effective_deadline = min(deadline, last_activity + idle_timeout_s) if failure_detected else deadline
            remaining = effective_deadline - now
            if remaining <= 0:
                if self._buf:
                    lines.append(self._buf.decode("utf-8", errors="replace"))
                    self._buf.clear()
                return lines, None, failure_detected

            r, _, _ = select.select([self._sock], [], [], min(remaining, 0.05))
            if r:
                chunk = self._sock.recv(4096)
                if chunk:
                    self._buf.extend(chunk)
                    last_activity = time.monotonic()
                else:
                    # Server closed connection; reconnect and keep waiting within deadline.
                    time.sleep(0.05)
                    self.reconnect()


def _tcp_port_open(host: str, port: int, timeout_s: float = 0.2) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout_s):
            return True
    except OSError:
        return False


def _read_log(log_file) -> str:
    log_file.seek(0)
    return log_file.read().decode("utf-8", errors="replace").strip()


@contextlib.contextmanager
def _managed_openocd_rtt(host: str, rtt_port: int, tcl_port: int):
    """Ensure OpenOCD RTT server is running; start and clean it up if we launched it."""
    if _tcp_port_open(host, rtt_port):
        yield
        return

    if host not in ("127.0.0.1", "localhost", "::1"):
        raise RuntimeError(
            f"RTT server not reachable at {host}:{rtt_port} (auto-start only supported for localhost)"
        )

    script_env = os.environ.get("OPENOCD_RTT_SCRIPT")
    script_path = (
        Path(script_env)
        if script_env
        else Path(__file__).resolve().parents[4] / "scripts" / "openocd_rtt.sh"
    )
    if not script_path.is_file():
        raise FileNotFoundError(f"OpenOCD RTT script not found: {script_path}")

    print(
        f"[testrunner] RTT server not running on {host}:{rtt_port}; starting {script_path}",
        flush=True,
    )
    env = os.environ.copy()
    env["RTT_PORT"] = str(rtt_port)
    env["TCL_PORT"] = str(tcl_port)

    log_file = tempfile.TemporaryFile()
    proc = subprocess.Popen(
        [str(script_path)],
        env=env,
        stdin=subprocess.DEVNULL,
        stdout=log_file,
        stderr=subprocess.STDOUT,
    )
    try:
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                output = _read_log(log_file)
                raise RuntimeError(
                    f"{script_path} exited prematurely with code {proc.returncode}:\n{output}"
                )
            if _tcp_port_open(host, rtt_port) and _tcp_port_open(host, tcl_port):
                break
            time.sleep(0.05)
        else:
            output = _read_log(log_file)
            raise TimeoutError(
                f"Timed out waiting for OpenOCD RTT server on {host}:{rtt_port}/{tcl_port}:\n{output}"
            )

        yield
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        log_file.close()


def _openocd_tcl(cmd: str, host: str, tcl_port: int, timeout_s: float = 15.0) -> str:
    """Send a single command to OpenOCD's Tcl RPC port and return the response."""
    with socket.create_connection((host, tcl_port), timeout=2.0) as sock:
        sock.settimeout(timeout_s)
        sock.sendall(f"{cmd}\x1a".encode("utf-8"))
        resp = bytearray()
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            if b"\x1a" in chunk:
                before, _, _ = chunk.partition(b"\x1a")
                resp.extend(before)
                break
            resp.extend(chunk)
        return resp.decode("utf-8", errors="replace")


def _ensure_rtt_started(host: str, tcl_port: int) -> None:
    try:
        _openocd_tcl(f'rtt setup {_RTT_ADDR} {_RTT_SIZE} "SEGGER RTT"', host, tcl_port)
        _openocd_tcl("catch { rtt start }", host, tcl_port)
    except OSError:
        # Tcl port not reachable; proceed and let the RTT socket / fallback handle it.
        pass


def _flash_firmware(elf_path: Path, host: str, tcl_port: int) -> None:
    print(f"[testrunner] Flashing firmware: {elf_path}", flush=True)
    abs_elf = str(elf_path.resolve())
    try:
        _openocd_tcl("catch { rtt stop }", host, tcl_port)
        _openocd_tcl(f"program {{{abs_elf}}} verify reset", host, tcl_port, timeout_s=30.0)
        time.sleep(0.3)
        _openocd_tcl(f'rtt setup {_RTT_ADDR} {_RTT_SIZE} "SEGGER RTT"', host, tcl_port)
        _openocd_tcl("rtt start", host, tcl_port)
        return
    except OSError:
        pass

    cmd = [
        "openocd",
        "-f",
        "interface/stlink-dap.cfg",
        "-c",
        "transport select dapdirect_swd",
        "-f",
        "target/stm32c5x.cfg",
        "-c",
        f"program {abs_elf} verify reset exit",
    ]
    subprocess.run(cmd, check=True)


def _wait_for_ready(client: _RttClient, timeout_s: float) -> str | None:
    _, match, _ = client.read_lines_until(_READY_RE.match, timeout_s)
    if match is None:
        return None
    return match.group(1).lower()


def _parse_cli_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--test-filter", "--filter", dest="test_filter", default=None)
    parser.add_argument("--host", default=None)
    parser.add_argument("--port", type=int, default=None)
    parser.add_argument("--tcl-port", type=int, default=None)
    parser.add_argument("--timeout", type=float, default=None)
    args, _ = parser.parse_known_args()
    return args


def run_test(
    suite: str,
    *,
    test_filter: str | None = None,
    timeout_s: float = 10.0,
    host: str | None = None,
    port: int | None = None,
    tcl_port: int | None = None,
) -> str:
    """Ensure matching firmware is flashed, run a CppUTest suite over RTT TCP, and return transcript."""
    cli = _parse_cli_args()
    effective_filter = test_filter or cli.test_filter or os.environ.get("TESTBRIDGE_TEST_ONLY")
    effective_host = host or cli.host or os.environ.get("RTT_HOST", _DEFAULT_HOST)
    effective_port = (
        port
        if port is not None
        else (cli.port if cli.port is not None else int(os.environ.get("RTT_PORT", _DEFAULT_RTT_PORT)))
    )
    effective_tcl_port = (
        tcl_port
        if tcl_port is not None
        else (
            cli.tcl_port
            if cli.tcl_port is not None
            else int(os.environ.get("OPENOCD_TCL_PORT", _DEFAULT_TCL_PORT))
        )
    )
    effective_timeout = cli.timeout if cli.timeout is not None else timeout_s

    elf_env = os.environ.get("MCU_FIRMWARE_ELF")
    stamp_env = os.environ.get("MCU_FIRMWARE_STAMP")
    if not elf_env or not stamp_env:
        raise RuntimeError("MCU_FIRMWARE_ELF and MCU_FIRMWARE_STAMP must be set")

    elf_path = Path(elf_env)
    expected_stamp = Path(stamp_env).read_text(encoding="ascii").strip().lower()

    with _managed_openocd_rtt(effective_host, effective_port, effective_tcl_port):
        _ensure_rtt_started(effective_host, effective_tcl_port)

        with _RttClient(effective_host, effective_port) as client:
            client.drain()
            client.write(b"RESET\n")
            running_stamp = _wait_for_ready(client, timeout_s=2.0)

            if running_stamp != expected_stamp:
                print(
                    f"[testrunner] Buildstamp mismatch (board={running_stamp!r}, expected={expected_stamp!r})",
                    flush=True,
                )
                _flash_firmware(elf_path, effective_host, effective_tcl_port)
                client.drain()
                client.write(b"RESET\n")
                running_stamp = _wait_for_ready(client, timeout_s=3.0)
                if running_stamp != expected_stamp:
                    raise AssertionError(
                        f"Board failed to report expected buildstamp {expected_stamp!r} after flash "
                        f"(got {running_stamp!r})"
                    )
            else:
                print(
                    f"[testrunner] Firmware buildstamp {expected_stamp} matches; skipping flash.",
                    flush=True,
                )

            cmd = f"RUN {suite}.{effective_filter}\n" if effective_filter else f"RUN {suite}\n"
            print(f"[testrunner] Sending: {cmd.strip()}", flush=True)
            client.write(cmd.encode("ascii"))

            lines, result_match, failure_detected = client.read_lines_until(
                _RESULT_RE.match,
                effective_timeout,
                early_fail_substring=_FAILURE_MARKER,
                idle_timeout_s=_FAILURE_IDLE_TIMEOUT_S,
            )
            transcript = "\n".join(lines)
            print(transcript, flush=True)

            if result_match is None:
                # Reset the MCU so any active motor/PWM outputs are disabled
                # even if the test timed out, hung, or aborted before teardown.
                try:
                    _openocd_tcl("reset run", effective_host, effective_tcl_port, timeout_s=5.0)
                except OSError:
                    pass
                if failure_detected:
                    raise AssertionError(
                        f"CppUTest suite {suite!r} failed (detected {_FAILURE_MARKER!r}; "
                        f"stream idle for {_FAILURE_IDLE_TIMEOUT_S:.2f}s)"
                    )
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
