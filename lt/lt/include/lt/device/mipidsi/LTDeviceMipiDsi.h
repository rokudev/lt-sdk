/*******************************************************************************
 * <lt/device/mipidsi/LTDeviceMipiDsi.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 *  @file LTDeviceMipiDsi.h header for public interface object LTDeviceMipiDsi
 */

/**
 * @defgroup ltdevice_mipidsi LTDeviceMipiDsi
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for MIPI-DSI display host controllers.
 *
 * LTDeviceMipiDsi represents the DSI host controller that drives a display
 * panel over a MIPI D-PHY link.  It provides the two things a panel client
 * needs: link configuration/streaming control (PHY, lanes, video timing) and
 * a command channel for talking to the panel itself (DCS and generic packets).
 *
 * Panel-side hardware that is not part of the DSI link -- reset lines, backlight,
 * regulator enables -- is not represented here; use LTDeviceGpio, LTDevicePwm,
 * or LTDevicePowerSubswitch for those.
 *
 * Typical bring-up of a video-mode panel:
 * <pre>
 *     LTDeviceMipiDsi *dsi = lt_createobject(LTDeviceMipiDsi);
 *     dsi->API->Configure(dsi, &myPanelConfig);
 *     dsi->API->PowerOn(dsi);                            // PHY up, lanes in LP-11
 *     dsi->API->DcsWrite(dsi, 0x11, NULL, 0);            // exit sleep
 *     dsi->API->DcsWrite(dsi, 0x29, NULL, 0);            // display on
 *     dsi->API->SetFrameBuffer(dsi, myPixels, sizeof(myPixels));
 *     dsi->API->StartVideo(dsi);                         // begin HS pixel stream
 *     ...
 *     dsi->API->StopVideo(dsi);
 *     dsi->API->PowerOff(dsi);
 *     lt_destroyobject(dsi);
 * </pre>
 *
 * @internal
 *
 * The Device level is a thin platform-independent wrapper over LTDriverMipiDsi;
 * it owns client notification (via an LTThread dispatching error events) and
 * validates configuration.  The Driver level owns the DSI controller registers,
 * the D-PHY, and the packet FIFOs.
 *
 * LTDeviceMipiDsi is a single-unit device; it does not use the multi-unit
 * Device Unit facility.
 *
 * @endinternal
 */

#ifndef LT_INCLUDE_LT_DEVICE_MIPIDSI_LTDEVICEMIPIDSI_H
#define LT_INCLUDE_LT_DEVICE_MIPIDSI_LTDEVICEMIPIDSI_H

#include <lt/LTObject.h>
#include <lt/core/LTThread.h>

LT_EXTERN_C_BEGIN

/* ___________________________
   LTDeviceMipiDsi constants */

#define kLTDeviceMipiDsi_MaxLanes           4
    /**< maximum number of D-PHY data lanes addressable by this interface */

#define kLTDeviceMipiDsi_MaxVirtualChannel  3
    /**< maximum DSI virtual channel id (the DSI virtual channel field is 2 bits) */

#define kLTDeviceMipiDsi_MaxShortParams     2
    /**< maximum payload bytes carried by a DSI short packet */

/* _______________________
   LTDeviceMipiDsi types */

typedef_LTENUM_SIZED(LTDeviceMipiDsi_PixelFormat, u32) {
    kLTDeviceMipiDsi_PixelFormat_Unset = 0,     /**< no pixel format selected; the device is created in this state */
    kLTDeviceMipiDsi_PixelFormat_RGB565,        /**< 16 bits per pixel, packed */
    kLTDeviceMipiDsi_PixelFormat_RGB666Packed,  /**< 18 bits per pixel, packed */
    kLTDeviceMipiDsi_PixelFormat_RGB666Loose,   /**< 18 bits per pixel, loosely packed into 24 bits */
    kLTDeviceMipiDsi_PixelFormat_RGB888,        /**< 24 bits per pixel */
    kLTDeviceMipiDsi_PixelFormat_YUV422_8Bit,   /**< YCbCr 4:2:2, 8 bits per component */
};
    /**< pixel formats that may be carried over the DSI link */

