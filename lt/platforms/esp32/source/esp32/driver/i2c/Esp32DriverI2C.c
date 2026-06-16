/******************************************************************************
 * platforms/esp32/source/esp32/driver/i2c/Esp32DriverI2C.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * ESP32 hardware I²C master driver implementing the ILTDriverI2C interface.
 *
 * The ESP32 has two I²C controllers (port 0 and port 1).  Each is driven by a
 * command-list engine: the host loads up to 16 command slots, preloads TX bytes
 * into the hardware TX FIFO, then writes the TRANS_START bit.  The hardware
 * serialises START / WRITE / READ / STOP phases autonomously.  On completion
 * received bytes can be popped from the RX FIFO.  Transfers are polled
 * synchronously (no interrupt) which is correct for the low-traffic SCCB bus.
 *
 * TX and RX FIFOs are accessed through the AHB data bus window:
 *   I2C0: 0x6001301c   I2C1: 0x6002701c
 * Writing pushes a byte to the TX FIFO; reading pops a byte from the RX FIFO.
 *
 * Configuration keys read from LTDeviceConfig.json per unit:
 *   name  — logical bus name (e.g. "sccb")
 *   port  — hardware port number: 0 or 1
 *   sda   — GPIO number for SDA
 *   scl   — GPIO number for SCL
 *
 * Maximum payload per call: 30 bytes (TX or RX), matching the 32-byte FIFO
 * minus 1-2 bytes of address overhead.  SCCB writes are 3 bytes and reads
 * return 1 byte, well within this limit.
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/config/LTDeviceConfig.h>

#include "esp32/Esp32_Registers.h"
#include "esp32/Esp32_Clock.h"
#include "esp32/Esp32_GPIO.h"

DEFINE_LTLOG_SECTION("esp32.drv.i2c");

/* Instance state */
typedef struct {
    const char                *name;
    u8                         port;
    u8                         sdaGpio;
    u8                         sclGpio;
    u32                        nRefCount;
    LTMutex                   *mutex;
    LTDeviceI2C_Configuration  cfg;
    LTDeviceI2C_Capabilities   caps;
} I2C_Instance;

static struct {
    I2C_Instance *pInstances;
    u32           nCount;
} S;

/* =========================================================================
 * ESP32 I²C register file
 *
 * Peripheral base (through DPORT address space):
 *   I2C0: 0x3FF53000   I2C1: 0x3FF67000  (stride 0x14000)
 * ========================================================================= */

#define I2C_DPORT_BASE(port)    (0x3FF53000UL + (u32)(port) * 0x14000UL)
#define I2C_REG(port, off)      (*(volatile u32 *)(I2C_DPORT_BASE(port) + (off)))

/* AHB data-bus window for FIFO push/pop (different address decode from DPORT) */
#define I2C_FIFO_AHB(port)      ((volatile u32 *)(0x60013000UL + (u32)(port) * 0x14000UL + 0x001CUL))

/* Register offsets */
#define I2C_SCL_LOW_PERIOD_OFF   0x0000
#define I2C_CTR_OFF              0x0004
#define I2C_SR_OFF               0x0008
#define I2C_TO_OFF               0x000C
#define I2C_FIFO_CONF_OFF        0x0018
#define I2C_INT_RAW_OFF          0x0020
#define I2C_INT_CLR_OFF          0x0024
#define I2C_INT_ENA_OFF          0x0028
#define I2C_SDA_HOLD_OFF         0x0030
#define I2C_SDA_SAMPLE_OFF       0x0034
#define I2C_SCL_HIGH_PERIOD_OFF  0x0038
#define I2C_SCL_START_HOLD_OFF   0x0040
#define I2C_SCL_RSTART_SETUP_OFF 0x0044
#define I2C_SCL_STOP_HOLD_OFF    0x0048
#define I2C_SCL_STOP_SETUP_OFF   0x004C
#define I2C_SCL_FILTER_OFF       0x0050
#define I2C_SDA_FILTER_OFF       0x0054
#define I2C_COMD0_OFF            0x0058  /* command[0..15]: offsets 0x58, 0x5C … 0x94 */

