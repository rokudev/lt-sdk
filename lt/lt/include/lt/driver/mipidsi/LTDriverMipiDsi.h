/*******************************************************************************
 * <lt/driver/mipidsi/LTDriverMipiDsi.h>
 *
 * Driver interface for MIPI-DSI display host controllers.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 *  @file LTDriverMipiDsi.h header for private driver interface object LTDriverMipiDsi
 */

/**
 * @defgroup ltdriver_mipidsi LTDriverMipiDsi
 * @ingroup ltdriver
 * @{
 *
 * @brief LT Driver Library for MIPI-DSI display host controllers.
 *
 * LTObjects of type LTDriverMipiDsi are intended for use by LTDeviceMipiDsi ONLY.
 * Do not access the DSI controller directly through the Driver interface.
 *
 * IMPLEMENTING DRIVER-LEVEL SUPPORT FOR LTDeviceMipiDsi
 * =====================================================
 *
 * A platform's DSI driver library implements a specialization of the LTDriverMipiDsi
 * LTObject.  LTDeviceMipiDsi is the only library that creates LTDriverMipiDsi objects;
 * it does so with lt_createdriverobject_fordevice() using the driver named for
 * "LTDeviceMipiDsi" in the platform's LTDeviceConfig.json.
 *
 * The division of labour between the Device and Driver levels is:
 *
 * - The Device level validates configuration, serializes packet transfers, chooses
 *   short versus long packet types from the payload size, accumulates error flags,
 *   and dispatches error notifications to clients.  A driver must not duplicate any
 *   of this.
 *
 * - The Driver level owns the DSI controller registers, the D-PHY, and the packet
 *   FIFOs.  It transmits exactly the packet it is handed and reports link errors
 *   as they occur.
 *
 * A driver must:
 * - reject a %Configure that it cannot honour rather than silently substituting
 *   a different lane count, pixel format, or mode
 * - derive D-PHY timing from the lane rate when a %LTDeviceMipiDsi_PhyTiming field
 *   is zero, and use the supplied override otherwise
 * - leave the lanes in the LP-11 stop state when %PowerOn returns true
 * - call the supplied LTDriverMipiDsi_ErrorProc from interrupt context as errors
 *   occur; the Device level queues the notification onto a thread
 */

#ifndef LT_INCLUDE_LT_DRIVER_MIPIDSI_LTDRIVERMIPIDSI_H
#define LT_INCLUDE_LT_DRIVER_MIPIDSI_LTDRIVERMIPIDSI_H

#include <lt/device/mipidsi/LTDeviceMipiDsi.h>

LT_EXTERN_C_BEGIN

/* _________________________
   LTDriverMipiDsi types  */

typedef void (LTDriverMipiDsi_ErrorProc)(LTDeviceMipiDsi_Error errors, void *pClientData) LT_ISR_SAFE;
    /**< called in ISR context whenever the driver detects one or more DSI link errors
     *
     * @param errors the bitmask of errors newly detected
     * @param pClientData the client data passed in to SetErrorProc
     */

typedef_LTENUM_SIZED(LTDriverMipiDsi_PacketType, u32) {
    kLTDriverMipiDsi_PacketType_DcsShort = 0,   /**< DCS short packet: a command byte and up to one parameter */
    kLTDriverMipiDsi_PacketType_DcsLong,        /**< DCS long packet: a command byte followed by a payload */
    kLTDriverMipiDsi_PacketType_GenericShort,   /**< generic short packet: up to kLTDeviceMipiDsi_MaxShortParams payload bytes */
    kLTDriverMipiDsi_PacketType_GenericLong,    /**< generic long packet: a variable length payload */
};
    /**< the DSI packet type to transmit
     *
     * @note the Device level selects the packet type from the payload size so that
     *       drivers do not each reimplement that rule.
     */

typedef struct LTDriverMipiDsi_Packet {
    LTDriverMipiDsi_PacketType  packetType;     /**< the packet type to transmit */
    u32                         nVirtualChannel;/**< the virtual channel id to address */
    const u8                   *pPayload;       /**< the packet payload, or NULL when there is none */
    u32                         nPayloadBytes;  /**< the size of the payload in bytes */
    bool                        bHighSpeed;     /**< whether to transmit in high speed mode rather than low power escape mode */
} LTDriverMipiDsi_Packet;
    /**< a DSI packet to transmit
     *
     * @note for the DCS packet types the command byte is the first payload byte.
     */

#ifndef DOXY_SKIP // [
/* _______________________
   LTDriverMipiDsi API  */
