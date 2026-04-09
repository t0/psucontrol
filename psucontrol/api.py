#!/usr/bin/env python3
"""
API functions for controlling t0-psu boards
"""

import contextlib
import requests
import time
import zeroconf


class PSUController:
    def __init__(self, hostname: str, timeout: float = 1):
        self.hostname = hostname
        self.timeout = timeout

    def _post(self, method, **kwargs):
        response = requests.post(
            f"http://{self.hostname}/{method}", json=kwargs, timeout=self.timeout
        )
        response.raise_for_status()

    def _get(self, method):
        response = requests.get(
            f"http://{self.hostname}/{method}", timeout=self.timeout
        )
        response.raise_for_status()
        return response.json()

    def set_power(self, state: bool):
        """Turn PSU output on"""
        return self._post("psu-control", output_state=state)

    def get_status(self):
        """Get PSU status and telemetry"""
        return self._get("psu")

    def clear_faults(self):
        """Clear PSU faults"""
        return self._post("psu-clear-faults")


def discover(timeout: float = 1) -> list[PSUController]:
    class PSUListener(zeroconf.ServiceListener):
        def __init__(self):
            self.services = {}

        def update_service(self, zc: zeroconf.Zeroconf, type_: str, name: str) -> None:
            if info := zc.get_service_info(type_, name):
                instance = name.replace(f".{type_}", "")
                self.services[instance] = {
                    "hostname": info.server,
                    "addresses": info.parsed_addresses(),
                    "port": info.port,
                    "properties": info.properties,
                }

        def add_service(self, zc: zeroconf.Zeroconf, type_: str, name: str) -> None:
            self.update_service(zc, type_, name)

        def remove_service(self, zc: zeroconf.Zeroconf, type_: str, name: str) -> None:
            instance = name.replace(f".{type_}", "")
            if instance in self.services:
                del self.services[instance]

    with contextlib.closing(zeroconf.Zeroconf()) as zc:
        listener = PSUListener()
        browser = zeroconf.ServiceBrowser(zc, "_t0-psu._tcp.local.", listener)

        time.sleep(timeout)

        cs = []
        for instance, info in listener.services.items():
            cs.append(PSUController(hostname=info["hostname"].rstrip(".")))
        return cs
