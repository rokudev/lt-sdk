/******************************************************************************
 * Esp32_Gdma.h                                                    ESP32-P4 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The esp32p4's general purpose DMA, which is a Synopsys DesignWare AXI DMA
 * rather than Espressif's own GDMA - so the register names below are the ones
 * in the DWC databook, and nothing about the esp32c3's GDMA carries over.
 *
 * Four channels.  Each has a 0x100 byte register block, the first at base +
 * 0x100, and is told what to move either by writing the addresses and the
 * block size straight into its registers or by pointing it at a linked list of
 * descriptors in memory.  The DSI bridge wants the list form: it is the only
 * way to make a transfer repeat without the CPU re-arming it between frames.
 *
 * Two AXI master ports, and which one a transfer uses is part of its
 * configuration.  Master 0 reaches the DSI and CSI bridges; master 1 reaches
 * memory - L2MEM, ROM, and flash and PSRAM through the MSPI.  Getting this
 * backwards does not fault, it simply never transfers.
 *
 * Values here were taken from the ESP-IDF v5.4 esp32p4 soc and hal headers -
 * dw_gdma_reg.h and dw_gdma_ll.h - and are limited to what the DSI driver
 * touches, following Esp32_Registers.h.
 */

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32P4_GDMA_H
#define PLATFORMS_ESP32_INCLUDE_ESP32P4_GDMA_H

#include "Esp32_Registers.h"

enum Esp32_GdmaLimits {
    kEsp32_Gdma_ChannelCount             = 4,
    kEsp32_Gdma_ChannelBase              = 0x100,   /* channel 0's block, from the GDMA base */
    kEsp32_Gdma_ChannelStride            = 0x100,

    /* The descriptor is 64 bytes and the hardware takes only bits 31:6 of its
     * address, so it has to sit on a 64 byte boundary */
    kEsp32_Gdma_DescriptorAlignment      = 64,
};

/*
 * Global registers, offsets from the GDMA base.
 *
 * CHEN0 is written rather than read-modify-written: each channel has a control
 * bit and a write-enable bit for it, so a write naming one channel leaves the
 * other three alone.  The four encodings below are the whole of what it does.
 * RESET0's reset bit self clears, and is the only way to recover a channel that
 * has latched a bus error.
 */
enum Esp32_RegisterGDMA {
    kEsp32_RegisterGDMA_CFG0                          = ESP32_REG_BASE(GDMA) + 0x10,
    kEsp32_RegisterGDMA_CFG0_DMAC_EN_M                = 0x01 << 0,
    kEsp32_RegisterGDMA_CFG0_INT_EN_M                 = 0x01 << 1,

    kEsp32_RegisterGDMA_CHEN0                         = ESP32_REG_BASE(GDMA) + 0x18,
    kEsp32_RegisterGDMA_CHEN1                         = ESP32_REG_BASE(GDMA) + 0x1c,

    kEsp32_RegisterGDMA_RESET0                        = ESP32_REG_BASE(GDMA) + 0x58,
    kEsp32_RegisterGDMA_RESET0_DMAC_RST_M             = 0x01 << 0,
};

/* Channel n's CHEN0 words.  ABORT goes to CHEN1 instead and self clears. */
#define ESP32_GDMA_CHEN_ENABLE(n)          (0x00000101u << (n))
#define ESP32_GDMA_CHEN_DISABLE(n)         (0x00000100u << (n))
#define ESP32_GDMA_CHEN_SUSPEND(n)         (0x01010000u << (n))
#define ESP32_GDMA_CHEN_RESUME(n)          (0x01000000u << (n))
#define ESP32_GDMA_CHEN_ABORT(n)           (0x00000101u << (n))

/*
 * Per channel registers, offsets from a channel's own block.
 *
 * BLOCK_TS holds one less than the number of items to move, and an "item" is
 * one unit of the source transfer width rather than one byte.  LLP holds the
 * descriptor address shifted right by six, not the address.
 */