typedef_LTObject(LTDriverMipiDsi, 1) {

    bool (* Configure)(LTDriverMipiDsi *dsiDriver, const LTDeviceMipiDsi_Config *pConfig);
        /**< configures the DSI controller and D-PHY for the attached panel
         *
         * @param[in] dsiDriver the DSI driver to configure
         * @param[in] pConfig the link and panel configuration to apply, already validated by the Device level
         * @return true if the hardware can honour the configuration
         *
         * @note the Device level guarantees the lane count, virtual channel, mode, and
         *       pixel format are in range before calling; a driver rejects only what its
         *       hardware genuinely cannot do.
         *
         * @note the Device level will not call %Configure while the link is powered on.
         */

    bool (* PowerOn)(LTDriverMipiDsi *dsiDriver);
        /**< powers up the D-PHY and brings the configured lanes to the LP-11 stop state
         *
         * @param[in] dsiDriver the DSI driver to power on
         * @return true if the link reached the stop state
         */

    void (* PowerOff)(LTDriverMipiDsi *dsiDriver);
        /**< powers down the D-PHY
         *
         * @param[in] dsiDriver the DSI driver to power off
         *
         * @note the Device level stops the pixel stream before calling.
         */

    bool (* SetFrameBuffer)(LTDriverMipiDsi *dsiDriver, const void *pPixels, u32 nBytes);
        /**< supplies the framebuffer the controller scans out in the video modes
         *
         * @param[in] dsiDriver the DSI driver to set the framebuffer on
         * @param[in] pPixels the framebuffer, in the configured pixel format, or NULL to forget it
         * @param[in] nBytes the size of the framebuffer in bytes
         * @return true if the hardware can scan out that buffer
         *
         * @note the buffer is not copied, and the Device level does not call this while
         *       the pixel stream is running.
         *
         * @note the Device level does not call this in command mode.
         */

    bool (* StartVideo)(LTDriverMipiDsi *dsiDriver);
        /**< begins the high speed pixel stream to the panel
         *
         * @param[in] dsiDriver the DSI driver to start
         * @return true if the pixel stream started
         *
         * @note the Device level does not call this in command mode.
         */

    void (* StopVideo)(LTDriverMipiDsi *dsiDriver);
        /**< ends the pixel stream and returns the lanes to the stop state
         *
         * @param[in] dsiDriver the DSI driver to stop
         */

    bool (* WriteFrame)(LTDriverMipiDsi *dsiDriver, const void *pPixels, u32 nBytes);
        /**< sends a frame of pixel data to a command mode panel
         *
         * @param[in] dsiDriver the DSI driver to transmit on
         * @param[in] pPixels the pixel data, in the configured pixel format
         * @param[in] nBytes the size of the pixel data in bytes
         * @return true if the frame was transmitted
         *
         * @note the Device level does not call this in the video modes.
         */

    bool (* SendPacket)(LTDriverMipiDsi *dsiDriver, const LTDriverMipiDsi_Packet *pPacket);
        /**< transmits a DSI packet to the panel
         *
         * @param[in] dsiDriver the DSI driver to transmit on
         * @param[in] pPacket the packet to transmit
         * @return true if the packet was transmitted and acknowledged
         *
         * @note transfers are serialized by the Device level, so a driver need not
         *       lock against concurrent callers.
         */

    s32 (* ReceivePacket)(LTDriverMipiDsi *dsiDriver, const LTDriverMipiDsi_Packet *pRequest, u8 *pReadBuffer, u32 nReadBufferBytes);
        /**< performs a read transaction with the panel
         *
         * @param[in]  dsiDriver the DSI driver to transact on
         * @param[in]  pRequest the read request packet to transmit
         * @param[out] pReadBuffer receives the returned bytes
         * @param[in]  nReadBufferBytes the capacity of pReadBuffer in bytes
         * @return the number of bytes read, or -1 on error
         *
         * @note a read needs a bus turnaround, so the request is always transmitted in
         *       low power escape mode irrespective of the packet's bHighSpeed field.
         */

    bool (* SetUltraLowPowerState)(LTDriverMipiDsi *dsiDriver, bool bEnter);
        /**< enters or exits the ultra low power state on the clock and data lanes
         *
         * @param[in] dsiDriver the DSI driver to operate on
         * @param[in] bEnter true to enter ULPS, false to exit it back to the stop state
         * @return true if all configured lanes reached the requested state
         */

    LTDeviceMipiDsi_LaneState (* GetClockLaneState)(LTDriverMipiDsi *dsiDriver);
        /**< reports the current state of the clock lane
         *  @return the state of the clock lane */

    LTDeviceMipiDsi_LaneState (* GetDataLaneState)(LTDriverMipiDsi *dsiDriver, u32 nLane);
        /**< reports the current state of a data lane
         *
         * @param[in] dsiDriver the DSI driver to query
         * @param[in] nLane the data lane to query, 0..nDataLanes - 1
         * @return the state of the data lane, or kLTDeviceMipiDsi_LaneState_Off if nLane is not configured
         */

    void (* SetErrorProc)(LTDriverMipiDsi *dsiDriver, LTDriverMipiDsi_ErrorProc *pErrorProc, void *pErrorClientData);
        /**< supplies the procedure the driver calls when it detects a link error
         *
         * @param[in] dsiDriver the DSI driver to register with
         * @param[in] pErrorProc the proc to call from ISR context on error, or NULL to stop error reporting
         * @param[in] pErrorClientData the client data passed back to the error proc
         *
         * @note the Device level sets this once during construction.
         */

    void (* DumpRegisters)(LTDriverMipiDsi *dsiDriver, LTDeviceMipiDsi_RegisterProc *pRegisterProc, void *pClientData);
        /**< reads back the controller, D-PHY and clock registers behind the link
         *
         * @param[in] dsiDriver the DSI driver to dump
         * @param[in] pRegisterProc called once per register, in whatever order the driver chooses
         * @param[in] pClientData the client data passed back to pRegisterProc
         *
         * @note the register set is the driver's own; the Device level passes the
         *       callback straight through and does not interpret what comes back.
         *       The link need not be powered on.
         */

} LTOBJECT_API;
#endif  // DOXY_SKIP ]

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DRIVER_MIPIDSI_LTDRIVERMIPIDSI_H */

/** @} */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  30-Sep-26   dwoodward   created
 *  01-Oct-26   dwoodward   added DumpRegisters
 */
