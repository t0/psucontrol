# Bugs and Quirks

These notes are (possibly) bugs in Zephyr that should be avoided for now.

## Network buffer leaks possibly tied to IPv6 or MDNS probe responder

mDNS / DNS-SD support can leak buffers, leading to out-of-memory problems after
some time elapses. The Zephyr console starts emitting the following messages:

    [00:07:49.427,000] <err> eth_stm32_hal: Failed to obtain RX buffer

"net allocs" command on the console shows:

    Network memory allocations

    memory          Status  Pool    Function alloc -> freed
    0x2403d684/1     used      RX   net_pkt_clone_internal():2106
    0x2403d5b8/1     used      RX   net_pkt_clone_internal():2106
    0x2403d640/1     used      RX   eth_stm32_rx():434
    0x2403d5fc/1     used      RX   net_pkt_clone_internal():2106
    0x2403d70c/1     used      RX   net_pkt_clone_internal():2106
    0x2403d6c8/1     used      RX   net_pkt_clone_internal():2106
    [...]

This problem seemed to go away when IPv6 was disabled. However, it also
occurred later on after an Ethernet unplug / plug test.

## Avahi, Caching, and Failure to resolve non-PTR fields

There is some problematic interaction between Zephyr's mDNS responder and
avahi. It looks like Zephyr responds to mDNS queries for PTR records and/or for
records matching the machine's hostname. Otherwise it does not respond to
TXT/SRV/A queries. I think (?) the response to a PTR query also includes these
other records, though, which would explain why avahi-browse is sometimes able
to resolve services and sometimes not (since these entries may be populated in
its cache, but a direct request for non-PTR records times out.)

See zephyr/subsys/net/lib/dns/mdns_responder.c.

## Lowest Common Denominator

These seem like reliable probes for a minimum functional subset of mDNS.

Resolving hostname:

$ avahi-resolve-host-name -4 t0-psu-0280e17fcea7.local
t0-psu-0280e17fcea7.local	192.168.10.13

Browsing service:

$ avahi-browse -rt _t0-psu._tcp
+   eth0 IPv4 t0-psu-0280e17fcea7                           _t0-psu._tcp         local
=   eth0 IPv4 t0-psu-0280e17fcea7                           _t0-psu._tcp         local
   hostname = [t0-psu-0280e17fcea7.local]
   address = [192.168.10.13]
   port = [80]
   txt = []

