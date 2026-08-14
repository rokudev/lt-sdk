# LT OS Device Library Suite — Differentiators and Usage Guide

## Architecture Differentiator Common to All Device Libraries

Every LT device library shares a structural advantage that neither FreeRTOS nor Linux matches in a single model: a strict, enforced **Device / Driver split**. Each device library (`LTDevice*`) exposes a platform-independent C++ pure-virtual interface; the matching driver library (`LTDriver*`) provides the platform-specific implementation. Applications never touch driver code. The binding is declared in a platform's `LTDeviceConfig.json` and resolved at runtime by the LT library manager — meaning the same application binary runs on ESP32, Bouffalo, Anyka, ST, and Linux x86/ARM with no conditional compilation anywhere. FreeRTOS has no equivalent abstraction layer; each port wires application code directly to BSP-specific calls. Linux's `/dev` model is closer, but requires file I/O and system-call overhead even within the same process, and offers no standardized property/capability model across device classes. The LT model carries zero syscall overhead and zero file descriptors.

All device libraries also carry the `LTDeviceKonfig` / `LTDeviceConfig` subsystem: a JSON-described device tree that maps class names to unit names to driver object names, supports named memory regions, and lets drivers enumerate their units and read their configuration at first use — not at boot — so unused peripherals consume no initialization time or memory.

---

## Device Library Reference

### LTDeviceADC — Analog-to-Digital Converters
FreeRTOS has no standard ADC abstraction; each port is vendor-specific. Linux uses `iio` (Industrial I/O), which requires sysfs or character-device reads. LTDeviceADC provides named channels (`GetChannelByName("bvat_mV")`) alongside index-based access, callback-driven burst sampling (`GetSamples`), and channel enumeration for self-test — all without any file I/O.

```c
LTDeviceADC *adc = lt_openlibrary(LTDeviceADC);
LTADCChannel *ch = adc->GetChannelByName("bvat_mV");
u32 mv;
ch->API->GetSample(ch, &mv);          // single sample
ch->API->GetSamples(ch, 64, myCb, NULL); // burst, callback per sample
lt_destroyobject(ch);
lt_closelibrary(adc);
```

### LTDeviceAnalogMic — Analog Microphone
FreeRTOS has no standard audio capture abstraction. Linux ALSA is powerful but heavyweight (kernel drivers, PCM descriptors, mmap). LTDeviceAnalogMic is a minimal direct-DMA interface: set a user-supplied s16 buffer, set gain, start capture, receive callbacks per DMA frame — no kernel, no ALSA plugin chain.

```c
LTDeviceAnalogMic *mic = lt_openlibrary(LTDeviceAnalogMic);
s16 buf[512];
mic->SetBuffSize(512, buf);
mic->SetGain(20);
mic->StartCap(myAudioCallback);   // callback fires with (data, framesize)
// ... capture audio ...
mic->StopCap();
```

### LTDeviceAuthentication — Hardware-Backed Device Authentication
No equivalent in FreeRTOS or standard Linux beyond OpenSSL. LTDeviceAuthentication provides AES-ECB encode/decode backed by hardware eFuse keys (via `LTDeviceEfuse`), chip ID retrieval, and key validation — all without exposing raw key material to application code. Dev-key mode detection allows factory/production firmware to behave differently without conditional compilation.

```c
LTDeviceAuthentication *auth = lt_openlibrary(LTDeviceAuthentication);
u8 authKey[kLTAuthenticationKeyBytes];
if (auth->IsSecureDevice() && !auth->IsUsingDevKeys()) {
    auth->CalculateAuthKey(authKey);
    u8 challenge[16], response[16];
    auth->GetRandom16(challenge);
    auth->AuthEncode(challenge, response);  // hardware AES
}
```

### LTDeviceBattery — Battery Management System
FreeRTOS has no battery abstraction. Linux uses the `power_supply` class (sysfs reads). LTDeviceBattery provides structured `LTDeviceBatteryInfo` (voltage, current, temperature, SoC, charger mode, adaptor voltage — all in one call), operating mode control (Normal/Shipping/Shutdown), input current limiting, and event-driven status change callbacks. Multiple named battery systems on one device are enumerable.

```c
LTDeviceBattery *bat = lt_openlibrary(LTDeviceBattery);
LTBatterySystem *sys = bat->GetBatterySystemByName("main");
LTDeviceBatteryInfo info;
sys->API->GetInfo(sys, &info);
lt_consoleprint("SoC=%u%% voltage=%umV\n", info.battery.stateOfCharge, info.battery.voltage);
sys->API->OnStatusChangeCallback(sys, myBatteryCallback, NULL);
```