/* CTR bits */
#define CTR_SDA_FORCE_OUT   (1u << 0)
#define CTR_SCL_FORCE_OUT   (1u << 1)
#define CTR_MS_MODE         (1u << 4)   /* 1 = master */
#define CTR_TRANS_START     (1u << 5)
#define CTR_CLK_EN          (1u << 8)

/* INT_RAW bits */
#define INT_TRANS_COMPLETE  (1u << 7)
#define INT_TIME_OUT        (1u << 8)
#define INT_ACK_ERR         (1u << 10)
#define INT_ARB_LOST        (1u << 5)

/* FIFO_CONF bits */
#define FIFO_RX_RST         (1u << 12)
#define FIFO_TX_RST         (1u << 13)

/* Command engine opcodes (op_code field at bits [13:11]) */
#define CMD_OP_RSTART   0u
#define CMD_OP_WRITE    1u
#define CMD_OP_READ     2u
#define CMD_OP_STOP     3u
#define CMD_OP_END      4u

#define CMD_ACK_EN      (1u << 8)   /* check ACK from slave during WRITE */
#define CMD_ACK_EXP     (1u << 9)   /* expected ACK value: 0 = ACK */
#define CMD_ACK_VAL     (1u << 10)  /* ACK to send after last READ byte: 1 = NACK */

/* APB clock is 80 MHz on all ESP32 variants */
#define APB_CLK_HZ      80000000UL

/* GPIO matrix signal indices for I²C (from soc/gpio_sig_map.h) */
#define I2C0_SDA_SIG    30u   /* I2CEXT0_SDA_IN_IDX / I2CEXT0_SDA_OUT_IDX */
#define I2C0_SCL_SIG    29u   /* I2CEXT0_SCL_IN_IDX / I2CEXT0_SCL_OUT_IDX */
#define I2C1_SDA_SIG    96u   /* I2CEXT1_SDA_IN_IDX / I2CEXT1_SDA_OUT_IDX */
#define I2C1_SCL_SIG    95u   /* I2CEXT1_SCL_IN_IDX / I2CEXT1_SCL_OUT_IDX */

/* =========================================================================
 * Instance state
 * ========================================================================= */

/* =========================================================================
 * Hardware helpers
 * ========================================================================= */

static void HwResetFifos(u8 port) {
    u32 c = I2C_REG(port, I2C_FIFO_CONF_OFF);
    I2C_REG(port, I2C_FIFO_CONF_OFF) = c | FIFO_RX_RST | FIFO_TX_RST;
    I2C_REG(port, I2C_FIFO_CONF_OFF) = c & ~(FIFO_RX_RST | FIFO_TX_RST);
}

static void HwClearInterrupts(u8 port) {
    I2C_REG(port, I2C_INT_ENA_OFF) = 0u;
    I2C_REG(port, I2C_INT_CLR_OFF) = 0x1FFFu;
}

static void HwConfigure(u8 port, u32 freq) {
    if (!freq) freq = 100000u;

    /* Half-period in APB clock counts, clamped to at least 1 */
    u32 half = APB_CLK_HZ / (2u * freq);
    if (!half) half = 1u;

    I2C_REG(port, I2C_SCL_LOW_PERIOD_OFF)   = half - 1u;
    I2C_REG(port, I2C_SCL_HIGH_PERIOD_OFF)  = half - 1u;
    I2C_REG(port, I2C_SDA_HOLD_OFF)         = half / 2u;
    I2C_REG(port, I2C_SDA_SAMPLE_OFF)       = half / 2u;
    I2C_REG(port, I2C_SCL_START_HOLD_OFF)   = half;
    I2C_REG(port, I2C_SCL_RSTART_SETUP_OFF) = half;
    I2C_REG(port, I2C_SCL_STOP_HOLD_OFF)    = half;
    I2C_REG(port, I2C_SCL_STOP_SETUP_OFF)   = half;

    /* Timeout: 20-bit field, set to maximum (~13 ms at 80 MHz APB) */
    I2C_REG(port, I2C_TO_OFF) = 0xFFFFFu;

    /* Glitch filters: enabled with threshold = 7 APB clocks */
    I2C_REG(port, I2C_SCL_FILTER_OFF) = (1u << 3) | 7u;
    I2C_REG(port, I2C_SDA_FILTER_OFF) = (1u << 3) | 7u;

    /* Master mode, push-pull SCL/SDA output enables, clock gate on */
    I2C_REG(port, I2C_CTR_OFF) = CTR_SDA_FORCE_OUT | CTR_SCL_FORCE_OUT
                                | CTR_MS_MODE | CTR_CLK_EN;
}