typedef_LTENUM_SIZED(LTDeviceMipiDsi_Mode, u32) {
    kLTDeviceMipiDsi_Mode_Unset = 0,            /**< no mode selected; the device is created in this state */
    kLTDeviceMipiDsi_Mode_VideoSyncPulse,       /**< video mode, non-burst, sync pulses transmitted */
    kLTDeviceMipiDsi_Mode_VideoSyncEvent,       /**< video mode, non-burst, sync events transmitted */
    kLTDeviceMipiDsi_Mode_VideoBurst,           /**< video mode, burst; pixels sent faster than pixel rate, link returns to LP between lines */
    kLTDeviceMipiDsi_Mode_Command,              /**< command mode; the panel holds its own framebuffer and is updated by DCS writes */
};
    /**< DSI operating modes
     *
     * @note in command mode the pixel stream is delivered with %WriteFrame rather than by
     *       a display controller, and %StartVideo / %StopVideo have no effect.
     */

typedef_LTENUM_SIZED(LTDeviceMipiDsi_LaneState, u32) {
    kLTDeviceMipiDsi_LaneState_Off = 0,         /**< the lane is powered down */
    kLTDeviceMipiDsi_LaneState_Stop,            /**< the lane is in the LP-11 stop state */
    kLTDeviceMipiDsi_LaneState_LowPower,        /**< the lane is transferring in low power escape mode */
    kLTDeviceMipiDsi_LaneState_HighSpeed,       /**< the lane is transferring in high speed mode */
    kLTDeviceMipiDsi_LaneState_UltraLowPower,   /**< the lane is in the ultra low power state (ULPS) */
};
    /**< the state of a D-PHY lane */

typedef_LTENUM_SIZED(LTDeviceMipiDsi_Error, u32) {
    kLTDeviceMipiDsi_Error_None             = 0,          /**< no error */
    kLTDeviceMipiDsi_Error_TxFifoOverflow   = (1u << 0),  /**< the transmit FIFO was written while full */
    kLTDeviceMipiDsi_Error_TxFifoUnderflow  = (1u << 1),  /**< the transmit FIFO ran dry mid-packet */
    kLTDeviceMipiDsi_Error_RxFifoOverflow   = (1u << 2),  /**< the receive FIFO was not drained in time */
    kLTDeviceMipiDsi_Error_RxFifoUnderflow  = (1u << 3),  /**< the receive FIFO was read while empty */
    kLTDeviceMipiDsi_Error_PixelCountShort  = (1u << 4),  /**< a line was transmitted with fewer pixels than the configured width */
    kLTDeviceMipiDsi_Error_PixelCountLong   = (1u << 5),  /**< a line was transmitted with more pixels than the configured width */
    kLTDeviceMipiDsi_Error_EccUncorrectable = (1u << 6),  /**< an uncorrectable ECC error was seen in a received packet header */
    kLTDeviceMipiDsi_Error_Checksum         = (1u << 7),  /**< a received long packet failed its checksum */
    kLTDeviceMipiDsi_Error_Timeout          = (1u << 8),  /**< a transfer or lane state change did not complete in time */
    kLTDeviceMipiDsi_Error_PanelReported    = (1u << 9),  /**< the panel returned a DSI acknowledge-and-error report packet */
};
    /**< DSI link error flags, combined as a bitmask
     *
     * @see LTDeviceMipiDsi_ErrorEventProc
     */

typedef struct LTDeviceMipiDsi_Timing {
    u32 nHorizontalActive;      /**< active pixels per line */
    u32 nHorizontalSyncWidth;   /**< horizontal sync width in pixel clocks */
    u32 nHorizontalBackPorch;   /**< horizontal back porch in pixel clocks */
    u32 nHorizontalFrontPorch;  /**< horizontal front porch in pixel clocks */
    u32 nVerticalActive;        /**< active lines per frame */
    u32 nVerticalSyncWidth;     /**< vertical sync width in lines */
    u32 nVerticalBackPorch;     /**< vertical back porch in lines */
    u32 nVerticalFrontPorch;    /**< vertical front porch in lines */
    u32 nFrameRateHz;           /**< target refresh rate in frames per second */
} LTDeviceMipiDsi_Timing;
    /**< display timing of the attached panel
     *
     * @note in command mode only %nHorizontalActive and %nVerticalActive are meaningful.
     */

