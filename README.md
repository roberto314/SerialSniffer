# SerialSniffer
A Board to to observe serial communication between two units.

Version 1.0

This is a small PCB with a STM32F401 "blackpill" Board ontop. It can measure the Baudrate with a timer and then switch to listen mode an relay the packets via USB to the host computer.

There are two hardware serial ports on the STM32 and one USB for shell and output.

Firmware is ChibiOS Link: https://www.chibios.org), Version is trunk from ChibiStudio 20. Compilation is done with ChibiStudio 20 on Linux.

### Used Software and OS ###

* Linux Mint (21.3 cinnamon) (Link: https://linuxmint.com/)
* Sublime Text 3 (Link: https://www.sublimetext.com/)
* Chibi Studio 20 (IDE for ChibiOS) (Link: https://www.chibios.org/dokuwiki/doku.php?id=chibios:products:chibistudio:start)