### LTDeviceBle — Bluetooth LE (GATT Server/Client)
FreeRTOS has no standard BLE abstraction; vendor SDKs (NimBLE, esp-idf BLE) are used directly. Linux uses BlueZ via D-Bus, adding IPC overhead. LTDeviceBle provides a unified GATT server/client model: register services and characteristics with typed read/write callbacks, advertise, connect, notify/indicate, update connection parameters, and get per-packet TX timestamps for time-sensitive applications — all in a single library with no D-Bus, no sockets.

```c
LTBleDeviceCtx devCtx = { .mac = myMac, .preferred_mtu = 517 };
LTBleSvcCtx svcCtx = { .uuid = &myServiceUuid, .numCharacteristics = 1 };
LTBleChrCtx chrCtx = { .uuid = &myCharUuid, .chrFlags = LT_BLE_GATT_CHR_F_READ | LT_BLE_GATT_CHR_F_NOTIFY,
                        .readCb = myChrReadCb, .readBuf = rxBuf, .readBufSize = sizeof(rxBuf) };
ble->SetService(svcHandle);
ble->Start(devHandle);
LTBleAdvFields adv = { .name = "MySensor", .flags = LT_BLE_ADV_F_DISC_GEN | LT_BLE_ADV_F_BREDR_UNSUP };
LTBleAdvParams params = { .conn_mode = LT_BLE_CONN_MODE_UND, .duration_ms = LT_BLE_ADV_FOREVER };
ble->StartAdvertise(devHandle, &adv, NULL, &params);
```

### LTDeviceBleController — HCI BLE Controller
Splits the BLE stack at the HCI boundary: the controller library manages the link layer, and the host library (`LTDeviceBle`) manages GATT. This allows a secondary BLE controller chip (connected over UART/SDIO) to be used transparently with the same host API. FreeRTOS and Linux offer no equivalent split-stack model without heavy middleware.

### LTDeviceCapTouch — Capacitive Touch Sensor
FreeRTOS has no standard touch abstraction. Linux uses the `input` subsystem (file reads). LTDeviceCapTouch delivers mode-driven touch events (Normal/Sleep/Deep-Sleep) via callbacks with configurable debounce, cumulative trigger counting for manufacturing test, and a built-in reset-line test — in a single `lt_createdeviceobject` call.

```c
LTDeviceCapTouch *ct = lt_createdeviceobject(LTDeviceCapTouch);
ct->API->OnCapTouchTriggerEvent(ct, myTouchCallback, NULL, myData);
ct->API->Initialize(ct, kLTDeviceCapTouch_Mode_Normal);
```

### LTDeviceConfig / LTDeviceKonfig — Device Configuration Tree
The JSON-described device tree is a first-class LT library with no equivalent in FreeRTOS (BSPs use C header files) or standard Linux (devicetree is kernel-only; user space reads via `/sys`). LTDeviceKonfig is a lightweight LTObject: instantiate, query, destroy — no persistent state, no sysfs. It supports named memory regions (for DMA-region-aware allocations), device class and unit enumeration, string/integer/binary config reads, and the automatic creation of the correct driver object for any device class/unit pair.

```c
LTDeviceKonfig *cfg = lt_createobject(LTDeviceKonfig);
u32 n = cfg->API->GetNumDeviceUnits(cfg, deviceClassIndex);
for (u32 i = 0; i < n; i++)
    lt_consoleprint("unit: %s\n", cfg->API->GetDeviceUnitNameAt(cfg, deviceClassIndex, i));
lt_destroyobject(cfg);
```

### LTDeviceEfuse — eFuse One-Time Programmable Memory
FreeRTOS has no standard eFuse API. Linux exposes eFuse via sysfs (vendor-specific). LTDeviceEfuse provides a named-field model: each field has a name, index, and byte length. A test/simulation mode prevents accidental real burns. Factory commands can validate all required fields in one call. The interface also reports the unprogrammed byte value (0x00 or 0xFF depending on technology) to aid validation code.

### LTDeviceFlash — NOR Flash
FreeRTOS flash is typically raw mtd or vendor-specific. Linux uses `mtd` character devices. LTDeviceFlash adds: a named **partition table** (get/enumerate by name), primary + backup partition table with CRC validation, partition-flag-based transparent hardware encryption (`ReadBytes`/`WriteBytes` auto-decrypt; `ReadRawBytes`/`WriteRawBytes` bypass encryption), write-quantum constraints for AES-GCM-aligned writes, and bus-address-to-byte-offset mapping for XIP self-identification.