typedef struct LTDeviceMipiDsi_PhyTiming {
    u32 nClockZeroNs;       /**< clock lane HS-ZERO duration in nanoseconds */
    u32 nClockTrailNs;      /**< clock lane HS-TRAIL duration in nanoseconds */
    u32 nClockExitNs;       /**< clock lane HS-EXIT duration in nanoseconds */
    u32 nClockPrepareNs;    /**< clock lane HS-PREPARE duration in nanoseconds */
    u32 nDataZeroNs;        /**< data lane HS-ZERO duration in nanoseconds */
    u32 nDataTrailNs;       /**< data lane HS-TRAIL duration in nanoseconds */
    u32 nDataExitNs;        /**< data lane HS-EXIT duration in nanoseconds */
    u32 nDataPrepareNs;     /**< data lane HS-PREPARE duration in nanoseconds */
    u32 nLpxNs;             /**< low power transmit period (LPX) in nanoseconds */
    u32 nTurnaroundGoNs;    /**< bus turnaround TA-GO duration in nanoseconds */
    u32 nTurnaroundGetNs;   /**< bus turnaround TA-GET duration in nanoseconds */
    u32 nWakeupUs;          /**< ULPS wakeup duration in microseconds */
} LTDeviceMipiDsi_PhyTiming;
    /**< D-PHY timing overrides
     *
     * @note a zero field means "use the value the driver derives from the lane rate".
     *       Supply overrides only for panels whose datasheet demands values outside
     *       the D-PHY specification defaults.
     */

typedef struct LTDeviceMipiDsi_Config {
    LTDeviceMipiDsi_Mode         mode;              /**< DSI operating mode */
    LTDeviceMipiDsi_PixelFormat  pixelFormat;       /**< pixel format carried over the link */
    u32                          nDataLanes;        /**< number of data lanes to use, 1..kLTDeviceMipiDsi_MaxLanes */
    u32                          nVirtualChannel;   /**< virtual channel id of the panel, 0..kLTDeviceMipiDsi_MaxVirtualChannel */
    u32                          nLaneRateMbps;     /**< high speed bit rate per data lane in megabits per second, or 0 to derive it from the timing */
    bool                         bContinuousClock;  /**< whether the clock lane stays in high speed between transmissions */
    bool                         bEnableEotPacket;  /**< whether an end-of-transmission packet is appended to high speed bursts */
    LTDeviceMipiDsi_Timing       timing;            /**< display timing of the attached panel */
    LTDeviceMipiDsi_PhyTiming    phyTiming;         /**< D-PHY timing overrides; zero-filled to accept driver defaults */
} LTDeviceMipiDsi_Config;
    /**< the configuration of the DSI link and the panel attached to it
     *
     * @note zero-initialize this structure before filling it in so that fields added
     *       to it in later interface versions take their default behaviour.
     */

typedef void (LTDeviceMipiDsi_ErrorEventProc)(LTDeviceMipiDsi_Error errors, void *pClientData);
    /**< EventProc type for receiving DSI link error notifications
     *
     * @param errors the bitmask of errors accumulated since the previous notification
     * @param pClientData the client data that was passed in during registration
     *
     * @note this proc is called on a Device level thread, not from interrupt context,
     *       and so may call back into LTDeviceMipiDsi.
     */

typedef void (LTDeviceMipiDsi_RegisterProc)(const char *pName, u32 nAddress, u32 nValue, void *pClientData);
    /**< callback type receiving one hardware register per call from %DumpRegisters
     *
     * @param pName the register name, as the platform documentation spells it
     * @param nAddress the address the value was read from
     * @param nValue the value read
     * @param pClientData the client data passed in to %DumpRegisters
     */

