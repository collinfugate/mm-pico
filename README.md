# Multi-Metrics Pico firmware

Firmware for the Raspberry Pi Pico inside the Multi-Metrics box, plus one
script that compiles it and flashes it. It works the same on Windows and macOS
and contains no paths tied to anyone's computer.

Two boards are supported:

| Board  | Talks to the app over |
| ------ | --------------------- |
| Pico   | USB                   |
| Pico W | USB or Wi-Fi          |

## Flash a box

You need Python 3.8 or newer and a USB cable that carries data.

1. Hold the **BOOTSEL** button on the Pico and plug in the USB cable. It shows
   up as a drive called `RPI-RP2`.
2. Double-click `flash.bat` on Windows or `flash.command` on macOS.
3. Pick the board. For a Pico W, type the Wi-Fi name and password when asked.

The script copies the firmware over and the Pico restarts on its own.

From a terminal the same thing is:

```
python flash.py pico
python flash.py pico_w
python flash.py pico_w --ssid "Lab WiFi" --password "secret123"
```

A release zip already contains compiled images in `prebuilt/`, so flashing
from it needs no compiler. A plain checkout of this repository compiles first.

macOS may refuse to open `flash.command` the first time because it was
downloaded. Right-click it and choose Open.

## Wi-Fi

A Pico W joins your existing Wi-Fi network and the Multi-Metrics app finds it
automatically when no box is plugged in over USB.

- The Wi-Fi name and password are stored on the Pico, not in this repository
  and not in the firmware image. `flash.py` writes them when it flashes.
- Flashing again and pressing Enter at the Wi-Fi name keeps the stored
  settings. To change networks, flash again and type the new details.
- The box and the computer must be on the same network.
- WPA2 networks and open networks are supported.
- The box still needs power over its USB connector.
- Anyone on the same network can send commands to the box. There is no
  password on the connection.

Commands travel over TCP port 6060 and use exactly the same bytes as USB. The
box answers a UDP broadcast of `MM-DISCOVER` on port 6060 with
`MM-PICO <model number>`, which is how the app locates it.

## Build from source

```
python flash.py pico_w --build       compile src/, then flash
python flash.py all --build-only     compile both images, flash nothing
python flash.py pico --clean --build start from an empty build folder
```

The first build downloads the Pico SDK 2.3.1 and the ARM compiler, a few
hundred MB, into `.pico-sdk` in your home folder. That is the same place and layout
the Raspberry Pi Pico VS Code extension uses, so if the extension has already
installed them nothing is downloaded, and either tool can build the project.

Compiled images land in `build/pico/` and `build/pico_w/`.

## Layout

```
flash.py               compile and flash; the only script you run
flash.bat              double-click launcher for Windows
flash.command          double-click launcher for macOS
CMakeLists.txt         build definition, no machine paths
pico_sdk_import.cmake  standard Pico SDK locator
src/
  main_device.c        startup, main loop, command decoder
  mm.c  mm.h           instrument functions and op codes
  wifi.c  wifi.h       Wi-Fi link, compiled into the Pico W image only
  lwipopts.h           network stack settings for the Pico W image
  usb_descriptors.*  tusb_config.h   USB setup
```

## If something goes wrong

- **No Pico appeared.** The cable may be power-only, or BOOTSEL was not held
  down while plugging in.
- **"That board is a Pico 2."** This firmware targets the original Pico and
  Pico W.
- **The app does not find the box over Wi-Fi.** Flash again and retype the
  Wi-Fi details. With the box on USB, sending op code 178 returns its link
  state and IP address. Setting `MM_PORT=socket://<address>:6060` before
  starting the app connects to a known address directly.