static void HwConnectGpio(u8 port, u8 sdaPin, u8 sclPin) {
    u8 sdaSig = (port == 0u) ? (u8)I2C0_SDA_SIG : (u8)I2C1_SDA_SIG;
    u8 sclSig = (port == 0u) ? (u8)I2C0_SCL_SIG : (u8)I2C1_SCL_SIG;

    /* SDA: open-drain with pull-up; bidirectional through GPIO matrix */
    Esp32GPIO_ConfigPin(sdaPin, kEsp32GPIO_Direction_Input,
                        kEsp32GPIO_PullUp, kEsp32GPIO_Function_GPIO);
    Esp32GPIO_ConfigOutputType(sdaPin, kEsp32GPIO_OutputType_OpenDrain);
    Esp32GPIO_ConfigMatrixPin(sdaPin, sdaSig, kEsp32GPIO_Direction_Output, false);
    Esp32GPIO_ConfigMatrixPin(sdaPin, sdaSig, kEsp32GPIO_Direction_Input,  false);

    /* SCL: open-drain with pull-up; loopback input needed for clock-stretch */
    Esp32GPIO_ConfigPin(sclPin, kEsp32GPIO_Direction_Input,
                        kEsp32GPIO_PullUp, kEsp32GPIO_Function_GPIO);
    Esp32GPIO_ConfigOutputType(sclPin, kEsp32GPIO_OutputType_OpenDrain);
    Esp32GPIO_ConfigMatrixPin(sclPin, sclSig, kEsp32GPIO_Direction_Output, false);
    Esp32GPIO_ConfigMatrixPin(sclPin, sclSig, kEsp32GPIO_Direction_Input,  false);
}

/* Spin-poll until TRANS_COMPLETE or an error flag appears.
 * Returns true on success, false on timeout / ACK error / arbitration loss.
 * On error the FIFO is reset and the caller should re-establish config. */
/* Full hardware reset of an I2C port: disable clock, re-enable, reconfigure.
 * Called after timeout to recover from a bus stall (slave clock-stretching). */
static void HwFullReset(u8 port, u32 freq) {
    u32 clkBit = (port == 0u) ? kEsp32_Clock_I2C_EXT0 : kEsp32_Clock_I2C_EXT1;
    Esp32_ClockDisablePeripheralClock(clkBit);
    for (volatile u32 i = 0u; i < 200u; ++i) {}
    Esp32_ClockEnablePeripheralClock(clkBit);
    HwResetFifos(port);
    HwClearInterrupts(port);
    HwConfigure(port, freq);
}

static bool HwWaitDone(u8 port) {
    for (u32 i = 0u; i < 200000u; ++i) {
        u32 raw = I2C_REG(port, I2C_INT_RAW_OFF);
        if (raw & INT_TRANS_COMPLETE) {
            I2C_REG(port, I2C_INT_CLR_OFF) = raw;
            return true;
        }
        if (raw & (INT_ACK_ERR | INT_ARB_LOST)) {
            /* NACK or bus collision — bus recovers automatically after STOP */
            LTLOG("wait.err", "int_raw=0x%lx port=%lu", LT_Pu32(raw), LT_Pu32((u32)port));
            I2C_REG(port, I2C_INT_CLR_OFF) = raw;
            HwResetFifos(port);
            return false;
        }
        if (raw & INT_TIME_OUT) {
            /* Slave is clock-stretching — reset controller and let caller retry */
            LTLOG("wait.timeout", "port=%lu sr=0x%lx",
                LT_Pu32((u32)port), LT_Pu32(I2C_REG(port, I2C_SR_OFF)));
            HwFullReset(port, 100000u);
            return false;
        }
    }
    {
        LTLOG_YELLOWALERT("wait.spin", "port=%lu — no completion flag", LT_Pu32((u32)port));
        HwFullReset(port, 100000u);
    }
    return false;
}

