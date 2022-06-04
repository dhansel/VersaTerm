# VersaTerm
A versatile DIY serial terminal.

![Labeled Board](hardware/pictures/board_labeled.jpg)
(more pictures [here](hardware/pictures/ReadMe.md))

## Highlights

- Instant-on/Instant-off - no waiting for OS to boot, no need to shut down safely
- Native HDMI and VGA video output (no conversion)
- Supports PS/2 and USB keyboards (including keyboards with integrated USB hubs)
- RS232 and TTL level serial input/output (TTL switchable between 3.3V and 5V)
- Wide range of baud rates: 50-921600 baud (presets and custom)
- Supports hardware (RTS/CTS) and software (XOn/XOff) flow control
- Can be powered via USB or 7-28V DC
- [Highly configurable](software/screenshots/settings.md), including user-uploadable fonts (bitmaps)
- Supports all [VT100 attributes](software/screenshots/vt100.md): bold/underline/blink/inverse
- Supports [16 ANSI colors](software/screenshots/vt100.md#ANSI-Colors)
- Decent VT100 control sequence support - [passes VTTest tests](software/screenshots/vttest.md) for 80-column VT52/VT100/VT102
- [PETSCII mode](software/screenshots/petscii.md) supports PETSCII character set and control characters, PETSCII (C64) font included
- Easy to DIY - vast majority of soldering is through-hole, firmware can be uploaded via USB (no special equipment required)

### Limitations

The terminal is powered by a Raspberry Pi Pico. The Pico is a microcontroller and does not have integrated graphics
capabilities. It can however still produce video signals (see [PicoDVI](https://github.com/Wren6991/PicoDVI) 
and [PicoVGA](https://github.com/Panda381/PicoVGA)). 
Some limitations for the terminal arise from the Pico's limited processing power:

- Max 80 columns per row (no 132 column support)
- Font characters must be 8 pixels wide (original VT100 was 10 pixels), height can be 8-16 pixels
- 16 Ansi colors only (no 8-bit or 24-bit color support)
- No smooth scrolling (emulated via delayed scrolling)

## Building VersaTerm

This is not a kit but building VersaTerm should be fairly easy:

- The Gerber file to produce the PCB is available [here](https://github.com/dhansel/VersaTerm/raw/main/hardware/PCB/VersaTermGerber.zip)
- See [here](hardware/PCB/ReadMe.md) for component sourcing and assembly tips
- STL files for an enclosure can be found [here](hardware/enclosure)
- Instructions on how to upload the firmware (simple USB connection) are [here](software/ReadMe.md)

## Screen Shots

See [here](software/screenshots/ReadMe.md) for some demonstrations of VersaTerm's capabilities
