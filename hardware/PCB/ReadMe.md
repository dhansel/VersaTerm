# Parts 

I sourced most of the parts from DigiKey. The VersaTermBOM.xlsx file in this folder includes all the parts and their part numbers.
You can also click the link below to automatically create a shopping cart at DigiKey that contains most of the parts:<BR>
https://www.digikey.com/short/d0qw14h1

The the following necessary parts are currently not available at DigiKey and have to be ordered separately:
  - Raspberry Pi Pico: not sold at DigiKey, available at AdaFruit, part #4883
  - 2N7000 MOSFET (need 7): currently (May 2022) on backorder at DigiKey. Available on EBay from various sellers.

If you want to use the provided [3d-printable enclosure](../enclosure), you will need four machine screws #4-40 with length 22mm (7/8") below the head and matching
#4-40 nuts. DigiKey part #335-1089-ND and #36-9600-ND will fit.
  
# Assembly tips
  
The vast majority of solder points are through-hole and easy to solder, except for the following:
  - Diodes D1 and D2 are surface-mount devices but the diodes themselves are fairly large. I have no SMD soldering experience and had no problem soldering them.
  - For the Raspberry Pi Pico, solder female header pins onto the PCB and then male header pins to the Pi Pico. That will make it easier to debug issues later 
  or to replace the Pico if that becomes necessary.
  - I also used male/female headers to connect the Max3232
  - The Raspberry Pi Pico does not route the USB data signals to its pin headers. However, there are two test points (TP2 and TP3) on the bottom of the Pi Pico 
  on which the USB data signals can be accessed. Solder in a short wire from TP2 on the Pi Pico board to TP2 on the VersaTerm PCB. Same for TP3.
  - While the HDMI connector is through-hole, its pins are very closely spaced. I definitely recommend using a multimeter to check there are no solder bridges 
  between the pins after soldering.
  - When soldering the male Molex connector for the TTL serial connection to the board, do not stick it into the board as far as it will go, otherwise the 
  female connector will not fit afterwards. I recommend soldering the male connector while the female one is plugged in, that way there is definitely enough space.

![board_top](../pictures/board_top.jpg)
