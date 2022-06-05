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
- Supports all [VT100 attributes](software/screenshots/vt100.md): bold/underline/blink/inverse/double width/double height
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
- STL files for 3d printing an enclosure can be found [here](hardware/enclosure)
- Instructions on how to upload the firmware binary (via USB micro cable, no extra hardware needed) are [here](software/ReadMe.md)

## Using VersaTerm

### Settings menu and multiple configurations.

The settings menu can be entered by pressing the F12 keyboard key and navigated
using the cursor keys.

The "Manage Configurations" sub-menu provides 10 slots to save configurations
(e.g. if you want to use VersTerm with different computers).
- To save the current settings to the selected cofiguration slot, press "S"
- To make the selected configuration slot the startup default, press "*"
- To give the selected configuration slot a name, press "N"

You can switch between configurations via either of these methods:
- On startup or RESET, hold down the F1-F10 keyboard keys to select a configuration
- While VersaTerm is on, press CTRL and F1-F10 to load a configuration
- Press CTRL+F12 to open a quick-select menu that shows your configurations and their names

### Video output selection (HDMI/VGA)

VersaTerm can produce either VGA or HDMI output but not both at the same time. On startup, VersaTerm detects whether
a HDMI monitor is connected (via the HDMI "Hot Plug Detect" signal pin). If a HDMI monitor is detected then HDMI
output is produced, otherwise VGA. You can disable auto-detect and directly specify the output type in the "Screen"
settings menu. If your HDMI monitor is not detected at all, the HDMI output can be forced by doing the following:

- Connect a keyboard
- Hold down the RESET and DEFAULTS buttons on the side of the VersaTerm PCB
- Hold down the CTRL key on the keyboard
- Release RESET

Doing so will use default settings but disable auto-detect and instead force HDMI output. 
Once you have a monitor image you can enter settings,  fix the output type to HDMI and save 
the settings so HDMI is always used automatically.

### USB mode

The Raspberry Pi Pico has only one USB port which can function either as a USB host or device.
If the Pico is connected to another computer using the USB micro socket on the Pico board then
VersaTerm will detect that at startup and run the USB port as a device. Otherwise it assumes
that the USB port should be run as a host and allow a USB keyboard to be connected.

Do not connect the Pico to another computer **and** plug in a USB keyboard at the same time. 
Doing so won't break anything but the USB port won't function properly.

If the USB port is plugged into another computer (i.e. used as a device) then VersaTerm should
be recognized by the computer as a USB CDC (serial) device. In that case there are three operating
modes that can be selected in the USB settings menu:
- *Serial.* In this mode VersaTerm views the USB connection as a secondary serial connection. Any data received is treated just the same as data received on the main serial connection and keypresses are sent to both the main and the USB connection.
- *Feed-through.* In this mode (which is the default) VersaTerm forwards any data received on the main serial connection to USB and vice versa. This way VersaTerm can be used as a USB-to-serial converter.
- *Feed-through (terminal disabled).* Similar to the basic feed-through mode but VersaTerm won't process any input it receives from the main serial connection. This can be used to transfer (binary) data between the main serial connection and the USB serial connection without messing up the terminal display.

### Resetting the terminal

The terminal can be reset by pressing the RESET button on the side of the PCB. 

If you get yourself into a situation where VersaTerm will not work because of invalid default settings
(e.g. set to force HDMI output but you only have a VGA monitor), hold down the DEFAULTS button
(next to RESET) on the side of the PCB while pressing and releasing RESET. This will re-start
VersaTerm with the default settings.

## Screen Shots

See [here](software/screenshots/ReadMe.md) for some demonstrations of VersaTerm's capabilities