/* Write one command-engine slot */
LT_INLINE void WriteCmd(u8 port, u8 slot, u32 cmd) {
    I2C_REG(port, I2C_COMD0_OFF + (u32)slot * 4u) = cmd;
}

LT_INLINE u32 MkWrite(u8 nBytes) {
    /* op=WRITE — SCCB does not always ACK, so don't check ACK */
    return ((u32)CMD_OP_WRITE << 11) | (u32)nBytes;
}

LT_INLINE u32 MkRead(u8 nBytes) {
    /* op=READ, NACK after the last byte so the slave releases SDA */
    return ((u32)CMD_OP_READ << 11) | CMD_ACK_VAL | (u32)nBytes;
}

/* =========================================================================
 * Core transfer engine
 *
 * SCCB (used by OV sensors) requires STOP between the register-address write
 * and the data read — it does not support repeated START for combined
 * write+read.  When both txBuf and rxBuf are provided we therefore run two
 * independent bus transactions: write-phase (with STOP), then read-phase
 * (with START+STOP).
 *
 * Single-phase (write-only or read-only) follows the issueStart/issueStop
 * flags from the caller.
 * ========================================================================= */

static bool RunPhase(u8 port, u8 addr,
                     void *rxBuf, u32 rxLen,
                     const void *txBuf, u32 txLen,
                     bool start, bool stop) {
    volatile u32 *fifo = I2C_FIFO_AHB(port);

    HwResetFifos(port);
    HwClearInterrupts(port);

    u8 slot = 0u;

    if (txBuf && txLen) {
        if (start)
            WriteCmd(port, slot++, (u32)CMD_OP_RSTART << 11);
        *fifo = (u32)((addr << 1u) | 0u);
        for (u32 i = 0u; i < txLen; ++i)
            *fifo = ((const u8 *)txBuf)[i];
        WriteCmd(port, slot++, MkWrite((u8)(txLen + 1u)));
    }

    if (rxBuf && rxLen) {
        if (start)
            WriteCmd(port, slot++, (u32)CMD_OP_RSTART << 11);
        *fifo = (u32)((addr << 1u) | 1u);
        WriteCmd(port, slot++, MkWrite(1u));
        WriteCmd(port, slot++, MkRead((u8)rxLen));
    }

    if (stop)
        WriteCmd(port, slot++, (u32)CMD_OP_STOP << 11);

    WriteCmd(port, slot, (u32)CMD_OP_END << 11);

    I2C_REG(port, I2C_CTR_OFF) |= CTR_TRANS_START;

    if (!HwWaitDone(port)) return false;

    if (rxBuf && rxLen) {
        for (u32 i = 0u; i < rxLen; ++i)
            ((u8 *)rxBuf)[i] = (u8)(*fifo & 0xFFu);
    }
    return true;
}

/* Retry a RunPhase up to maxRetries times on timeout (slave clock-stretching).
 * Each failure triggers HwFullReset (already done in HwWaitDone), then waits
 * a bit before re-attempting. */
static bool RunPhaseWithRetry(u8 port, u8 addr,
                              void *rxBuf, u32 rxLen,
                              const void *txBuf, u32 txLen,
                              bool start, bool stop) {
    for (u32 attempt = 0u; attempt < 5u; ++attempt) {
        if (RunPhase(port, addr, rxBuf, rxLen, txBuf, txLen, start, stop))
            return true;
        /* Brief delay before retry to let the sensor finish internal processing */
        for (volatile u32 w = 0u; w < 8000u; ++w) {}
    }
    return false;
}

static bool DoTransfer(I2C_Instance *inst,
                       u8 addr,
                       void *rxBuf, u32 rxLen,
                       const void *txBuf, u32 txLen,
                       bool issueStart, bool issueStop) {
    u8 port = inst->port;

    /* SCCB write+read: two separate bus transactions (STOP between them). */
    if (txBuf && txLen && rxBuf && rxLen) {
        if (!RunPhaseWithRetry(port, addr, NULL, 0, txBuf, txLen, true, true))
            return false;
        return RunPhaseWithRetry(port, addr, rxBuf, rxLen, NULL, 0, true, true);
    }

    /* Single-phase: write-only or read-only */
    return RunPhaseWithRetry(port, addr, rxBuf, rxLen, txBuf, txLen, issueStart, issueStop);
}

