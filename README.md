Welcome to the _Roku LT Operating System_ open source distribution.

LT Open Source instructional videos may be found on the Roku Developer YouTube channel at https://go.roku.com/roku-lt-os-videos  
The LT Open Source announcement may be found on the Roku Developer Blog at https://blog.roku.com/developer/roku-lt-os

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
To perform first time initialziation of all partitions of the Flash (build first):
```
LT_FLASH=all make FlashFirmware
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

To obtain an inexpensive ESP32 development platform, go to amazon.com and search "ESP32 Cam". They are available in 2-packs for ~$20.00.