/* _____________________
   LTDeviceMipiDsi API */
typedef_LTObject(LTDeviceMipiDsi, 1) {
    /**< LTDeviceMipiDsi object API.
     *
     *   @note DSI packet transfers are serialized by the Device level, so a panel
     *         driver may issue commands from any thread.
     */

    bool (* Configure)(LTDeviceMipiDsi *dsi, const LTDeviceMipiDsi_Config *pConfig);
        /**< configures the DSI link for the attached panel
         *
         * @param[in] dsi the DSI device to configure
         * @param[in] pConfig the link and panel configuration to apply
         * @return true if the configuration was accepted
         *
         * @note %Configure must be called before %PowerOn.  Calling it while the link is
         *       powered on fails; call %PowerOff first.
         */

    bool (* GetConfig)(LTDeviceMipiDsi *dsi, LTDeviceMipiDsi_Config *pConfigOut);
        /**< retrieves the configuration currently in effect
         *
         * @param[in]  dsi the DSI device to query
         * @param[out] pConfigOut receives the configuration in effect
         * @return true if a configuration has been applied, false if the device is unconfigured
         */

    bool (* PowerOn)(LTDeviceMipiDsi *dsi);
        /**< powers up the D-PHY and brings the configured lanes to the LP-11 stop state
         *
         * @param[in] dsi the DSI device to power on
         * @return true if the link reached the stop state
         *
         * @note on return the panel may be addressed with %DcsWrite, %DcsRead, %GenericWrite,
         *       and %GenericRead.  Pixel traffic does not begin until %StartVideo is called.
         *
         * @see PowerOff, StartVideo
         */

    void (* PowerOff)(LTDeviceMipiDsi *dsi);
        /**< stops any pixel stream and powers down the D-PHY
         *
         * @param[in] dsi the DSI device to power off
         *
         * @note the panel should be placed in its own off state with %DcsWrite before
         *       calling %PowerOff; this function does not send any panel commands.
         *
         * @see PowerOn
         */

    bool (* IsPoweredOn)(LTDeviceMipiDsi *dsi);
        /**< reports whether the D-PHY is powered up
         *  @return whether or not the link is powered on */

    bool (* SetFrameBuffer)(LTDeviceMipiDsi *dsi, const void *pPixels, u32 nBytes);
        /**< supplies the framebuffer the display controller scans out in the video modes
         *
         * @param[in] dsi the DSI device to set the framebuffer on
         * @param[in] pPixels the framebuffer, in the configured pixel format, or NULL to forget it
         * @param[in] nBytes the size of the framebuffer in bytes
         * @return true if the framebuffer was accepted
         *
         * @note the device does not copy the framebuffer; the caller owns it and must keep
         *       it alive until %StopVideo returns or another framebuffer replaces it.
         *
         * @note a video mode controller scans the framebuffer out repeatedly, so whatever
         *       is written into it appears on the panel without another call here.  Some
         *       controllers read it by DMA and will not see writes still sitting in a
         *       cache, which is the caller's to flush.
         *
         * @note this function is meaningful only in the video modes; in
         *       kLTDeviceMipiDsi_Mode_Command it fails and has no effect.
         *
         * @see StartVideo, WriteFrame
         */

    bool (* StartVideo)(LTDeviceMipiDsi *dsi);
        /**< begins the high speed pixel stream to the panel
         *
         * @param[in] dsi the DSI device to start
         * @return true if the pixel stream started
         *
         * @note a framebuffer must have been supplied with %SetFrameBuffer first.
         *
         * @note this function is meaningful only in the video modes; in
         *       kLTDeviceMipiDsi_Mode_Command it fails and has no effect.
         *
         * @see StopVideo, WriteFrame
         */

    void (* StopVideo)(LTDeviceMipiDsi *dsi);
        /**< ends the high speed pixel stream and returns the lanes to the stop state
         *
         * @param[in] dsi the DSI device to stop
         *
         * @see StartVideo
         */

    bool (* IsVideoActive)(LTDeviceMipiDsi *dsi);
        /**< reports whether a pixel stream is currently being transmitted
         *  @return whether or not the pixel stream is active */

    bool (* WriteFrame)(LTDeviceMipiDsi *dsi, const void *pPixels, u32 nBytes);
        /**< sends a frame of pixel data to a command mode panel
         *
         * @param[in] dsi the DSI device to transmit on
         * @param[in] pPixels the pixel data, in the configured pixel format
         * @param[in] nBytes the size of the pixel data in bytes
         * @return true if the frame was transmitted
         *
         * @note the caller is responsible for having set the panel's column and page
         *       address (DCS 0x2a / 0x2b) and for issuing the memory write command that
         *       this data belongs to.
         *
         * @note this function is meaningful only in kLTDeviceMipiDsi_Mode_Command.
         */

    bool (* DcsWrite)(LTDeviceMipiDsi *dsi, u8 dcsCommand, const u8 *pParams, u32 nParams);
        /**< sends a DCS command to the panel
         *
         * @param[in] dsi the DSI device to transmit on
         * @param[in] dcsCommand the DCS command byte
         * @param[in] pParams the command parameters, or NULL when there are none
         * @param[in] nParams the number of command parameters
         * @return true if the command was transmitted and acknowledged
         *
         * @note the packet type is chosen from nParams: a DCS short packet carries the
         *       command byte plus at most one parameter, so anything longer goes out long.
         */

    s32 (* DcsRead)(LTDeviceMipiDsi *dsi, u8 dcsCommand, u8 *pReadBuffer, u32 nReadBufferBytes);
        /**< reads a DCS parameter from the panel
         *
         * @param[in]  dsi the DSI device to transact on
         * @param[in]  dcsCommand the DCS command byte to read
         * @param[out] pReadBuffer receives the returned bytes
         * @param[in]  nReadBufferBytes the capacity of pReadBuffer in bytes
         * @return the number of bytes read, or -1 on error
         *
         * @note reads require a bus turnaround and so are always performed in low
         *       power escape mode, regardless of %SetCommandsUseHighSpeed.
         */

    bool (* GenericWrite)(LTDeviceMipiDsi *dsi, const u8 *pPayload, u32 nPayloadBytes);
        /**< sends a generic (non-DCS) packet to the panel
         *
         * @param[in] dsi the DSI device to transmit on
         * @param[in] pPayload the packet payload
         * @param[in] nPayloadBytes the size of the payload in bytes
         * @return true if the packet was transmitted and acknowledged
         *
         * @note as with %DcsWrite the short or long generic packet type is chosen from
         *       the payload size.
         */

    s32 (* GenericRead)(LTDeviceMipiDsi *dsi, const u8 *pPayload, u32 nPayloadBytes, u8 *pReadBuffer, u32 nReadBufferBytes);
        /**< performs a generic (non-DCS) read from the panel
         *
         * @param[in]  dsi the DSI device to transact on
         * @param[in]  pPayload the read request payload, of up to kLTDeviceMipiDsi_MaxShortParams bytes
         * @param[in]  nPayloadBytes the size of the read request payload in bytes
         * @param[out] pReadBuffer receives the returned bytes
         * @param[in]  nReadBufferBytes the capacity of pReadBuffer in bytes
         * @return the number of bytes read, or -1 on error
         */

    void (* SetCommandsUseHighSpeed)(LTDeviceMipiDsi *dsi, bool bHighSpeed);
        /**< selects whether panel commands are sent in high speed or low power escape mode
         *
         * @param[in] dsi the DSI device to configure
         * @param[in] bHighSpeed true to send commands in high speed mode, false to send them in low power escape mode
         *
         * @note commands are sent in low power escape mode by default, which is what most
         *       panels require during their initialization sequence.  High speed commands
         *       are only usable once the link is running.
         */

    bool (* GetCommandsUseHighSpeed)(LTDeviceMipiDsi *dsi);
        /**< reports the transmission mode currently used for panel commands
         *  @return true if commands are sent in high speed mode */

    bool (* EnterUltraLowPowerState)(LTDeviceMipiDsi *dsi);
        /**< places the link lanes in the ultra low power state
         *
         * @param[in] dsi the DSI device to place in ULPS
         * @return true if all configured lanes entered ULPS
         *
         * @note the pixel stream must be stopped before entering ULPS.
         *
         * @see ExitUltraLowPowerState
         */

    bool (* ExitUltraLowPowerState)(LTDeviceMipiDsi *dsi);
        /**< brings the link lanes out of the ultra low power state back to the stop state
         *
         * @param[in] dsi the DSI device to bring out of ULPS
         * @return true if all configured lanes returned to the stop state
         *
         * @see EnterUltraLowPowerState
         */

    LTDeviceMipiDsi_LaneState (* GetClockLaneState)(LTDeviceMipiDsi *dsi);
        /**< reports the current state of the clock lane
         *  @return the state of the clock lane */

    LTDeviceMipiDsi_LaneState (* GetDataLaneState)(LTDeviceMipiDsi *dsi, u32 nLane);
        /**< reports the current state of a data lane
         *
         * @param[in] dsi the DSI device to query
         * @param[in] nLane the data lane to query, 0..nDataLanes - 1
         * @return the state of the data lane, or kLTDeviceMipiDsi_LaneState_Off if nLane is not configured
         */

    void (* OnErrorEvent)(LTDeviceMipiDsi *dsi, LTDeviceMipiDsi_ErrorEventProc *pErrorEventProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData);
        /**< registers an event procedure for receipt of DSI link errors
         *
         * @param[in] dsi the DSI device to register with
         * @param[in] pErrorEventProc the event proc to be called when link errors occur
         * @param[in] pClientDataReleaseProc the function called when the client data is released (when %NoErrorEvent is called or the device object is destroyed with registered handlers), or NULL
         * @param[in] pClientData the client data passed back to the event callback
         *
         * @see NoErrorEvent
         */

    void (* NoErrorEvent)(LTDeviceMipiDsi *dsi, LTDeviceMipiDsi_ErrorEventProc *pErrorEventProc);
        /**< unregisters a previously registered error event proc
         *
         * @param[in] dsi the DSI device to unregister from
         * @param[in] pErrorEventProc the event proc to be unregistered
         */

    LTDeviceMipiDsi_Error (* GetErrors)(LTDeviceMipiDsi *dsi, bool bClear);
        /**< retrieves the link errors accumulated since the last call
         *
         * @param[in] dsi the DSI device to query
         * @param[in] bClear whether to clear the accumulated errors
         * @return the bitmask of accumulated errors
         *
         * @note provided for clients that poll rather than register with %OnErrorEvent,
         *       and for MFG link testing.
         */

    const char * (* ErrorToString)(LTDeviceMipiDsi_Error error);
        /**< returns the string name of a single LTDeviceMipiDsi_Error flag
         *
         * @param[in] error a single error flag, not a bitmask of several
         * @return the human-readable name of the flag
         *
         * @note provided for logging and debugging support.  The returned strings are
         *       substrings of the enum value labels.
         */

    void (* DumpRegisters)(LTDeviceMipiDsi *dsi, LTDeviceMipiDsi_RegisterProc *pRegisterProc, void *pClientData);
        /**< reads back the controller, D-PHY and clock registers behind the link
         *
         * @param[in] dsi the DSI device to dump
         * @param[in] pRegisterProc called once per register, in the order the driver chooses
         * @param[in] pClientData the client data passed back to pRegisterProc
         *
         * @note which registers appear, and what they are called, is the platform
         *       driver's business; a caller formats whatever it is handed.  Provided
         *       for bring-up and MFG diagnosis only.
         */

} LTOBJECT_API;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_MIPIDSI_LTDEVICEMIPIDSI_H */

/** @} */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  30-Sep-26   dwoodward   created
 *  01-Oct-26   dwoodward   added DumpRegisters
 */
