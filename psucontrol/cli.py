#!/usr/bin/env python3
"""
Command-line interface for controlling t0-psu boards
"""

import argparse
import json
import os
import requests
import sys
import time
import serial
from datetime import datetime
import re

from .api import PSUController, discover


def cmd_discover(args):
    """Discover PSUs on the network"""
    print(f"Discovering PSUs (timeout: {args.timeout}s)...")
    controllers = discover(args.timeout)

    if not controllers:
        print("No PSUs found")
        return 1

    print(f"\nFound {len(controllers)} PSU(s):\n")
    for pc in controllers:
        hostname = pc.hostname

        print(f"* {pc.hostname} (http://{pc.hostname})")

    return 0


def cmd_flash(args):
    """Flash firmware to PSU controller"""
    import subprocess

    steps = []

    if not os.path.exists(".west"):
        steps.append(("Initializing west workspace", ["west", "init"]))

    steps.extend(
        [
            ("Updating west workspace", ["west", "update"]),
            ("Exporting Zephyr environment", ["west", "zephyr-export"]),
            ("Installing packages", ["west", "packages", "pip", "--install"]),
            ("Installing Zephyr SDK", ["west", "sdk", "install", "-t", "arm-zephyr-eabi"]),
            ("Building firmware", ["west", "build", "-b", "nucleo_h723zg", "."]),
            ("Flashing firmware", ["west", "flash"]),
        ]
    )

    for msg, cmd in steps:
        print(f"{msg}...")
        result = subprocess.run([sys.executable, "-m"] + cmd)
        if result.returncode != 0:
            print(f"Failed: {msg}", file=sys.stderr)
            if msg == "Building firmware":
                print(
                    f"For error during 'Building Firmware' --> Note that the working directory is assumed to be the parent `psucontrol` directory.",
                    file=sys.stderr,
                )
            return result.returncode

    return 0


def serial_log():
    port = "/dev/ttyACM0"
    baud = 115200

    # Regex to remove ANSI escape sequences
    ansi_escape = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")

    log_dir = os.path.expanduser("~/PSUCONTROL_LOGS")
    os.makedirs(log_dir, exist_ok=True)

    logfile = f"{log_dir}/serial_{datetime.now().strftime('%Y%m%d__%H-%M-%S')}.log"
    try:
        with serial.Serial(port, baud, timeout=1) as ser, open(logfile, "w") as f:
            print(f"Logging to {logfile} (Ctrl+C to stop)\n")
            try:
                while True:
                    raw = ser.readline().decode(errors="ignore")
                    line = ansi_escape.sub("", raw)  # Remove ANSI codes
                    if line:
                        print(line, end="")
                        f.write(line)
            except KeyboardInterrupt:
                print("\nStopped.")
    except serial.serialutil.SerialException as e:
        raise RuntimeError(f"No serial connection found: {e}")


def network_log(args):
    log_dir = os.path.expanduser("~/PSUCONTROL_LOGS")
    os.makedirs(log_dir, exist_ok=True)

    logfile = f"{log_dir}/psu_{datetime.now().strftime('%Y%m%d_%H%M%S')}.jsonl"

    print(f"Logging PSU telemetry to {logfile}")

    text = ""

    try:
        with open(logfile, "w") as f:
            while True:
                try:
                    response = requests.get(
                        f"http://{args.target}/psu", timeout=1.0
                    )  # timeout of 1s for logging
                    response.raise_for_status()
                    text = response.text
                    data = response.json()

                    entry = {
                        "host_time": datetime.now().isoformat(),
                        "device": args.target,
                        **data,
                    }

                    print(entry)
                    f.write(json.dumps(entry) + "\n")
                    f.flush()

                except Exception as e:
                    print(f"Error: raw response: {text},\nerror: {e}")

                interval = 1 / args.polling
                time.sleep(interval)

    except KeyboardInterrupt:
        print("\nStopped.")


def main():
    sys.excepthook = lambda typ, val, tb: print(f"Error: {val}", file=sys.stderr)

    parser = argparse.ArgumentParser(
        description="Command-line interface for controlling t0-psu boards",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s -d
  %(prog)s t0-psu-0280e17fcea7.local --on
  %(prog)s 192.168.10.13 --off
  %(prog)s 192.168.10.13 -s
  %(prog)s http://192.168.10.13 --status --json
  %(prog)s --flash
        """.strip(),
    )

    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument(
        "--serial_log",
        action="store_true",
        help="Log serial output. Serial connection required!",
    )
    action.add_argument("--log", action="store_true", help="Log PSU data")
    action.add_argument(
        "-p",
        "--polling",
        type=float,
        default=1.0,
        help="Polling time in seconds (default: 1s)",
    )

    action.add_argument(
        "-d",
        "--discover",
        action="store_true",
        help="Discover PSUs on the network via mDNS",
    )
    action.add_argument("--on", action="store_true", help="Turn PSU output on")
    action.add_argument("--off", action="store_true", help="Turn PSU output off")
    action.add_argument(
        "-s", "--status", action="store_true", help="Get PSU status and telemetry"
    )
    action.add_argument(
        "--flash",
        action="store_true",
        help="Flash firmware to PSU controller (requires STLink connection)",
    )
    action.add_argument("--clear-faults", action="store_true", help="Clear PSU faults")

    parser.add_argument(
        "target",
        nargs="?",
        help="PSU target (hostname, IP, or URL) - not needed for --discover or --flash",
    )

    parser.add_argument(
        "-t",
        "--timeout",
        type=float,
        default=None,
        help="Request timeout in seconds (default: 1s for discover, 5s for other operations)",
    )
    parser.add_argument(
        "-j",
        "--json",
        action="store_true",
        help="Output raw JSON instead of formatted text (only for --status)",
    )
    parser.add_argument(
        "--build-dir",
        type=str,
        default=None,
        help="Build directory for west flash (only for --flash)",
    )

    args = parser.parse_args()

    if args.serial_log:
        serial_log()
    elif args.discover:
        if args.target:
            parser.error("--discover does not take a target argument")
        if args.timeout is None:
            args.timeout = 1
        return cmd_discover(args)
    elif args.flash:
        if args.target:
            parser.error("--flash does not take a target argument")
        return cmd_flash(args)

    if not args.target:
        parser.error(
            "target is required for --on, --off, --status, --log, and --clear-faults"
        )

    if args.timeout is None:
        args.timeout = 5.0

    psu = PSUController(hostname=args.target, timeout=args.timeout)

    if args.on or args.off:
        psu.set_power(args.on)

    elif args.status:
        data = psu.get_status()
        if args.json:
            print(json.dumps(data, indent=2))
        else:
            vout = data.get("vout", 0)
            iout = data.get("iout", 0)

            print(f"PSU Status:")
            print(f"  Output:       {'ON' if data.get('output_on') else 'OFF'}")
            print(f"  Input:        {data.get('vin', 0):.2f} V")
            print(f"  Output:       {vout:.2f} V @ {iout:.3f} A ({iout*vout:.1f} W)")
            print(f"  Inlet Temp:   {data.get('temp_inlet', 0):.1f} °C")
            print(f"  Outlet Temp:  {data.get('temp_outlet', 0):.1f} °C")
            print(f"  Fan speed:    {data.get('fan_rpm', 0)} RPM")

    elif args.log:
        network_log(args)
    elif args.clear_faults:
        return psu.clear_faults()
