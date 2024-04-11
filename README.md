# ST7789 Driver for MicroPython

This driver is based on [russhughes' st7789_mpy driver.](https://github.com/russhughes/st7789_mpy).

Supports 240x320, 170x320, 240x280, 240x240, 135x240, 128x160 and 128x128 displays.

## Display Configuration

Some displays may use a BGR color order or inverted colors.

### Color Modes

You can test for the correct color order needed by a display by filling it with
the `st7789.RED` color and observing the actual color displayed.

- If the displayed color is RED, the settings are correct.
- If the displayed color is BLUE, `color_order` should be `st7789.BGR`.
- If the displayed color is YELLOW, `inversion_mode` should be `True.`
- If the displayed color is CYAN, `color_order` should be `st7789.BGR` and
  `inversion_mode` should be `True.`

### colstart and rowstart

Some displays have a frame buffer memory larger than the physical display
matrix. In these cases, the driver must be configured with the position of the
first physical column and row pixels relative to the frame buffer. Each
rotation setting of the display may require different colstart and rowstart
values.

The driver automatically sets the `colstart` and `rowstart` values for common
135x240, 240x240, 170x320 and 240x320 displays. If the default values do not work for
your display, these values can be overridden using the `offsets` method. The
`offsets` method should be called after any `rotation` method calls.

## Overview

This is a driver for MicroPython to handle cheap displays based on the ST7789
chip. The driver is written in C. Firmware is provided for ESP32, ESP32 with SPIRAM,
pyboard1.1, and Raspberry Pi Pico devices.

<p align="center">
  <img src="https://raw.githubusercontent.com/blockcodelab/st7789_mpy/master/docs/ST7789.jpg" alt="ST7789 display photo"/>
</p>

# Setup MicroPython Build Environment in Ubuntu 20.04.2

See the MicroPython
[README.md](https://github.com/micropython/micropython/blob/master/ports/esp32/README.md#setting-up-esp-idf-and-the-build-environment)
if you run into any build issues not directly related to the st7789 driver. The
recommended MicroPython build instructions may have changed.

Update and upgrade Ubuntu using apt-get if you are using a new install of
Ubuntu or the Windows Subsystem for Linux.

```bash
sudo apt-get -y update
sudo apt-get -y upgrade
```

Use apt-get to install the required build tools.

```bash
sudo apt-get -y install build-essential libffi-dev git pkg-config cmake virtualenv python3-pip python3-virtualenv
```

### Install a compatible esp-idf SDK

The MicroPython README.md states: "The ESP-IDF changes quickly, and MicroPython
only supports certain versions. Currently, MicroPython supports v4.0.2, v4.1.1,
and v4.2 although other IDF v4 versions may also work." I have had good luck
using IDF v4.4

Clone the esp-idf SDK repo -- this usually takes several minutes.

```bash
git clone -b v4.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf/
git pull
```

If you already have a copy of the IDF, you can checkout a version compatible
with MicroPython and update the submodules using:

```bash
$ cd esp-idf
$ git checkout v4.4
$ git submodule update --init --recursive
```

Install the esp-idf SDK.

```bash
./install.sh
```

Source the esp-idf export.sh script to set the required environment variables.
You must source the file and not run it using ./export.sh. You will need to
source this file before compiling MicroPython.

```bash
source export.sh
cd ..
```

Clone the MicroPython repo.

```bash
git clone https://github.com/micropython/micropython.git
```

Clone the st7789 driver repo.

```bash
git clone https://github.com/russhughes/st7789_mpy.git
```

Update the git submodules and compile the MicroPython cross-compiler

```bash
cd micropython/
git submodule update --init
cd mpy-cross/
make
cd ..
cd ports/esp32
```

```bash
make USER_C_MODULES=../../../../st7789_mpy/st7789/micropython.cmake
```

Erase and flash the firmware to your device. Set PORT= to the ESP32's usb
serial port. I could not get the USB serial port to work under the Windows
Subsystem (WSL2) for Linux. If you have the same issue, you can copy the
firmware.bin file and use the Windows esptool.py to flash your device.

```bash
make USER_C_MODULES=../../../../st7789_mpy/st7789/micropython.cmake PORT=/dev/ttyUSB0 erase
make USER_C_MODULES=../../../../st7789_mpy/st7789/micropython.cmake PORT=/dev/ttyUSB0 deploy
```

The firmware.bin file will be in the build-GENERIC directory. To flash using
the python esptool.py utility. Use pip3 to install the esptool if it's not
already installed.

```bash
pip3 install esptool
```

Set PORT= to the ESP32's USB serial port

```bash
esptool.py --port COM3 erase_flash
esptool.py --chip esp32 --port COM3 write_flash -z 0x1000 firmware.bin
```

## CMake building instructions for MicroPython 1.14 and later

for ESP32:

    `$ cd micropython/ports/esp32`

And then compile the module with specified USER_C_MODULES dir.

    `$ make USER_C_MODULES=../../../../st7789_mpy/st7789/micropython.cmake`

for Raspberry Pi PICO:

    `$ cd micropython/ports/rp2`

And then compile the module with specified USER_C_MODULES dir.

    `$ make USER_C_MODULES=../../../st7789_mpy/st7789/micropython.cmake`

## Working examples

This module was tested on ESP32, STM32 based pyboard v1.1, and the Raspberry Pi
Pico. You have to provide an `SPI` object and the pin to use for the `dc' input
of the screen.

    # ESP32 Example
    # To use baudrates above 26.6MHz you must use my firmware or modify the micropython
    # source code to increase the SPI baudrate limit by adding SPI_DEVICE_NO_DUMMY to the
    # .flag member of the spi_device_interface_config_t struct in the machine_hw_spi_init_internal.c
    # file.  Not doing so will cause the ESP32 to crash if you use a baudrate that is too high.

    import machine
    import st7789
    spi = machine.SPI(2, baudrate=40000000, polarity=1, sck=machine.Pin(18), mosi=machine.Pin(23))
    display = st7789.ST7789(spi, 240, 240, reset=machine.Pin(4, machine.Pin.OUT), dc=machine.Pin(2, machine.Pin.OUT))
    display.init()

## Methods

- `st7789.ST7789(spi, width, height, dc, reset, cs, backlight, rotations, rotation, custom_init, color_order, inversion, options)`

  ### Required positional arguments:

  - `spi` spi device
  - `width` display width
  - `height` display height

  ### Required keyword arguments:

  - `dc` sets the pin connected to the display data/command selection input.
    This parameter is always required.

  ### Optional keyword arguments:

  - `reset` sets the pin connected to the display's hardware reset input. If
    the displays reset pin is tied high, the `reset` parameter is not
    required.

  - `cs` sets the pin connected to the displays chip select input. If the
    display's CS pin is tied low, the display must be the only device
    connected to the SPI port. The display will always be the selected
    device, and the `cs` parameter is not required.

  - `backlight` sets the pin connected to the display's backlight enable
    input. The display's backlight input can often be left floating or
    disconnected as the backlight on some displays is always powered on and
    cannot be turned off.

  - `rotations` sets the orientation table. The orientation table is a list
    of tuples for each `rotation` used to set the MADCTL register, display width,
    display height, start_x, and start_y values.

    Default `rotations` are included for the following st7789 and st7735
    display sizes:

    | Display | Default Orientation Tables                                                                               |
    | ------- | -------------------------------------------------------------------------------------------------------- |
    | 240x320 | [(0x00, 240, 320, 0, 0), (0x60, 320, 240, 0, 0), (0xc0, 240, 320, 0, 0), (0xa0, 320, 240, 0, 0)]         |
    | 170x320 | [(0x00, 170, 320, 35, 0), (0x60, 320, 170, 0, 35), (0xc0, 170, 320, 35, 0), (0xa0, 320, 170, 0, 35)]     |
    | 240x280 | [(0x00, 240, 280, 0, 20), (0x60, 280, 240, 20, 0), (0xc0, 240, 280, 0, 20), (0xa0, 280, 240, 20, 0)]     |
    | 240x240 | [(0x00, 240, 240, 0, 0), (0x60, 240, 240, 0, 0), (0xc0, 240, 240, 0, 80), (0xa0, 240, 240, 80, 0)]       |
    | 135x240 | [(0x00, 135, 240, 52, 40), (0x60, 240, 135, 40, 53), (0xc0, 135, 240, 53, 40), (0xa0, 240, 135, 40, 52)] |
    | 128x160 | [(0x00, 128, 160, 0, 0), (0x60, 160, 128, 0, 0), (0xc0, 128, 160, 0, 0), (0xa0, 160, 128, 0, 0)]         |
    | 128x128 | [(0x00, 128, 128, 2, 1), (0x60, 128, 128, 1, 2), (0xc0, 128, 128, 2, 3), (0xa0, 128, 128, 3, 2)]         |
    | other   | [(0x00, width, height, 0, 0)]                                                                            |

    You may define as many rotations as you wish.

  - `rotation` sets the display rotation according to the orientation table.

    The default orientation table defines four counter-clockwise rotations for 240x320, 170x320, 240x280,
    240x240, 135x240, 128x160 and 128x128 displays with the LCD's ribbon cable at the bottom of the display.
    The default rotation is Portrait (0 degrees).

    | Index | Rotation                        |
    | ----- | ------------------------------- |
    | 0     | Portrait (0 degrees)            |
    | 1     | Landscape (90 degrees)          |
    | 2     | Reverse Portrait (180 degrees)  |
    | 3     | Reverse Landscape (270 degrees) |

  - `custom_init` List of display configuration commands to send to the display during the display init().
    The list contains tuples with a bytes object, optionally followed by a delay specified in ms. The first
    byte of the bytes object contains the command to send optionally followed by data bytes.
    See the `examples/configs/t_dongle_s3/tft_config.py` file or an example.

  - `color_order` Sets the color order used by the driver (st7789.RGB or st7789.BGR)

  - `inversion` Sets the display color inversion mode if True, clears the
    display color inversion mode if false.

  - `options` Sets driver option flags.

    | Option        | Description                                                                                              |
    | ------------- | -------------------------------------------------------------------------------------------------------- |
    | st7789.WRAP   | pixels, lines, polygons, and Hershey text will wrap around the display both horizontally and vertically. |
    | st7789.WRAP_H | pixels, lines, polygons, and Hershey text will wrap around the display horizontally.                     |
    | st7789.WRAP_V | pixels, lines, polygons, and Hershey text will wrap around the display vertically.                       |

- `inversion_mode(bool)` Sets the display color inversion mode if True, clears
  the display color inversion mode if False.

- `madctl(value)` Returns the current value of the MADCTL register or sets the MADCTL register if a value is passed to the
  method. The MADCTL register is used to set the display rotation and color order.

  #### [MADCTL constants](#madctl-constants)

  | Constant Name    | Value | Description              |
  | ---------------- | ----- | ------------------------ |
  | st7789.MADCTL_MY | 0x80  | Page Address Order       |
  | st7789_MADCTL_MX | 0x40  | Column Address Order     |
  | st7789_MADCTL_MV | 0x20  | Page/Column Order        |
  | st7789_MADCTL_ML | 0x10  | Line Address Order       |
  | st7789_MADCTL_MH | 0x04  | Display Data Latch Order |
  | st7789_RGB       | 0x00  | RGB color order          |
  | st7789_BGR       | 0x08  | BGR color order          |

  #### [MADCTL examples](#madctl-examples)

  | Orientation                                                                                      | MADCTL Values for RGB color order, for BGR color order add 0x08 to the value. |
  | ------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------- |
  | <img src="https://raw.githubusercontent.com/russhughes/st7789_mpy/master/docs/madctl_0.png" />   | 0x00                                                                          |
  | <img src="https://raw.githubusercontent.com/russhughes/st7789_mpy/master/docs/madctl_y.png" />   | 0x80 ( MADCTL_MY )                                                            |
  | <img src="https://raw.githubusercontent.com/russhughes/st7789_mpy/master/docs/madctl_x.png" />   | 0x40 ( MADCTL_MX )                                                            |
  | <img src="https://raw.githubusercontent.com/russhughes/st7789_mpy/master/docs/madctl_xy.png" />  | 0xC0 ( MADCTL_MX + MADCTL_MY )                                                |
  | <img src="https://raw.githubusercontent.com/russhughes/st7789_mpy/master/docs/madctl_v.png" />   | 0x20 ( MADCTL_MV )                                                            |
  | <img src="https://raw.githubusercontent.com/russhughes/st7789_mpy/master/docs/madctl_vy.png" />  | 0xA0 ( MADCTL_MV + MADCTL_MY )                                                |
  | <img src="https://raw.githubusercontent.com/russhughes/st7789_mpy/master/docs/madctl_vx.png" />  | 0x60 ( MADCTL_MV + MADCTL_MX )                                                |
  | <img src="https://raw.githubusercontent.com/russhughes/st7789_mpy/master/docs/madctl_vxy.png" /> | 0xE0 ( MADCTL_MV + MADCTL_MX + MADCTL_MY )                                    |

- `init()`

  Must be called to initialize the display.

- `on()`

  Turn on the backlight pin if one was defined during init.

- `off()`

  Turn off the backlight pin if one was defined during init.

- `sleep_mode(value)`

  If value is True, cause the display to enter sleep mode, otherwise wake up if value is False. During sleep display content may not be preserved.

- `fill(color)`

  Fill the display with the specified color.

- `pixel(x, y, color)`

  Set the specified pixel to the given `color`.

- `line(x0, y0, x1, y1, color)`

  Draws a single line with the provided `color` from (`x0`, `y0`) to
  (`x1`, `y1`).

- `hline(x, y, length, color)`

  Draws a single horizontal line with the provided `color` and `length`
  in pixels. Along with `vline`, this is a fast version with fewer SPI calls.

- `vline(x, y, length, color)`

  Draws a single horizontal line with the provided `color` and `length`
  in pixels.

- `rect(x, y, width, height, color)`

  Draws a rectangle from (`x`, `y`) with corresponding dimensions

- `fill_rect(x, y, width, height, color)`

  Fill a rectangle starting from (`x`, `y`) coordinates

- `blit_buffer(buffer, x, y, width, height)`

  Copy bytes() or bytearray() content to the screen internal memory. Note:
  every color requires 2 bytes in the array

- `bounding({status, as_rect})`

  Bounding enables or disables tracking the display area that has been written
  to. Initially, tracking is disabled; pass a True value to enable tracking and
  False to disable it. Passing a True or False parameter will reset the current
  bounding rectangle to (display_width, display_height, 0, 0).

  Returns a four integer tuple containing (min_x, min_y, max_x, max_y)
  indicating the area of the display that has been written to since the last
  clearing.

  If `as_rect` parameter is True, the returned tuple will contain (min_x,
  min_y, width, height) values.

- `width()`

  Returns the current logical width of the display. (ie a 135x240 display
  rotated 90 degrees is 240 pixels wide)

- `height()`

  Returns the current logical height of the display. (ie a 135x240 display
  rotated 90 degrees is 135 pixels high)

- `rotation(r)`

  Set the rotates the logical display in a counter-clockwise direction.
  0-Portrait (0 degrees), 1-Landscape (90 degrees), 2-Inverse Portrait (180
  degrees), 3-Inverse Landscape (270 degrees)

- `offset(x_start, y_start)` The memory in the ST7789 controller is configured
  for a 240x320 display. When using a smaller display like a 240x240 or
  135x240, an offset needs to be added to the x and y parameters so that the
  pixels are written to the memory area corresponding to the visible display.
  The offsets may need to be adjusted when rotating the display.

  For example, the TTGO-TDisplay is 135x240 and uses the following offsets.

  | Rotation | x_start | y_start |
  | -------- | ------- | ------- |
  | 0        | 52      | 40      |
  | 1        | 40      | 53      |
  | 2        | 53      | 40      |
  | 3        | 40      | 52      |

  When the rotation method is called, the driver will adjust the offsets for a
  135x240 or 240x240 display. Your display may require using different offset
  values; if so, use the `offset` method after `rotation` to set the offset
  values.

  The values needed for a particular display may not be documented and may
  require some experimentation to determine the correct values. One technique
  is to draw a box the same size as the display and then make small changes to
  the offsets until the display looks correct. See the `cfg_helper.py` program
  in the examples folder for more information.

The module exposes predefined colors:
`BLACK`, `BLUE`, `RED`, `GREEN`, `CYAN`, `MAGENTA`, `YELLOW`, and `WHITE`

## Helper functions

- `color565(r, g, b)`

  Pack a color into 2-bytes rgb565 format

- `map_bitarray_to_rgb565(bitarray, buffer, width, color=WHITE, bg_color=BLACK)`

  Convert a `bitarray` to the rgb565 color `buffer` suitable for blitting. Bit
  1 in `bitarray` is a pixel with `color` and 0 - with `bg_color`.
