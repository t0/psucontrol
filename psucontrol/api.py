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
            return state
        except requests.exceptions.RequestException as e:
            raise requests.exceptions.RequestException(f"Failed to control PSU: {e}")


    def status(self, json_output : bool = False):
        """Get PSU status and telemetry"""

        try:
            response = requests.get(
                f"http://{self.hostname}/psu",
                timeout=self.timeout)
            response.raise_for_status()
            data = response.json()

            if json_output:
                return json.dumps(data, indent=2)
            else:
                return data
        except requests.exceptions.RequestException as e:
            raise requests.exceptions.RequestException(f"Failed to get PSU status: {e}")
        except json.JSONDecodeError as e:
            raise json.JSONDecodeError(f"Failed to parse PSU response: {e}")
        except KeyError as e:
            raise KeyError(f"Failed to parse PSU response: {e}")
        