```c
LTDeviceFlash *flash = lt_openlibrary(LTDeviceFlash);
LTDeviceUnit hUnit = flash->CreateDeviceUnitHandle(0);
LTDeviceFlash_Partition part;
flash->GetPartition(hUnit, "ota", &part);
lt_gethandleinterface(hUnit, ILTFlashDeviceUnit)->WriteBytes(hUnit, part.entry.nByteOffset, len, data);
```

### LTDeviceFS — File System Object
A portable LTObject wrapping `LTDriverFS` (e.g., FAT32) with path-based operations: exists, isFile, isDirectory, copy, move, recursive delete, make directory path, directory traversal with callback, plus block-oriented read/write and seek — no POSIX dependency. FreeRTOS uses FatFs directly with no object abstraction. Linux uses POSIX but requires libc.

```c
LTDeviceFS *f = lt_createobject(LTDeviceFS);
f->API->SetBaseDirectory(f, "/mnt/sd");
f->API->SetName(f, "config.json");
if (f->API->Open(f, false))
    f->API->Read(f, sizeof(buf), buf);
f->API->Close(f);
lt_destroyobject(f);
```

### LTDeviceGpio — General-Purpose I/O
FreeRTOS GPIO is per-BSP. Linux uses `libgpiod` (character device). LTDeviceGpio provides: name-to-index lookup, mode (input/output/highZ/alternate-function) and pull (none/up/down) configuration, ISR registration with `LT_ISR_SAFE` type enforcement, named-pin abstraction (logical pin names decoupled from GPIO numbers), wakeup GPIO bitmask retrieval, and pending IRQ clearing — all per-platform-agnostic index.

```c
LTDeviceGpio *gpio = lt_createdeviceobject(LTDeviceGpio);
u16 idx = /* index for "camera_reset" pin */;
gpio->API->SetGpioModeFromIndex(gpio, idx, kLTDeviceGpio_ModeType_Output);
gpio->API->SetOutputValue(gpio, idx, true);
gpio->API->SetISR(gpio, idx, myISR, kLTDeviceGPIO_TriggerType_RisingEdge, myData);
lt_destroyobject(gpio);
```

### LTDeviceI2C — I2C Bus
FreeRTOS I2C is per-BSP. Linux uses `ioctl` on `/dev/i2c-N`. LTDeviceI2C adds: named bus lookup (`GetBusIndexFromName`), capability discovery (DMA, async, master/slave, hardware/bit-bang), configurable transfer timeout, combined write-then-read in a single call, and bus-level reset and address probe — all without file descriptors.

```c
LTDeviceI2C *i2c = lt_openlibrary(LTDeviceI2C);
u32 bus = i2c->GetBusIndexFromName("i2c0");
LTDeviceUnit hBus = i2c->CreateDeviceUnitHandle(bus);
u8 reg = 0x0F, val;
i2c->I2CMasterTransfer(hBus, 0x48, &val, 1, &reg, 1, true, true, NULL, NULL);
```

### LTDeviceIdentity — Device Identity & Security
FreeRTOS has no identity model. Linux provides no standard embedded-device identity. LTDeviceIdentity delivers: manufacturer/model/type/serial/MAC/board-revision (all in one library), device-specific AES key retrieval by index, Unique Device Secret (UDS) management, LTAT (LT Authentication Token) installation and claim checking, and security status flags (IsSecured, IsManufacturingFirmware).

```c
LTDeviceIdentity *id = lt_openlibrary(LTDeviceIdentity);
if (id->IsValid()) {
    lt_consoleprint("Serial: %s\n", id->GetSerialNumber());
    u8 key[kLTIdentityDeviceAESKeyBytes];
    id->GetAESKey(key, 1);
}
```

### LTDeviceImageSensor — Camera Image Sensor
FreeRTOS has no image sensor abstraction. Linux uses V4L2 (complex, kernel-resident). LTDeviceImageSensor provides async power-on/off with event notification, and a rich `SetAttribute`/`GetAttribute` model covering frame size (96×96 to 2048×1536), pixel format (RGB565/YUV422/Grayscale/JPEG), exposure control, white balance, AGC, noise reduction, sharpness, lens correction, binning, and direct register access — no kernel drivers needed.

