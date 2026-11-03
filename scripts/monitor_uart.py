#! /usr/bin/env python3

import serial
import time
import sys
import os
import subprocess
import argparse

def reset_target():
    print("Resetting target via OpenOCD...")
    try:
        # Run OpenOCD to trigger a hardware reset and let target run.
        subprocess.run([
            'openocd', 
            '-f', 'interface/stlink.cfg', 
            '-f', 'target/stm32g4x.cfg', 
            '-c', 'init; reset run; exit'
        ], capture_output=True, check=True)
        print("Target reset successfully.")
    except Exception as e:
        print(f"Warning: Failed to reset target: {e}")

def main():
    default_port = os.getenv('UART_PORT', '/dev/ttyACM0' if os.path.exists('/dev/ttyACM0') else '/dev/ttyUSB0')
    default_baud = int(os.getenv('UART_BAUD', '115200'))

    parser = argparse.ArgumentParser(description='Monitor STM32 UART output.')
    parser.add_argument('--port', '-p', default=default_port,
                        help=f'Serial port to monitor (default: {default_port})')
    parser.add_argument('--baud', '-b', type=int, default=default_baud,
                        help=f'Baud rate (default: {default_baud})')
    parser.add_argument('--timeout', '-t', type=float, default=None,
                        help='Exit after specified seconds of inactivity (default: None)')
    parser.add_argument('--no-reset', action='store_true',
                        help='Do not trigger OpenOCD reset before monitoring')
    
    args = parser.parse_args()

    try:
        if not args.no_reset:
            reset_target()
            # Give OpenOCD a moment to release the ST-Link
            time.sleep(0.5)

        port = args.port
        baud = args.baud
        timeout_val = args.timeout
        
        ser = serial.Serial(port, baud, timeout=1)
        print(f"Connected to {port} at {baud} baud.")
        print(f"Monitoring UART... (Timeout: {timeout_val}s, Ctrl+C to stop)")
        
        last_data_time = time.time()
        while True:
            if ser.in_waiting:
                data = ser.read(ser.in_waiting).decode('ascii', errors='replace')
                sys.stdout.write(data)
                sys.stdout.flush()
                last_data_time = time.time()
            
            if timeout_val is not None and (time.time() - last_data_time) > timeout_val:
                print(f"\nNo data received for {timeout_val}s. Timing out.")
                break
                
            time.sleep(0.01)
            
    except KeyboardInterrupt:
        print("\nStopping monitor.")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        if 'ser' in locals():
            ser.close()

if __name__ == "__main__":
    main()