enum Esp32_RegisterGDMA_CH {
    kEsp32_RegisterGDMA_CH_SAR0                       = 0x00,
    kEsp32_RegisterGDMA_CH_SAR1                       = 0x04,
    kEsp32_RegisterGDMA_CH_DAR0                       = 0x08,
    kEsp32_RegisterGDMA_CH_DAR1                       = 0x0c,

    kEsp32_RegisterGDMA_CH_BLOCK_TS0                  = 0x10,
    kEsp32_RegisterGDMA_CH_BLOCK_TS0_M                = 0x3fffff << 0,

    /*
     * SMS and DMS are the AXI master each side uses; SINC and DINC are
     * inverted, so a set bit holds the address still and a clear bit advances
     * it.  The widths are log2 of the bus width in bytes less three - 0 is 8
     * bits, 6 is 512 - and MSIZE is log2 of the burst item count.
     */
    kEsp32_RegisterGDMA_CH_CTL0                       = 0x18,
    kEsp32_RegisterGDMA_CH_CTL0_SMS_M                 = 0x01 << 0,
    kEsp32_RegisterGDMA_CH_CTL0_DMS_M                 = 0x01 << 2,
    kEsp32_RegisterGDMA_CH_CTL0_SINC_M                = 0x01 << 4,
    kEsp32_RegisterGDMA_CH_CTL0_DINC_M                = 0x01 << 6,
    kEsp32_RegisterGDMA_CH_CTL0_SRC_TR_WIDTH_S        = 8,
    kEsp32_RegisterGDMA_CH_CTL0_SRC_TR_WIDTH_M        = 0x07 << 8,
    kEsp32_RegisterGDMA_CH_CTL0_DST_TR_WIDTH_S        = 11,
    kEsp32_RegisterGDMA_CH_CTL0_DST_TR_WIDTH_M        = 0x07 << 11,
    kEsp32_RegisterGDMA_CH_CTL0_SRC_MSIZE_S           = 14,
    kEsp32_RegisterGDMA_CH_CTL0_SRC_MSIZE_M           = 0x0f << 14,
    kEsp32_RegisterGDMA_CH_CTL0_DST_MSIZE_S           = 18,
    kEsp32_RegisterGDMA_CH_CTL0_DST_MSIZE_M           = 0x0f << 18,

    /*
     * The ARLEN/AWLEN pair is the AXI burst length, and its enable bit has to
     * be set for the length to be used at all.  The two top bits are the
     * descriptor's own: LAST marks the end of a list and VALID is what the
     * hardware clears as it consumes a descriptor, so a list that is to repeat
     * has to have them set again before it is handed back.
     */
    kEsp32_RegisterGDMA_CH_CTL1                       = 0x1c,
    kEsp32_RegisterGDMA_CH_CTL1_ARLEN_EN_M            = 0x01 << 6,
    kEsp32_RegisterGDMA_CH_CTL1_ARLEN_S               = 7,
    kEsp32_RegisterGDMA_CH_CTL1_ARLEN_M               = 0xff << 7,
    kEsp32_RegisterGDMA_CH_CTL1_AWLEN_EN_M            = 0x01 << 15,
    kEsp32_RegisterGDMA_CH_CTL1_AWLEN_S               = 16,
    kEsp32_RegisterGDMA_CH_CTL1_AWLEN_M               = 0xff << 16,
    kEsp32_RegisterGDMA_CH_CTL1_IOC_BLKTFR_M          = 0x01 << 26,
    kEsp32_RegisterGDMA_CH_CTL1_LLI_LAST_M            = 0x01u << 30,
    kEsp32_RegisterGDMA_CH_CTL1_LLI_VALID_M           = 0x01u << 31,

    /* How each side gets its next transfer: 0 contiguous, 1 reload, 2 shadow
     * register, 3 linked list */
    kEsp32_RegisterGDMA_CH_CFG0                       = 0x20,
    kEsp32_RegisterGDMA_CH_CFG0_SRC_MULTBLK_TYPE_S    = 0,
    kEsp32_RegisterGDMA_CH_CFG0_SRC_MULTBLK_TYPE_M    = 0x03 << 0,
    kEsp32_RegisterGDMA_CH_CFG0_DST_MULTBLK_TYPE_S    = 2,
    kEsp32_RegisterGDMA_CH_CFG0_DST_MULTBLK_TYPE_M    = 0x03 << 2,