```c
LTDeviceImageSensor *sensor = lt_openlibrary(LTDeviceImageSensor);
sensor->OnImageSensorEvent(hUnit, mySensorEventCb, NULL);
sensor->PowerOn(hUnit);
// wait for kLTImageSensorEvent_PowerOn in callback
LTImageSensorFrameSize sz = kLTImageSensorFrameSize_640x480;
sensor->SetAttribute(hUnit, kLTImageSensorAttribute_FrameSize, &sz);
LTImageSensorPixelFormat fmt = kLTImageSensorPixelFormat_JPEG;
sensor->SetAttribute(hUnit, kLTImageSensorAttribute_PixelFormat, &fmt);
```

### LTDeviceIrTx — Infrared Transmitter
No equivalent in FreeRTOS or Linux standard libraries. LTDeviceIrTx delivers Roku NEC protocol key codes (`TransmitKey`), raw NEC system/command codes (`TransmitNEC`), universal PWM stream (`TransmitUniversal`), and press-and-hold repeat management (`FinishTransmission`) — all without any file descriptor or `lirc` stack.

### LTDeviceKeyInput / LTDeviceKeypad — Key Input
FreeRTOS has no key-input model. Linux uses the `input` subsystem. LTDeviceKeyInput delivers: press, release, hold (with configurable hold time), and "unstuck" events per registered callback; key-name ↔ value lookup; stuck-key detection and recovery. LTDeviceKeypad adds: programmatic key injection (`DispatchKeyDown/Up/Press/RawKeySequence`), current hardware state query, and stuck-key detection — useful for manufacturing test.

```c
LTDeviceKeyInput *ki = lt_openlibrary(LTDeviceKeyInput);
ki->Initialize(NULL);  // creates own thread
ki->RegisterForKeyHold(myHoldCb, kLTKeyPower, LTTime_Seconds(3), NULL, myData);
ki->RegisterForKeyPress(myPressCb, NULL, myData);
```

### LTDeviceLED — LED Groups with Fader
FreeRTOS LEDs are raw GPIO calls. Linux uses `leds` class (sysfs). LTDeviceLED organizes LEDs into named groups (indicator lamps and seven-segment displays). `LTDeviceLEDFader` adds smooth IRGB fade sequences defined as step arrays (color → transition_ms → dwell_ms) with per-step and per-sequence callbacks, running on a user-supplied or auto-created thread at a configurable interval — no RTOS tick dependency.

```c
static const LTDeviceLEDFaderTimingStep breathe[] = {
    { 0x00FF0000, 500, 0 },   // fade to red over 500ms, hold forever
};
LTDeviceLEDFader *fader = lt_createobject(LTDeviceLEDFader);
fader->API->Initialize(fader, "status_led", 0, NULL, LTTime_Milliseconds(20));
fader->API->StartFadeSequence(fader, breathe, LTDeviceLEDFaderTimingStep_Count(breathe), NULL, NULL, NULL);
```

### LTDeviceLightSensor — Ambient Light Sensor (Multi-Channel)
FreeRTOS has no light sensor abstraction. Linux uses `iio`. LTDeviceLightSensor delivers visible, IR, R, G, B channel readout in millilux (32-bit range, orders of magnitude above direct sunlight), configurable integration time (rounds down to next slower supported value for precision), and channel capability discovery — without sysfs.

```c
LTDeviceLightSensor *ls = lt_createdeviceobject(LTDeviceLightSensor);
ls->API->SetIntegrationTime(ls, LTTime_Milliseconds(100));
IlluminanceValue lux;
ls->API->GetChannelValue(ls, kLTDeviceLightSensor_Channel_Visible, &lux);
lt_consoleprint("Illuminance: %u mLux\n", lux);
lt_destroyobject(ls);
```

### LTDeviceMedia — Media Source/Sink Pipeline
FreeRTOS has no media pipeline model. Linux uses GStreamer/V4L2/ALSA. LTDeviceMedia defines a format-typed source/sink model: open a source for H.264-HD, H.264-SD, JPEG, PCM, G.711, Opus, or motion-data; receive frames via callback (`kLTMediaEvent_Data` with IPC buffer ID for zero-copy inter-process delivery, or local buffer pointer); control bitrate, GOP length, AE compensation; and insert H.264 SEI NALUs with multiple UUID-tagged payloads for metadata embedding. Sinks accept frames, report max-frame-size and available-buffer-count for flow control.

```c
LTDeviceMedia *media = lt_openlibrary(LTDeviceMedia);
LTMediaFormat fmt = { .nKind = kLTMediaKind_Video_HD, .nEncoding = kLTMediaEncoding_H264,
                       .params.h264 = { .nWidth = 1920, .nHeight = 1080 } };
LTMediaSource src = media->OpenSource(&fmt);
lt_gethandleinterface(src, ILTMediaSource)->OnMediaEvent(src, myFrameCallback, myCtx);
lt_gethandleinterface(src, ILTMediaSource)->Start(src);
```

