#!/usr/bin/env python3
"""
Discover t0-psu boards on the network via mDNS/DNS-SD
"""

from zeroconf import ServiceBrowser, ServiceListener, Zeroconf
import time
import contextlib
import argparse

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


def main():
    parser = argparse.ArgumentParser(
        description='Discover t0-psu boards on the network via mDNS/DNS-SD'
    )
    parser.add_argument('-t', '--timeout', type=float, default=1,
                        help='Browse timeout in seconds (default: 1)')
    args = parser.parse_args()

    with contextlib.closing(Zeroconf()) as zc:
        listener = PSUListener()
        browser = ServiceBrowser(zc, "_t0-psu._tcp.local.", listener)

        # Pause for service discovery
        time.sleep(args.timeout)

        for instance, info in listener.services.items():
            hostname = info['hostname'][:-1]  # drop trailing '.'
            if info['port']==80:
                print(f"http://{hostname} ({info['addresses'][0]})")
            else:
                print(f"http://{hostname}:{info['port']} ({info['addresses'][0]})")


if __name__ == "__main__":
    main()
