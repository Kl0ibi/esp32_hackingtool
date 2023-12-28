[24.12.2023]
- added ble spoof
- now all esp32 with atleast 4mb of flash are supported
- added command line interface
- fixed some bugs
- did some refactoring

[23.1.2023]
- Implemented functions to get the target uid from a nfc chip
- Provided bootloader and partition table bin files now its ready for flashing with esptool

[16.1.2023]
- implented function to get version revision of the pn532 (check that my spi functions are working)
- implented basic functions, send ack, waitReady ...

[15.1.2023]
- started with implementation of the nfc module (pn532)
- started with implementation of basic low level spi functions

[7.1.2023]
- added multiple new router captive portals for evil twin attack (there will be added more in the future)
- added facebook and apple shop captive portal (there will be added more in the future)
- added 2 new beacon spammer modes (duplicate wifi with same ssid and same/random mac) may be a good way to confuse devices
- added custom partition table
- fixed some bugs
- did some refactoring