### LTDeviceMipiCsi — MIPI-CSI Camera Bus
No equivalent in FreeRTOS. Linux uses V4L2 subdev. LTDeviceMipiCsi exposes clock and stream-output enable/disable for MIPI-CSI lanes — enough to initialize a camera sensor pipeline without V4L2 kernel infrastructure. Used together with LTDeviceImageSensor to sequence power → clock → stream.

### LTDeviceMotorPanTilt — Pan/Tilt Motor
No equivalent in FreeRTOS or Linux standard libraries. LTDeviceMotorPanTilt provides absolute and relative position movement, a **sentry path** (waypoint list with per-waypoint dwell times, running autonomously and looping), speed and calibration-interval control, IR-cut filter control, and event callbacks for state changes, movement progress, and sentry events.

```c
LTMotorPanTilt *motor = lt_createdeviceobject(LTMotorPanTilt);
motor->API->OpenMotor(motor, kLTMotorPanTilt_Type_PanTilt);
LTMotorPanTilt_SentryPath path = {
    .numWaypoints = 2,
    .waypoints = { { .dwellTime = LTTime_Seconds(3), .position = { .pan = -90, .tilt = 0 } },
                   { .dwellTime = LTTime_Seconds(3), .position = { .pan =  90, .tilt = 0 } } }
};
motor->API->SetSentryPath(motor, &path);
```

### LTDeviceNPU — Neural Processing Unit
No equivalent in FreeRTOS. Linux uses TFLite delegates or vendor SDKs. LTDeviceNPU provides a clean model-lifecycle API: load model from buffer, query tensor metadata (dimensions, data type, layout, scale), populate input tensors via direct pointers (no copy), run inference synchronously, read output tensors. Handles up to 8 inputs and 8 outputs per model.

```c
LTDeviceNPU *npu = lt_createdeviceobject(LTDeviceNPU);
LTHandle model;
npu->API->LoadModel(npu, modelData, modelSize, &model);
LTDeviceNPUModelInfo info;
npu->API->GetModelInformation(npu, model, &info);
lt_memcpy(info.inputs[0].data, inputImage, info.inputs[0].size);
npu->API->RunModel(npu, model);
// read info.outputs[0].data
npu->API->UnloadModel(npu, model);
lt_destroyobject(npu);
```

### LTDeviceOta / LTDeviceOtaBundle — OTA Firmware Update
FreeRTOS uses ESP-IDF's OTA or custom implementations. Linux uses system package managers. LTDeviceOta defines a standardized 9-step OTA protocol (Init → Validate → CheckStorage → Prepare → SaveBlock → VerifyImage → Apply → Complete) with a `LTDeviceOta_ImageHeader` that includes plaintext magic, seed/nonce for firmware key derivation, and AES-GCM authentication tag — enabling verified, encrypted OTA across all embedded platforms. LTDeviceOtaBundle adds a slice-request model for multi-asset updates.

### LTDevicePIR — Passive Infrared Motion Sensor
FreeRTOS has no PIR abstraction. Linux uses input events or GPIO userspace interrupts. LTDevicePIR delivers: named-sensor lookup, multi-subscriber motion detection callbacks (with debounce via `SetMotionEndDelay`), percentage-based sensitivity, and enable/disable — all without polling.

```c
LTDevicePIR *pir = lt_openlibrary(LTDevicePIR);
LTDeviceUnit hPir = pir->CreateDeviceUnitHandleByName("outdoor");
pir->RegisterForMotionDetection(hPir, myMotionCb, NULL, myData);
pir->SetSensitivity(hPir, 75);      // 75% sensitivity
pir->SetMotionEndDelay(hPir, LTTime_Seconds(5));
```

### LTDevicePins — Named GPIO Pin Banks
Complements LTDeviceGpio with a bank-oriented model: pins are grouped into output, input, and bidirectional banks, each named in `LTDeviceConfig.json`. Bidirectional banks support: push-pull/open-drain output configuration, pull-up/down input configuration, hardware-debounced IRQ callbacks (ISR-safe), wakeup-source configuration (integrated with LTDevicePower sleep model), and reboot-hold (maintain pin state through reboot).

```c
LTDevicePins *pins = lt_openlibrary(LTDevicePins);
LTDeviceUnit hBank = pins->CreateDeviceUnitHandleByName("pwr_ctrl");
ILTDriverPins_BidirectionalBank *iface = lt_gethandleinterface(hBank, ILTDriverPins_BidirectionalBank);
iface->ConfigureAsOutput(hBank, kLTDevicePin_PinConfiguration_OutputType_PushPull);
iface->Set(hBank, 0b101);   // pins 0 and 2 high, pin 1 low
```

