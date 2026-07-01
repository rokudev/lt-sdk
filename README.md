Welcome to the _Roku LT Operating System_ open source distribution.

You may view the <a href="https://go.roku.com/roku-lt-os-videos">LT Open Source instructional videos</a> and read the <a href="https://blog.roku.com/developer/roku-lt-os">LT Open Source announcement</a> to learn more.

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

To build LT OS for ESP32:
```
% cd /path-to-lt-sdk/lt-firmware-example
% source build-setup.sh
% espshell
% build
% make quiet
```

To flash, clean:
```
% make FlashFirmware
% make clean
```
To perform first-time initialization of all partitions of the flash (build first):
```
% LT_FLASH=all make FlashFirmware
```

To build LT OS for Linux:
```
% cd /path-to-lt-sdk/lt-firmware-example
% source build-setup.sh
% linuxshell
% build
% make quiet
```

To run Linux build:
```
% bin
% export LT_LIBRARY_PATH=.
% sudo ./ltrun LTSystemShell
```

For reference only, and not an endorsement, some example ESP32 development boards are:
- [Espressif ESP32-DevKitC](https://www.espressif.com/en/products/devkits/esp32-devkitc/overview)
- [Adafruit HUZZAH32 - ESP32 Feather Board (pre-soldered)](https://www.adafruit.com/product/3591)
- [SparkFun ESP32 Thing](https://www.sparkfun.com/sparkfun-esp32-thing.html)
- [Amazon search for ESP32 camera boards](https://www.amazon.com/s?k=ESP32+cam)

Availability, pricing, and features may vary by seller and region.