/* =========================================================================
 * Instance init / shutdown
 * ========================================================================= */

static bool InitInstance(I2C_Instance *inst) {
    u32 clkBit = (inst->port == 0u) ? kEsp32_Clock_I2C_EXT0 : kEsp32_Clock_I2C_EXT1;
    Esp32_ClockEnablePeripheralClock(clkBit);
    HwResetFifos(inst->port);
    HwClearInterrupts(inst->port);
    HwConnectGpio(inst->port, inst->sdaGpio, inst->sclGpio);
    HwConfigure(inst->port, inst->cfg.Frequency);
    return true;
}

static void ShutdownInstance(I2C_Instance *inst) {
    u32 clkBit = (inst->port == 0u) ? kEsp32_Clock_I2C_EXT0 : kEsp32_Clock_I2C_EXT1;
    HwClearInterrupts(inst->port);
    Esp32_ClockDisablePeripheralClock(clkBit);
    Esp32GPIO_ClearPinConfig(inst->sdaGpio);
    Esp32GPIO_ClearPinConfig(inst->sclGpio);
    if (inst->mutex) {
        lt_destroyobject(inst->mutex);
        inst->mutex = NULL;
    }
}

/* =========================================================================
 * LTDeviceConfig parsing
 * ========================================================================= */

static bool ConfigureDeviceUnit(LTDeviceConfig *cfg, I2C_Instance *inst, u32 section) {
    if (!(inst->name = cfg->ReadString(section, "name"))) {
        LTLOG_REDALERT("cdu.name", NULL); return false;
    }
    u32 port = cfg->ReadInteger(section, "port");
    if (port > 1u) {
        LTLOG_REDALERT("cdu.port", "port must be 0 or 1"); return false;
    }
    inst->port    = (u8)port;
    inst->sdaGpio = (u8)cfg->ReadInteger(section, "sda");
    inst->sclGpio = (u8)cfg->ReadInteger(section, "scl");

    inst->caps.Freq_min  = 10000u;
    inst->caps.Freq_max  = 400000u;
    inst->caps.Caps_mask = (u8)(kLTDeviceI2C_Capability_Master
                              | kLTDeviceI2C_Capability_Hardware);

    inst->cfg.Frequency = 100000u;
    inst->cfg.Master    = true;
    inst->cfg.Dma       = false;
    inst->cfg.Async     = false;

    if (!(inst->mutex = lt_createobject(LTMutex))) {
        LTLOG_REDALERT("cdu.mutex", NULL); return false;
    }
    return true;
}

static bool ConfigureDeviceUnits(void) {
    if (S.nCount || S.pInstances) return false;
    LTDeviceConfig *cfg = lt_openlibrary(LTDeviceConfig);
    if (!cfg) { LTLOG_REDALERT("cdus.cfg", NULL); return false; }

    u32 driverSection = cfg->GetDriverSection("LTDeviceI2C", "Esp32DriverI2C");
    S.nCount = cfg->GetNumDeviceUnits(driverSection);
    if (!S.nCount) { lt_closelibrary(cfg); return true; }

    S.pInstances = lt_malloc(S.nCount * sizeof(I2C_Instance));
    if (!S.pInstances) {
        LTLOG_REDALERT("cdus.oom", NULL); lt_closelibrary(cfg); return false;
    }
    lt_memset(S.pInstances, 0, S.nCount * sizeof(I2C_Instance));

    for (u32 i = 0u; i < S.nCount; ++i) {
        u32 unitSection = cfg->GetDeviceUnitSectionAt(driverSection, i);
        if (!unitSection ||
            !ConfigureDeviceUnit(cfg, &S.pInstances[i], unitSection)) {
            LTLOG_REDALERT("cdus.unit", "failed unit %lu", LT_Pu32(i));
            lt_closelibrary(cfg);
            return false;
        }
    }
    lt_closelibrary(cfg);
    return true;
}

/* =========================================================================
 * Library init / fini
 * ========================================================================= */

define_LTDEVICE_DRIVER_IMPLEMENTATION(ILTDriverI2C, Esp32DriverI2C);