    /*
     * TT_FC is the transfer direction and which block flow controls it; the
     * HS_SEL bits are inverted, so clearing one asks for hardware handshaking
     * on that side.  The OSR_LMT fields take one less than the number of
     * outstanding requests allowed.
     */
    kEsp32_RegisterGDMA_CH_CFG1                       = 0x24,
    kEsp32_RegisterGDMA_CH_CFG1_TT_FC_S               = 0,
    kEsp32_RegisterGDMA_CH_CFG1_TT_FC_M               = 0x07 << 0,
    kEsp32_RegisterGDMA_CH_CFG1_HS_SEL_SRC_M          = 0x01 << 3,
    kEsp32_RegisterGDMA_CH_CFG1_HS_SEL_DST_M          = 0x01 << 4,
    kEsp32_RegisterGDMA_CH_CFG1_SRC_PER_S             = 7,
    kEsp32_RegisterGDMA_CH_CFG1_SRC_PER_M             = 0x03 << 7,
    kEsp32_RegisterGDMA_CH_CFG1_DST_PER_S             = 12,
    kEsp32_RegisterGDMA_CH_CFG1_DST_PER_M             = 0x03 << 12,
    kEsp32_RegisterGDMA_CH_CFG1_CH_PRIOR_S            = 17,
    kEsp32_RegisterGDMA_CH_CFG1_CH_PRIOR_M            = 0x07 << 17,
    kEsp32_RegisterGDMA_CH_CFG1_SRC_OSR_LMT_S         = 23,
    kEsp32_RegisterGDMA_CH_CFG1_SRC_OSR_LMT_M         = 0x0f << 23,
    kEsp32_RegisterGDMA_CH_CFG1_DST_OSR_LMT_S         = 27,
    kEsp32_RegisterGDMA_CH_CFG1_DST_OSR_LMT_M         = 0x0fu << 27,

    /* LMS is the AXI master the descriptors themselves are fetched over */
    kEsp32_RegisterGDMA_CH_LLP0                       = 0x28,
    kEsp32_RegisterGDMA_CH_LLP0_LMS_M                 = 0x01 << 0,
    kEsp32_RegisterGDMA_CH_LLP0_LOC_S                 = 6,
    kEsp32_RegisterGDMA_CH_LLP1                       = 0x2c,

    kEsp32_RegisterGDMA_CH_INT_ST_ENA0                = 0x80,
    kEsp32_RegisterGDMA_CH_INT_ST0                    = 0x88,
    kEsp32_RegisterGDMA_CH_INT_SIG_ENA0               = 0x90,
    kEsp32_RegisterGDMA_CH_INT_CLR0                   = 0x98,
};

/* Channel event bits, shared by a channel's INT_ST0, INT_CLR0 and the two
 * enables.  Only the ones the DSI driver looks at. */
enum Esp32_GdmaChannelEvents {
    kEsp32_GdmaEvent_BlockTransferDone   = 0x01u << 0,
    kEsp32_GdmaEvent_DmaTransferDone     = 0x01u << 1,
    kEsp32_GdmaEvent_SrcDecodeError      = 0x01u << 5,
    kEsp32_GdmaEvent_DstDecodeError      = 0x01u << 6,
    kEsp32_GdmaEvent_SrcSlaveError       = 0x01u << 7,
    kEsp32_GdmaEvent_DstSlaveError       = 0x01u << 8,
    kEsp32_GdmaEvent_Disabled            = 0x01u << 30,
    kEsp32_GdmaEvent_Aborted             = 0x01u << 31,
};

/* AXI master ports.  Which peripheral is on which is fixed by the wiring. */
enum Esp32_GdmaMasterPort {
    kEsp32_GdmaMaster_MipiDsi            = 0,
    kEsp32_GdmaMaster_Memory             = 1,
};

