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

## Building the factory image

```
$ make factory_bin
```

The combined 4MB factory binary will be written to `build/factory.bin`.

## Building modular firmware for flashing via BLE/OTA/Ymodem in Device OS

```
$make module
```

## Updating the version information

Please edit version.mk and perform a clean build. 


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

## AT commands

This firmware is based on AT application for ESP32 ESP-IDF (https://github.com/espressif/esp32-at) and at the moment only enables *Base* and *WiFi* AT command sets (https://www.espressif.com/sites/default/files/documentation/esp32_at_instruction_set_and_examples_en.pdf).

## Additional AT commands

The NCP firmware also implements a number of additional AT commands.

### AT+CGMR

Retrieves Argon NCP firmware version.

#### Format

```
AT+CGMR
```

Example:
```
> AT+CGMR
< 0.0.2
< OK
```

### AT+MVER

Retrieves Argon MCP firmware module version.

#### Format

```
AT+MVER
```

Example:
```
> AT+MVER
< 1
< OK
```

### AT+FWUPD

Initiates firmware update process and starts XModem server.

#### Format

```
AT+FWUPD=<binary size>
```

`<binary size>`: size of the binary to be transmitted using XModem in bytes.

Example:

```
> AT+FWUPD=123456
< +FWUPD: ONGOING
> (XModem transfer)
> OK
```

After the XModem transfer completes, the NCP will output the final result code. In case of OK, the NCP will restart, in case of an ERROR will continue execution.

### AT+GPIOC

Set/retrieve GPIO configuration.

#### Format

##### Test command

Retrieves the expected command parameters and their ranges.

```
AT+GPIOC=?
```

Example:

```
> AT+GPIOC=?
< +GPIOC: (0-39),(0-3),(0-2),(0-1)
< OK
```

##### Query command

Retrieves the current GPIO configuration for each pin.

_NOTE:_ Only the pins that were previously configured using `AT+GPIOC` will be present in the list.

```
AT+GPIOC?
+GPIOC: <pin0>,<gpio_mode>,<gpio_pull>
+GPIOC: <pin1>,<gpio_mode>,<gpio_pull>
...
```

Example:
```
> AT+GPIOC?
< +GPIOC: 2,2,0
< +GPIOC: 4,1,0
<
< OK
```

#### Setup command

Sets the GPIO configuration.

```
AT+GPIOC=<pin>,<gpio_mode>[,<gpio_pull>][,<gpio_default>]
```

- `<pin>`: pin number (0-39)
- `<gpio_mode>`: 0 - `DISABLED`, 1 - `INPUT`, 2 - `OUTPUT`, 3 - `OUTPUT_OD`
- `<gpio_pull>`: (optional) 0 - no pull (default), 1 - pull-down, 2 - pull-up
- `<gpio_default>`: (optional) default value to immediately set on a pin configured as `OUTPUT`. 0 - low, 1 - high

Example, configures pin number 4 as INPUT with pull-up
```
> AT+GPIOC=4,1,2
< OK
```

### AT+GPIOR

#### Query command

Retrieves the expected command parameters and their ranges.

```
AT+GPIOR=?
```

Example:
```
> AT+GPIOR=?
< +GPIOR: (0-39)
< OK
```

#### Setup command

Reads the current logical value on the specified GPIO pin (both INPUT and OUTPUT).

```
AT+GPIOR=<pin>
```

- `<pin>`: pin number (0-39)

Example:
```
> AT+GPIOR=4
< +GPIOR: 1
< OK
```

### AT+GPIOW

#### Query command

Retrieves the expected command parameters and their ranges.

```
AT+GPIOW=?
```

Example:
```
> AT+GPIOW=?
< +GPIOW: (0-33),(0-1)
< OK
```

#### Setup command

```
AT+GPIOW=<pin>,<level>
```

- `<pin>`: pin number (0-33)
- `<level>`: logical level to set, 0 - low, 1 - high

Example:

```
> AT+GPIOW=1,0
< OK
```

### AT+GETMAC

#### Query command

Retrieves the expected command parameters and their ranges.

```
AT+GETMAC=?
```

Example:
```
> AT+GETMAC=?
< +GETMAC: (0-3)
< OK
```

#### Setup command

```
AT+GETMAC=<type>
```

- `<type>`, 0 - WiFi Station, 1 - WiFi AP, 2 - Bluetooth, 3 - Ethernet

Example:
```
> AT+GETMAC=0
< +GETMAC: "24:0a:c4:10:9d:f4"
< OK
```
