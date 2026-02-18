#!/usr/bin/env python3
"""
Command-line interface for controlling t0-psu boards
"""

import argparse
import contextlib
import json
import os
import requests
import sys
import time
from typing import Dict, Any

from zeroconf import ServiceBrowser, ServiceListener, Zeroconf

from .api import PSUController


class PSUListener(ServiceListener):
    def __init__(self):
        self.services = {}

    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        if info := zc.get_service_info(type_, name):
            instance = name.replace(f'.{type_}', '')
            self.services[instance] = {
                'hostname': info.server,
                'addresses': info.parsed_addresses(),
                'port': info.port,
                'properties': info.properties
            }

    def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        self.update_service(zc, type_, name)

    def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        instance = name.replace(f'.{type_}', '')
        if instance in self.services:
            del self.services[instance]


def discover_psus(timeout: float = 1) -> Dict[str, Dict[str, Any]]:
    """Discover PSUs on the network via mDNS"""

    with contextlib.closing(Zeroconf()) as zc:
        listener = PSUListener()
        browser = ServiceBrowser(zc, "_t0-psu._tcp.local.", listener)

        time.sleep(timeout)

        return listener.services.copy()


def cmd_discover(args):
    """Discover PSUs on the network"""
    print(f"Discovering PSUs (timeout: {args.timeout}s)...")
    services = discover_psus(args.timeout)

    if not services:
        print("No PSUs found")
        return 1

    print(f"\nFound {len(services)} PSU(s):\n")
    for instance, info in services.items():
        hostname = info['hostname'].rstrip('.')
        addr = info['addresses'][0] if info['addresses'] else 'unknown'
        port = info['port']

        if port == 80:
            url = f"http://{hostname}"
        else:
            url = f"http://{hostname}:{port}"

        print(f"  {instance}")
        print(f"    URL:     {url}")
        print(f"    Address: {addr}")
        print()

    return 0


def cmd_flash(args):
    """Flash firmware to PSU controller"""
    import subprocess

    steps = []

    if not os.path.exists('.west'):
        steps.append(("Initializing west workspace", ['west', 'init']))

    steps.extend([
        ("Updating west workspace", ['west', 'update']),
        ("Exporting Zephyr environment", ['west', 'zephyr-export']),
        ("Installing packages", ['west', 'packages', 'pip', '--install']),
        ("Building firmware", ['west', 'build', '-b', 'nucleo_h723zg', '.']),
        ("Flashing firmware", ['west', 'flash']),
    ])
    
    for msg, cmd in steps:
        print(f"{msg}...")
        result = subprocess.run([sys.executable, '-m'] + cmd)
        if result.returncode != 0:
            print(f"Failed: {msg}", file=sys.stderr)
            if msg == "Building firmware":
                print(f"For error during 'Building Firmware' --> Note that the working directory is assumed to be the parent `psucontrol` directory.", file=sys.stderr)
            return result.returncode

    return 0


def main():
    parser = argparse.ArgumentParser(
        description='Command-line interface for controlling t0-psu boards',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s -d
  %(prog)s t0-psu-0280e17fcea7.local --on
  %(prog)s 192.168.10.13 --off
  %(prog)s 192.168.10.13 -s
  %(prog)s http://192.168.10.13 --status --json
  %(prog)s --flash
        """.strip()
    )

    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument(
        '-d', '--discover',
        action='store_true',
        help='Discover PSUs on the network via mDNS'
    )
    action.add_argument(
        '--on',
        action='store_true',
        help='Turn PSU output on'
    )
    action.add_argument(
        '--off',
        action='store_true',
        help='Turn PSU output off'
    )
    action.add_argument(
        '-s', '--status',
        action='store_true',
        help='Get PSU status and telemetry'
    )
    action.add_argument(
        '--flash',
        action='store_true',
        help='Flash firmware to PSU controller (requires STLink connection)'
    )

    parser.add_argument(
        'target',
        nargs='?',
        help='PSU target (hostname, IP, or URL) - not needed for --discover or --flash'
    )

    parser.add_argument(
        '-t', '--timeout',
        type=float,
        default=None,
        help='Request timeout in seconds (default: 1s for discover, 5s for other operations)'
    )
    parser.add_argument(
        '-j', '--json',
        action='store_true',
        help='Output raw JSON instead of formatted text (only for --status)'
    )
    parser.add_argument(
        '--build-dir',
        type=str,
        default=None,
        help='Build directory for west flash (only for --flash)'
    )

    args = parser.parse_args()

    if args.discover:
        if args.target:
            parser.error("--discover does not take a target argument")
        if args.timeout is None:
            args.timeout = 1
        return cmd_discover(args)
    elif args.flash:
        if args.target:
            parser.error("--flash does not take a target argument")
        return cmd_flash(args)
    else:
        if not args.target:
            parser.error("target is required for --on, --off, and --status")
        if args.timeout is None:
            args.timeout = 5.0

        psu = PSUController(hostname = args.target, timeout = args.timeout)

        if args.on:
            try:
                psu.control_power(state = True)
                print("PSU output enabled")
                return 0
            except requests.exceptions.RequestException as e:
                print(f"Failed to control PSU: {e}", file=sys.stderr)
                return 1
        elif args.off:
            try:
                psu.control_power(state = False)
                print("PSU output disabled")
                return 0
            except requests.exceptions.RequestException as e:
                print(f"Failed to control PSU: {e}", file=sys.stderr)
                return 1
        elif args.status:
            try:
                if args.json:
                    data = psu.status(json_output = True)
                else:
                    data = psu.status(json_output = False)

                    vout = data.get('vout', 0)
                    iout = data.get('iout', 0)

                    print(f"PSU Status:")
                    print(f"  Output:       {'ON' if data.get('output_on') else 'OFF'}")
                    print(f"  Input:        {data.get('vin', 0):.2f} V")
                    print(f"  Output:       {vout:.2f} V @ {iout:.3f} A ({iout*vout:.1f} W)")
                    print(f"  Temperature:  {data.get('temp', 0):.1f} °C")
                    print(f"  Fan speed:    {data.get('fan_rpm', 0)} RPM")
                return 0
            except requests.exceptions.RequestException as e:
                print(f"Failed to control PSU: {e}", file=sys.stderr)
                return 1

    return 1


if __name__ == "__main__":
    sys.exit(main())