### LTDevicePotentiometer — Digital Potentiometer
No standard equivalent in FreeRTOS or Linux. Direct DPOT register get/set per named device unit — used for analog bias control (e.g., audio gain, sensor threshold) without exposing I2C details to the application.

### LTDevicePower — Low-Power Sleep Management
FreeRTOS power management is per-RTOS-port (tickless idle hooks). Linux uses PM QoS and runtime PM. LTDevicePower integrates with LTCore's thread-idle detection: enable sleep mode with a configurable idle delay and minimum sleep duration; LTCore automatically enters sleep when all threads are idle for that interval. Wakeup reasons (RTC, motion, button, WiFi packet, charge event) are queryable after wakeup.

```c
LTDevicePower *pwr = lt_createdeviceobject(LTDevicePower);
pwr->API->EnableSleepMode(pwr, LTTime_Seconds(5), LTTime_Seconds(2));
// ... threads run; system automatically sleeps after 5s of full idle ...
LTDevicePower_WakeupReason why = pwr->API->GetLastWakeupReason(pwr);
lt_consoleprint("woke: %s\n", pwr->API->WakeupReasonToString(pwr, why));
lt_destroyobject(pwr);
```

### LTDevicePowerSubswitch — Subsidiary SOC Power Control
No equivalent in FreeRTOS or Linux standard libraries. Reference-counted power switching for secondary SOCs (e.g., a media SOC controlled by a primary security SOC): multiple clients can hold a switch on; power cuts only when all clients release. `SwitchOnIfDevicePowered` allows conditional latching onto an already-powered subsystem without force-powering it.

### LTDevicePushButton — Physical Push Buttons
FreeRTOS uses raw GPIO ISRs. Linux uses the `input` subsystem. LTDevicePushButton delivers: press and release callbacks per button index, name↔index lookup, debounced hardware state query, and multiple simultaneous subscribers per button — without file descriptors.

### LTDevicePwm — PWM Output
FreeRTOS PWM is per-BSP. Linux uses `pwm` sysfs. LTDevicePwm provides per-pin PWM init (frequency, duty-cycle in permil [0..1000], active-high/low), start/stop, live duty-cycle update, and a secondary clock-output mode (output an internal clock signal on a GPIO pin) — no sysfs writes.

```c
LTDevicePwm *pwm = lt_openlibrary(LTDevicePwm);
LTDeviceUnit hPwm = pwm->CreateDeviceUnitHandle(0);
ILTDriverPwmDeviceUnit *iface = lt_gethandleinterface(hPwm, ILTDriverPwmDeviceUnit);
iface->InitPwmPin(18, true, 38000, 333, true);  // 38kHz IR carrier, 33.3% duty
iface->SetDutyCycle(18, 500);   // adjust to 50%
```

### LTDeviceQrreader — QR Code Reader
No equivalent in FreeRTOS or Linux standard embedded libraries. LTDeviceQrreader integrates with LTDeviceMedia for camera input: `Start(stopOnDecode)` begins scanning, fires `kLTDeviceQrreaderEvent_CodeReady`, and `GetCode` returns the decoded string. Continuous-scan mode (no auto-stop) is available for manufacturing test.

### LTDeviceRotaryEncoder — Rotary Encoder with Acceleration
FreeRTOS has no encoder abstraction. Linux uses the `input` subsystem. LTDeviceRotaryEncoder adds configurable polling intervals (minimum 10ms, maximum 100ms), acceleration (position changes in larger increments at higher rotation speeds), and named-encoder lookup.

### LTDeviceRTC — Real-Time Clock with Alarm
FreeRTOS has no standard RTC API. Linux uses the `rtc` character device (ioctl). LTDeviceRTC provides ISR-safe `GetTimeUTC`/`SetTimeUTC` using `LTTime` (nanosecond-resolution), and an `EnableAlarmInterrupt` that fires an ISR-safe callback at a specified UTC time — integrated with the LT type system, no ioctl.

```c
LTDeviceRTC *rtc = lt_createobject(LTDeviceRTC);
rtc->API->SetTimeUTC(rtc, networkTime);
LTTime wakeAt = LTTime_AddSeconds(rtc->API->GetTimeUTC(rtc), 3600);
rtc->API->EnableAlarmInterrupt(rtc, wakeAt, myAlarmISR, myData);
```

