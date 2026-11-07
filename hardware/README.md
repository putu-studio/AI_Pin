# AI Pin carrier board

A two-layer carrier PCB for the Seeed XIAO ESP32-S3 Sense that adds everything the pin needs to run on a battery: LiPo charging over USB-C, a power switch, a button and the display connector.

Designed in KiCad 10. Open `aipin-carrier.kicad_pro`.

## What's on the board

| Block | Details |
|---|---|
| Charger | TP4056, R<sub>PROG</sub> = 2.4 kΩ for about 500 mA, suited to an 1100 mAh LiPo |
| USB-C | 5.1 kΩ CC pull-downs so USB-C chargers supply 5 V, TVS diode on VBUS |
| Power path | Battery → charger → slide switch → XIAO |
| Battery sense | Resistor divider into an ADC pin |
| User input | One push button |
| Display | 1.69" ST7789 module (30 × 47 mm) |

## Files

| Path | Contents |
|---|---|
| `aipin-carrier.kicad_sch` | Schematic |
| `aipin-carrier.kicad_pcb` | Board layout |
| `lib/aipin.pretty/` | Custom footprints (switches and buttons) |
| `production/` | Gerbers and drill files (`aipin-carrier.zip`), BOM, pick-and-place and netlist |

## Ordering

The `production/` files were generated with the KiCad Fabrication Toolkit plugin and are ready to upload to [JLCPCB](https://jlcpcb.com). Upload `aipin-carrier.zip` for the bare board. For assembly, add `bom.csv` and `positions.csv`.

Before ordering, check that every BOM line's LCSC part number matches the value in the schematic.

## Reference

- [XIAO ESP32-S3 Sense documentation](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) (pinout and schematic)
