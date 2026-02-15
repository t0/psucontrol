#!/usr/bin/env python3
"""
API functions for controlling t0-psu boards
"""

import json
import os
import requests
import sys


class PSUController():
    def __init__(self, hostname : str, timeout : float = 1):
        self.hostname = hostname
        self.timeout = timeout
        return


    def control_power(self, state : bool):
        """Turn PSU output on"""

        try:
            response = requests.post(
                f"http://{self.hostname}/psu-control",
                json={"output_state": state},
                headers={"Content-Type": "application/json"},
                timeout=self.timeout
            )
            response.raise_for_status()
            print(f"PSU output {'enabled' if state else 'disabled'}")
            return 0
        except requests.exceptions.RequestException as e:
            print(f"Failed to control PSU: {e}", file=sys.stderr)
            return 1


    def status(self, json_output : bool = False):
        """Get PSU status and telemetry"""

        try:
            response = requests.get(
                f"http://{self.hostname}/psu",
                timeout=self.timeout)
            response.raise_for_status()
            data = response.json()

            if json_output:
                print(json.dumps(data, indent=2))
            else:
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
            print(f"Failed to get PSU status: {e}", file=sys.stderr)
            return 1
        except (json.JSONDecodeError, KeyError) as e:
            print(f"Failed to parse PSU response: {e}", file=sys.stderr)
            return 1


    def flash(self):
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

