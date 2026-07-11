# Welcome to the _Roku LT Operating System_ open source distribution.

### Documentation
1. <a href="LT-OS_Advantages.md">Advantages of LT</a>
2. <a href="lt/lt/doc/architecture">LT Architecture</a>
3. <a href="https://go.roku.com/roku-lt-os-videos">LT Instructional Videos</a>
4. <a href="https://blog.roku.com/developer/roku-lt-os">LT Open Source announcement</a>

### Building for ESP32
**Prerequisites**

Before building LT OS for ESP32, ensure the pyserial Python module is installed. The bundled esptool.py requires it; otherwise you may see errors about pyserial not being installed. On Debian/Ubuntu Linux, install it with:
```
% sudo apt install python3-serial
```
On Fedora/RHEL/CentOS, install it with:
```
% sudo dnf install python3-pyserial
```
On macOS, install it with:
```
% pip3 install pyserial
```
On Windows, install it with:
```
% py -m pip install pyserial
```
**To build:**
```
% cd /path-to-lt-sdk/lt-firmware-example
% source build-setup.sh
% espshell
% build
% make quiet
```
**To flash:**
```
% make FlashFirmware
```
**To clean:**
```
% make clean
```
**Note:**
To perform first-time initialization of all flash partitions (build first):
```
% LT_FLASH=all make FlashFirmware
```

### Building for Linux
```
% cd /path-to-lt-sdk/lt-firmware-example
% source build-setup.sh
% linuxshell
% build
% make quiet
```

### Running on Linux
```
% bin
% export LT_LIBRARY_PATH=.
% sudo ./ltrun LTSystemShell
```

### Building for STM32
Currently LT is operational on the CM7 core of the stm32-h755-nucleo-144 board.

**To build:**
```
% cd /path-to-lt-sdk/lt-firmware-example
% source build-setup.sh
% stshell
% build
% make quiet
```

**To flash:**

Flash using the STM32CubeProgrammer tool:
1. Flash the file *targets/lt-firmware-example.shell/st.st-h755-nucleo-144-cm7/release/bin/firmware.elf* to flash sector 0.
2. one time only - Flash the file *targets/lt-firmware-example.shell/st.st-h755-nucleo-144-cm7/release/bin/LTPartitionTable.bin* to flash sector 4 and to flash sector 5.

### Sample ESP32 Development Boards
For reference only, and not an endorsement, some example ESP32 development boards are:
- [Espressif ESP32-DevKitC](https://www.espressif.com/en/products/devkits/esp32-devkitc/overview)
- [Adafruit HUZZAH32 - ESP32 Feather Board (pre-soldered)](https://www.adafruit.com/product/3591)
- [SparkFun ESP32 Thing](https://www.sparkfun.com/sparkfun-esp32-thing.html)
- [Amazon search for ESP32 camera boards](https://www.amazon.com/s?k=ESP32+cam)

Availability, pricing, and features may vary by seller and region.