### LTDeviceSDCard — SD Card
FreeRTOS uses FatFs directly. Linux uses kernel block device. LTDeviceSDCard adds: event-driven insert/remove callbacks, `GetSDCardInfo` (present, mounted, fstype: FAT32/exFAT, capacity, available bytes in one call), mount/unmount, and on-device format.

### LTDeviceSdio — SDIO Host (WiFi/BT chips)
No standard SDIO abstraction in FreeRTOS or Linux user space. LTDeviceSdio is an LTObject-based SDIO host for multi-function cards (e.g., WiFi chips): enumerate cards by vendor/product ID, assign exclusive function handles, reserve/release the bus for thread-safe access, direct (CMD52) and block (CMD53) read/write, per-function interrupt handlers with automatic card-status interrogation, block-size management, and isochronous transfer abort.

### LTDeviceSerialPort — UART Serial Port
FreeRTOS UART is per-BSP. Linux uses termios (`/dev/ttyS*`). LTDeviceSerialPort provides: acquire (claim a named port with baud/data-bits/parity/stop-bits), release, connect (register ISR-safe receive-char and status callbacks), `SendChars`/`SendCString`/`SendChar`/`SendBreak` — no file descriptors, no termios.

```c
LTSerialPort *port = lt_createobject(LTSerialPort);
port->API->Acquire(port, "debug_uart", 115200, false, 8, LTDeviceSerialPort_NoParity, 1);
port->API->Connect(port, myRxCallback, NULL, myCtx);
port->API->SendCString(port, "LT boot complete\n");
```

### LTDeviceSleepControl — Configurable Sleep with Wakeup Sources
Builds on LTDevicePower with finer wakeup-source control (RTC, UART RX, key press, capacitive touch, or any). Provides pre-sleep and post-wake task callbacks, sleep duration limiting, actual-sleep-duration measurement, and ship-mode (ultra-deep sleep). Designed for battery-powered devices requiring millisecond-accurate sleep tracking.

### LTDeviceSlider — Logical Slider Control
No equivalent in FreeRTOS or Linux standard. Wraps key-input events into a bounded integer value with configurable range, initial value, max-step-per-event, and periodic or event-driven callback delivery. The callback always fires in the caller's thread context, avoiding extra synchronization.

### LTDeviceSPI — SPI Bus
FreeRTOS SPI is per-BSP. Linux uses `spidev` (ioctl). LTDeviceSPI adds: named bus lookup, capability discovery (DMA, async, master/slave, hardware/bit-bang, frequency range, bit-width range), mode enumeration (CPOL/CPHA 0–3), full-duplex transfer with async callback — no ioctl.

### LTDeviceThermometer — Temperature Sensors
FreeRTOS has no thermometer abstraction. Linux uses `hwmon` (sysfs). LTDeviceThermometer returns temperature in millidegrees Celsius from a named device unit — one function call, no sysfs path construction.

```c
LTDeviceThermometer *therm = lt_openlibrary(LTDeviceThermometer);
LTDeviceUnit hSensor = therm->CreateDeviceUnitHandle(0);
s32 temp = lt_gethandleinterface(hSensor, ILTThermometerDeviceUnit)->Read(hSensor);
lt_consoleprint("Temp: %d.%d C\n", temp / 10, temp % 10);
```

### LTDeviceUSB (Client/CDC/Host/Webcam/Serial) — USB Stack
FreeRTOS uses vendor USB stacks (TinyUSB, etc.) with no standard API. Linux uses `libusb`/kernel gadget. LT's USB suite provides: **LTDeviceUsbClient** (raw endpoint control with request handler, FIFO-only write batching), **LTDeviceUsbCDC** (virtual serial port in client or host mode, dual-mode switch per object specialization), **LTDeviceUsbHost** (control/bulk/interrupt/isochronous transfers with async or synchronous-timeout mode, device-status events with full configuration descriptor, descriptor enumeration callback), and **LTDeviceUSBSerial** (host-mode CDC serial access). All use LTObject abstraction — no file descriptors or libusb handles.

```c
// USB CDC device mode
LTDeviceUsbCDC *cdc = lt_createobject(LTDeviceUsbCDC);
cdc->API->Init(cdc, "usb0", myThread, onReadReady, onWriteReady, onError, myCtx);
cdc->API->Start(cdc);
// when onReadReady fires:
u8 buf[64];
s32 n = cdc->API->Read(cdc, buf, sizeof(buf));
cdc->API->Write(cdc, buf, n);   // echo
```

