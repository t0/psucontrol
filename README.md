# Power Supply Controller

This project exposes Bel Power TET2200-12-086NA power supplies as HTTP
endpoints, allowing telemetry collection and on/off control.

## Ingredients

- Zephyr RTOS (https://www.zephyrproject.org/)
- PMBus / I2C
- mDNS / DNS-SD

## How to Build (physical unit)

The necessary datasheet and schematic .pdfs are available in docs/. Bel Power
also has a PMBus application note for this part that describes register support
and format.

### Hardware Required

- Bel Power TET2200-12-086NA and connector (FCI Electronics 10053363-200LF)
- STMicro Nucleo-H723ZG board
- 4.7kOhm pull-up resistors (2x)
- Generic lab stuff (wire, solder, Ethernet and USB cabling)

### Wiring and Configuration

#### TET2200

- Connect PWR Return (P13-24 on either row) to the signal GND (P29).

#### TET2200 to Nucleo

Connect the TET2200 to the Nucleo as follows:

- PSON_L (B29) -> CN7 pin 9
- PS_KILL (B30) -> CN8 pin 11 (GND)
- SCL (A30) -> CN7 pin 2
- SDA (A32) -> CN7 pin 4
- 12 VSTBY (A26) -> CN8 pin 15

#### Nucleo

- Configure JP2 to short VIN to 5VPWR to enable the on-board LDO (U12). This
  LDO has just become a heater (it's a good thing the Nucleo doesn't consume
  much power.)
- Add a 4.7kOhm pull-up resistor from SDA to VREFP. The SDA pin is available at
  pin 3 of CN12 as a through-hole pad.
- Add a 4.7kOhm pull-up resistor from SCL to VREFP. The SCL pin is available at
  pin 5 of CN12 as a through-hole pad.

For the last two, VREFP is available on pin 6 of CN7. VREFP is just a 3.3V
reference and may be more convenient elsewhere on the board.

## How to Build (firmware)

On a Linux box with the ["uv" tool](https://docs.astral.sh/uv/) installed:

```bash
# Install dependencies and the psucontrol CLI tool
$ uv sync

# Build and flash firmware (requires STLink connected via USB)
$ uv run psucontrol --flash
```

The `--flash` command will automatically:
- Set up the west workspace
- Export Zephyr environment variables
- Install required packages
- Build the firmware
- Flash to the connected device

Alternatively, you can use west commands directly for more control:

```bash
# Just build without flashing
$ uv run west build -b nucleo_h723zg .

# Flash previously built firmware
$ uv run west flash
```

## Using the PSU Controller

### Command-Line Interface

The `psucontrol` command provides a command-line interface for discovering and controlling PSUs:

```bash
# Discover PSUs on the network
$ psucontrol -d
Discovering PSUs (timeout: 1.0s)...

Found 1 PSU(s):

  t0-psu-0280e17fcea7
    URL:     http://t0-psu-0280e17fcea7.local
    Address: 192.168.10.13

# Turn PSU output on
$ psucontrol t0-psu-0280e17fcea7.local --on
PSU output enabled

# Turn PSU output off
$ psucontrol 192.168.10.13 --off
PSU output disabled

# Get PSU status
$ psucontrol 192.168.10.13 -s
PSU Status:
  Output:       OFF
  Input:        229.50 V
  Output:       12.08 V @ 0.125 A
  Power:        1.5 W
  Temperature:  32.5 °C
  Fan speed:    2400 RPM

# Get raw JSON output
$ psucontrol 192.168.10.13 --status --json
{
  "vin": 229.50,
  "vout": 12.08,
  "iout": 0.125,
  "temp": 32.5,
  "fan_rpm": 2400,
  "output_on": false
}

# Flash firmware (requires STLink connected via USB)
$ psucontrol --flash
```

### Web Interface

You can also open the PSU URL in your web browser to see the web interface with real-time telemetry and control.

### Discovery with Avahi

Alternately, you can resolve the service using avahi:

```bash
$ avahi-browse -rt _t0-psu._tcp
+   eth0 IPv4 t0-psu-0280e17fcea7                           _t0-psu._tcp         local
=   eth0 IPv4 t0-psu-0280e17fcea7                           _t0-psu._tcp         local
   hostname = [t0-psu-0280e17fcea7.local]
   address = [192.168.10.13]
   port = [80]
   txt = []
```

## Debugging

If something went wrong, the Zephyr console is generally a good first step. You can access this via

```bash
$ screen /dev/ttyACM0 115200
```
