# Argon NCP (ESP32) firmware

The firmware for Argon NCP (ESP32).

## Prerequisites

- Ensure the xtensa-esp32 toolchain is in your PATH. It can be downloaded from [http://domoticx.com/sdk-esp32-xtensa-architecture-toolchain/](http://domoticx.com/sdk-esp32-xtensa-architecture-toolchain/)

## After pulling from the repo

After pulling changes from the repo, run 

```
git submodule update --init --recursive
```

## Building and flashing the application

```
$ make
$ make flash
```

## Updating the firmware using XModem

By default, the firmware runs an AT command interface on UART2 (TX: GPIO17, RX: GPIO16, HW flow control disabled).

Uploading a binary using `picocom` and [lrzsz](https://www.ohse.de/uwe/software/lrzsz.html) tools:

### Linux

```
$ picocom /dev/ttyUSB1 --baud 921600 --omap crcrlf --send-cmd "sx -k -b -X"
AT+FWUPD=<image size in bytes>
OK
<CTRL+a><CTRL-s>
*** file: /path/to/argon-ncp-firmware.bin
```

### macOS

```
$ picocom /dev/cu.xxxxxx --baud 921600 --omap crcrlf --send-cmd "lsx -k -b -X"
AT+FWUPD=<image size in bytes>
OK
<CTRL+a><CTRL-s>
*** file: /path/to/argon-ncp-firmware.bin
```