static bool Esp32DriverI2CImpl_LibInit(void) {
    S.pInstances = NULL;
    S.nCount     = 0u;
    if (!ConfigureDeviceUnits()) return false;
    for (u32 i = 0u; i < S.nCount; ++i) {
        if (!InitInstance(&S.pInstances[i])) {
            LTLOG_REDALERT("lib.init", "failed init unit %lu", LT_Pu32(i));
            return false;
        }
    }
    return true;
}

static void Esp32DriverI2CImpl_LibFini(void) {
    if (S.pInstances) {
        for (u32 i = S.nCount; i; --i)
            ShutdownInstance(&S.pInstances[i - 1u]);
        lt_free(S.pInstances);
        S.pInstances = NULL;
    }
    S.nCount = 0u;
}

/* =========================================================================
 * ILTDriverI2C implementation
 * ========================================================================= */

static ILTDriverI2C s_ILTDriverI2C;

static I2C_Instance *InstanceFromHandle(LTDeviceUnit hDevice) {
    I2C_Instance **pp = (I2C_Instance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    I2C_Instance  *inst = NULL;
    if (pp) {
        inst = *pp;
        LT_GetCore()->ReleaseHandlePrivateData(hDevice, pp);
    }
    return inst;
}

static u32 Esp32DriverI2CImpl_GetNumDeviceUnits(void) { return S.nCount; }

static u32 Esp32DriverI2CImpl_GetBusIndexFromName(const char *busName) {
    for (u32 i = 0u; i < S.nCount; ++i) {
        if (lt_strcmp(busName, S.pInstances[i].name) == 0)
            return i;
    }
    return LT_U32_MAX;
}

static void OnDestroyHandle(LTHandle hDevice) {
    I2C_Instance *inst = InstanceFromHandle(hDevice);
    if (!inst) return;
    inst->mutex->API->Lock(inst->mutex);
    if (inst->nRefCount > 0u) --inst->nRefCount;
    inst->mutex->API->Unlock(inst->mutex);
}

static LTDeviceUnit Esp32DriverI2CImpl_CreateDeviceUnitHandle(u32 n) {
    if (n >= S.nCount) return 0;
    LTDeviceUnit hDevice = LT_GetCore()->CreateHandle(
        (LTInterface *)&s_ILTDriverI2C, sizeof(I2C_Instance *));
    if (!hDevice) return 0;
    bool ok = false;
    I2C_Instance **pp = (I2C_Instance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    if (pp) {
        I2C_Instance *inst = *pp = &S.pInstances[n];
        inst->mutex->API->Lock(inst->mutex);
        ++inst->nRefCount;
        inst->mutex->API->Unlock(inst->mutex);
        ok = true;
        LT_GetCore()->ReleaseHandlePrivateData(hDevice, pp);
    }
    if (!ok) { lt_destroyhandle(hDevice); return 0; }
    return hDevice;
}

static bool Esp32DriverI2CImpl_GetDeviceCapabilities(
        LTDeviceUnit unit, LTDeviceI2C_Capabilities *pCaps) {
    I2C_Instance *inst = InstanceFromHandle(unit);
    if (!inst || !pCaps) return false;
    lt_memcpy(pCaps, &inst->caps, sizeof(LTDeviceI2C_Capabilities));
    return true;
}

static bool Esp32DriverI2CImpl_GetDeviceConfiguration(
        LTDeviceUnit unit, LTDeviceI2C_Configuration *pCfg) {
    I2C_Instance *inst = InstanceFromHandle(unit);
    if (!inst || !pCfg) return false;
    lt_memcpy(pCfg, &inst->cfg, sizeof(LTDeviceI2C_Configuration));
    return true;
}

static bool Esp32DriverI2CImpl_SetDeviceConfiguration(
        LTDeviceUnit unit, const LTDeviceI2C_Configuration *pCfg) {
    I2C_Instance *inst = InstanceFromHandle(unit);
    if (!inst || !pCfg) return false;
    if (!I2C_CONFIG_CAPS_OK(pCfg, &inst->caps)) {
        LTLOG_YELLOWALERT("cfg.caps", "freq=%lu out of range", LT_Pu32(pCfg->Frequency));
        return false;
    }
    lt_memcpy(&inst->cfg, pCfg, sizeof(LTDeviceI2C_Configuration));
    HwConfigure(inst->port, inst->cfg.Frequency);
    return true;
}

static void Esp32DriverI2CImpl_SetTransferTimeout(LTDeviceUnit unit, LTTime timeout) {
    LT_UNUSED(unit);
    LT_UNUSED(timeout);
}

static bool Esp32DriverI2CImpl_I2CMasterTransfer(
        LTDeviceUnit unit, u8 addr,
        void *rxBuf, u32 rxLen,
        const void *txBuf, u32 txLen,
        bool issueStart, bool issueStop,
        LTI2C_I2CMasterTransferStatusCallback *pCallback, void *pClientData) {
    LT_UNUSED(pCallback);
    LT_UNUSED(pClientData);

    I2C_Instance *inst = InstanceFromHandle(unit);
    if (!inst) return false;
    if (!rxLen && !txLen) return false;

    /* Payload guard: TX FIFO is 32 bytes; we need up to 2 bytes for address */
    if (txLen > 30u || rxLen > 30u) {
        LTLOG_REDALERT("xfer.oversize", "tx=%lu rx=%lu", LT_Pu32(txLen), LT_Pu32(rxLen));
        return false;
    }

    inst->mutex->API->Lock(inst->mutex);
    bool ok = DoTransfer(inst, addr, rxBuf, rxLen, txBuf, txLen, issueStart, issueStop);
    inst->mutex->API->Unlock(inst->mutex);
    return ok;
}

static bool Esp32DriverI2CImpl_Reset(LTDeviceUnit unit) {
    I2C_Instance *inst = InstanceFromHandle(unit);
    if (!inst) return false;
    inst->mutex->API->Lock(inst->mutex);
    HwResetFifos(inst->port);
    HwClearInterrupts(inst->port);
    HwConfigure(inst->port, inst->cfg.Frequency);
    inst->mutex->API->Unlock(inst->mutex);
    return true;
}

static bool Esp32DriverI2CImpl_ProbeAddress(LTDeviceUnit unit, u8 addr) {
    I2C_Instance *inst = InstanceFromHandle(unit);
    if (!inst) return false;

    inst->mutex->API->Lock(inst->mutex);

    u8  port = inst->port;
    volatile u32 *fifo = I2C_FIFO_AHB(port);

    HwResetFifos(port);
    HwClearInterrupts(port);

    /* START + WRITE(addr|W, 1 byte) + STOP — ACK = device present */
    *fifo = (u32)((addr << 1u) | 0u);
    WriteCmd(port, 0, (u32)CMD_OP_RSTART << 11);
    WriteCmd(port, 1, MkWrite(1u));
    WriteCmd(port, 2, (u32)CMD_OP_STOP << 11);
    WriteCmd(port, 3, (u32)CMD_OP_END << 11);

    I2C_REG(port, I2C_CTR_OFF) |= CTR_TRANS_START;
    bool present = HwWaitDone(port);

    inst->mutex->API->Unlock(inst->mutex);

    LTLOG_DEBUG("probe", "addr=0x%lx %s",
        LT_Pu32((u32)addr), present ? "ACK" : "NACK");
    return present;
}

/* =========================================================================
 * Interface and library root binding
 * ========================================================================= */

define_LTLIBRARY_INTERFACE(ILTDriverI2C, OnDestroyHandle) {
    .GetBusIndexFromName    = Esp32DriverI2CImpl_GetBusIndexFromName,
    .GetDeviceCapabilities  = Esp32DriverI2CImpl_GetDeviceCapabilities,
    .GetDeviceConfiguration = Esp32DriverI2CImpl_GetDeviceConfiguration,
    .SetDeviceConfiguration = Esp32DriverI2CImpl_SetDeviceConfiguration,
    .SetTransferTimeout     = Esp32DriverI2CImpl_SetTransferTimeout,
    .I2CMasterTransfer      = Esp32DriverI2CImpl_I2CMasterTransfer,
    .Reset                  = Esp32DriverI2CImpl_Reset,
    .ProbeAddress           = Esp32DriverI2CImpl_ProbeAddress,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(Esp32DriverI2C, (ILTDriverI2C))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  02-Jun-26   created — hardware I²C master for ESP32-CAM SCCB bus
 */