/* CFG1.TT_FC, the transfer direction and the block that paces it */
enum Esp32_GdmaTransferFlow {
    kEsp32_GdmaFlow_MemToMem_Dmac        = 0,
    kEsp32_GdmaFlow_MemToPeri_Dmac       = 1,
    kEsp32_GdmaFlow_PeriToMem_Dmac       = 2,
};

/* CFG1.SRC_PER / DST_PER, the handshake interface a peripheral is wired to */
enum Esp32_GdmaHandshakePeripheral {
    kEsp32_GdmaHandshake_MipiDsi         = 0,
    kEsp32_GdmaHandshake_MipiCsi         = 1,
    kEsp32_GdmaHandshake_Isp             = 2,
};

/* CFG0.SRC_MULTBLK_TYPE / DST_MULTBLK_TYPE */
enum Esp32_GdmaBlockTransferType {
    kEsp32_GdmaBlockTransfer_Contiguous  = 0,
    kEsp32_GdmaBlockTransfer_Reload      = 1,
    kEsp32_GdmaBlockTransfer_Shadow      = 2,
    kEsp32_GdmaBlockTransfer_LinkedList  = 3,
};

/* CTL0.SRC_TR_WIDTH / DST_TR_WIDTH, log2 of the bus width in bits less three */
enum Esp32_GdmaTransferWidth {
    kEsp32_GdmaTransferWidth_8           = 0,
    kEsp32_GdmaTransferWidth_16          = 1,
    kEsp32_GdmaTransferWidth_32          = 2,
    kEsp32_GdmaTransferWidth_64          = 3,
    kEsp32_GdmaTransferWidth_128         = 4,
    kEsp32_GdmaTransferWidth_256         = 5,
    kEsp32_GdmaTransferWidth_512         = 6,
};

/* CTL0.SRC_MSIZE / DST_MSIZE, log2 of the number of items in a burst */
enum Esp32_GdmaBurstSize {
    kEsp32_GdmaBurstSize_1               = 0,
    kEsp32_GdmaBurstSize_256             = 8,
    kEsp32_GdmaBurstSize_512             = 9,
};

/*
 * A linked list descriptor, as the hardware reads it.
 *
 * Laid out so that the first eight words land on the channel registers of the
 * same name - SAR, DAR, BLOCK_TS, LLP, CTL - which is why the gaps are here
 * rather than packed out.  Must be 64 bytes and 64 byte aligned; nothing in LT
 * allocates aligned memory, so an instance has to be a static object carrying
 * LT_ALIGNED(kEsp32_Gdma_DescriptorAlignment).
 */
typedef struct Esp32_GdmaDescriptor {
    u32 nSourceAddressLow;
    u32 nSourceAddressHigh;
    u32 nDestinationAddressLow;
    u32 nDestinationAddressHigh;
    u32 nBlockTransferSize;         /* one less than the item count, as BLOCK_TS0 */
    u32 nReserved14;
    u32 nNextDescriptorLow;         /* the next descriptor's address >> 6, as LLP0 */
    u32 nNextDescriptorHigh;
    u32 nControlLow;                /* as CTL0 */
    u32 nControlHigh;               /* as CTL1, including the LAST and VALID bits */
    u32 nSourceStatus;
    u32 nDestinationStatus;
    u32 nStatusLow;
    u32 nStatusHigh;
    u32 nReserved38;
    u32 nReserved3c;
} Esp32_GdmaDescriptor;

/* The DSI bridge's DMA sink.  A fixed address: the bridge takes pixels from
 * this one location and the transfer must not increment past it. */
#define kEsp32_Gdma_MipiDsiBridgeMemory    0x50105000

/* A channel's register block */
#define ESP32_GDMA_CH_REG(ch, r)                                               \
    (*(volatile u32 *)(ESP32_REG_BASE(GDMA) + kEsp32_Gdma_ChannelBase          \
                       + (ch) * kEsp32_Gdma_ChannelStride                      \
                       + kEsp32_RegisterGDMA_CH_ ## r))

#endif /* PLATFORMS_ESP32_INCLUDE_ESP32P4_GDMA_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  30-Sep-26   dwoodward   created
 */
