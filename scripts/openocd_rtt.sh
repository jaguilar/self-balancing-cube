#!/usr/bin/env bash
set -euo pipefail

RTT_PORT="${RTT_PORT:-9090}"
RTT_ADDR="${RTT_ADDR:-0x20000000}"
RTT_SIZE="${RTT_SIZE:-0x100}"
RTT_POLL_MS="${RTT_POLL_MS:-10}"
TCL_PORT="${TCL_PORT:-6666}"
TELNET_PORT="${TELNET_PORT:-4444}"

exec openocd \
  -f interface/stlink-dap.cfg \
  -c "transport select dapdirect_swd" \
  -f target/stm32c5x.cfg \
  -c "tcl_port ${TCL_PORT}" \
  -c "telnet_port ${TELNET_PORT}" \
  -c "init" \
  -c "rtt setup ${RTT_ADDR} ${RTT_SIZE} \"SEGGER RTT\"" \
  -c "rtt polling_interval ${RTT_POLL_MS}" \
  -c "rtt server start ${RTT_PORT} 0" \
  -c "catch { rtt start }"