### LTDeviceVideo — Video Encoder Pipeline
FreeRTOS has no video pipeline abstraction. Linux uses V4L2 (kernel-heavy). LTDeviceVideo manages multi-channel video (H.264-HD, H.264-SD, JPEG-HD, JPEG-SD, ISP-HD YUV420, ISP-SD YUV420) from a single source (image sensor), including: ISP tuning data, day/night mode with auto-switch, rotation/flip, WDR, auto-exposure, OSD logo and timestamp overlay, per-channel bitrate and GOP control, sync/async frame capture, crop-capture directly from ISP buffer, IDR frame request, and frame event subscription per channel. `PutMessagePack` in LTUtilityMessagePack is the typical transport for video metadata.

```c
LTDeviceVideo *video = lt_openlibrary(LTDeviceVideo);
video->Enable(kLTDeviceVideo_Source_0);
video->Start(kLTDeviceVideo_Channel_H264HD);
video->OnVideoEvent(kLTDeviceVideo_Channel_H264HD, myFrameEventCb, NULL);
// in callback:
//   videoData->address, videoData->length, videoData->type, videoData->time
//   video->ReleaseVideoData(channel, videoData);
```

### LTDeviceWatchdog — Watchdog with Thread-Level Coverage
FreeRTOS watchdog is a BSP `HAL_IWDG_Refresh()` call. Linux uses `/dev/watchdog` (ioctl). LTDeviceWatchdog's key innovation is `WatchThread`: any thread calls `WatchThread(responseFidelity, terminationAllowed)` and the watchdog automatically queues a probe task to that thread at the specified interval — no manual tickling, no inter-thread coordination. If the thread fails to process the probe within `responseFidelity`, the system reboots. `GetBootReason` distinguishes watchdog reset from power-on, software reboot, etc. `Reboot` notifies registered callbacks before rebooting (unlike watchdog timeout reboots).

```c
LTDeviceWatchdog *wdg = lt_openlibrary(LTDeviceWatchdog);
wdg->EnableTimer();
wdg->SetTimeout(LTTime_Seconds(30));
// in each critical thread:
wdg->WatchThread(LTTime_Seconds(10), false);  // must respond every 10s or system reboots
```

### LTDeviceWiFi — Wi-Fi STA/AP
FreeRTOS uses vendor SDKs (ESP-IDF, RW61x). Linux uses `wpa_supplicant` + nl80211. LTDeviceWiFi provides: granular status callbacks (Reset → Down → Up → ScanStart → ScanResult → ScanDone → JoinStart → JoinAssociated → JoinAuthenticated → JoinDone → Connected), AP settings load/save to `LTSystemSettings` (persisted credentials), channel-aware scanning, soft-AP mode, direct frame receive/transmit (scatter-gather `LTBufferChain`) for the network stack, TX queue stats per access category, and `IwPriv` for vendor-specific diagnostics — all without `wpa_supplicant`, sockets, or netlink.

```c
LTDeviceWiFi *wifi = lt_openlibrary(LTDeviceWiFi);
wifi->OnStatusChange(myStatusCallback, NULL, NULL);
LTWiFi_ApInfo ap;
wifi->LoadApSettings(&ap);           // load saved credentials
wifi->JoinAp(&ap, myJoinCallback, NULL);
// wait for kLTDeviceWiFi_Status_Connected in status callback
```

### LTDeviceFloodlight / LTDeviceFlPIR / LTDeviceFLUnit / LTDeviceFloodlightAccessory — Floodlight System
Product-specific libraries for Roku outdoor security cameras — no FreeRTOS or Linux equivalent. **LTDeviceFloodlight**: brightness (0–100%), flash frequency (0–10Hz), accommodation time (transition speed between brightness levels), on/off. **LTDeviceFlPIR**: multi-zone PIR (Left/Middle/Right bitmask), configurable algorithm (threshold vs. slope-weighted for false-alarm reduction), sensitivity, motion delay. **LTDeviceFLUnit**: proprietary protocol to a secondary floodlight accessory MCU over USB serial (get/set commands, PIR trigger events, authentication, SCM restart). **LTDeviceFloodlightAccessory**: unified endpoint/setting model for the full accessory (PIR + floodlight + system endpoints) with overheating and brightness-change events.

---

## Summary

The LT OS device library suite delivers **50+ standardized, platform-independent device abstractions** — from ADC and GPIO through BLE, video, NPU, and OTA — using a single Device/Driver split model with JSON-described configuration, all without libc, file descriptors, syscalls, or RTOS-specific porting work. The same application source code compiles and runs on every supported embedded and hosted platform with zero conditional compilation.
