/*******************************************************************************
 * lt/device/flpir/LTDeviceFlPIR.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 *  @file LTDeviceFlPIR.h header for public interface class
 *  LTDeviceFlPIR */

#ifndef LT_INCLUDE_LT_DEVICE_LTDEVICEFLPIR_H
#define LT_INCLUDE_LT_DEVICE_LTDEVICEFLPIR_H

#include <lt/LT.h>
#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

#define PIRZONEBITGRID_WIDTH  3
#define PIRZONEBITGRID_HEIGHT 1

typedef enum {
    kLTDriverFlPIREvent_Idle = 0x0101,
    kLTDriverFlPIREvent_MotionDetected
} LTDriverFlPIREvent;

typedef enum {
    kLTDeviceFlPIRZone_Left   = 0x80,
    kLTDeviceFlPIRZone_Middle = 0x40,
    kLTDeviceFlPIRZone_Right  = 0x20
} LTDeviceFlPIRZone;

typedef enum {
    kLTDeviceFlPIRAlgo_Off = 0,
    kLTDeviceFlPIRAlgo_HualaiActivate
} LTDeviceFlPIRAlgo;

typedef struct LTMediaMotionDetection_pirZoneBitGrid {
    u16 width;
    u16 height;
    LT_SIZE bitmapSize;
    u8 bitmap[];
} LTMediaMotionDetection_pirZoneBitGrid;

typedef void (LTDeviceFlPIR_OnMotionStateChangeEventProc)(LTDriverFlPIREvent event, void *eventData, void *pClientData);
/**< callback for motion detect events.
 *
 *   This callback is called to provide the client with motoion detected events.
 *
 *   @param event the motion event which has occurred
 *   @param eventData (optional) data associated with the event. The data type is dependent on event type as specified
 *                    by the %event% parameter.
 *   @param pClientData the client data that was specified when the callback was registered
 */

/*******************************************************************************
 * Floodlight PIR device interface (LTDevicePIR)
 *******************************************************************************/
typedef_LTObject(LTDeviceFlPIR, 1) {
    /**< Interface for handling */
    bool (*SetPIRSensitivity)(LTDeviceFlPIR *flPir, u8 sensitivityVal);
    /**< @brief sets the sensitivity value of floodlight PIR sensor
     *          Sets sensitivity to sensitivityVal, which must be in the range [0 .. 100 %],
     *          where 0 is the lowest sensitivity and 100 is the highest.
     * @param[in] flPir a pointer to the LTDeviceFlPIR object
     * @param[in] sensitivityVal pointer to the Sensitivity value to set for floodlight PIR sensor
     * @retval true if successfully set sensitivity value and false otherwise
     */

    bool (*GetPIRSensitivity)(LTDeviceFlPIR *flPir, u8 *sensitivityVal);
    /**< @brief get the sensitivity value of floodlight PIR sensor
     *
     * @param[in]  flPir a pointer to the LTDeviceFlPIR object
     * @param[out] sensitivityVal pointer to the Sensitivity value to get from floodlight PIR sensor
     * @retval true if successfully get sensitivity value and false otherwise
     */

    bool (*SetPIRZone)(LTDeviceFlPIR *flPir, LTDeviceFlPIRZone pirZone);
    /**< @brief sets the PIR zone of floodlight PIR sensor
     *          sets PIR select zone to pirZone, pirZone can be: 
     *          kLTDeviceFlPIRZone_Left                              0x80 only left zone,
     *          kLTDeviceFlPIRZone_Middle                            0x40 only middle zone,
     *          kLTDeviceFlPIRZone_Right                             0x20 only right zone
     * @param[in] flPir   a pointer to the LTDeviceFlPIR object
     * @param[in] pirZone is zone value to set for floodlight PIR sensor
     * @retval true if successfully set PIR zone value and false otherwise
     */

    bool (*GetPIRZone)(LTDeviceFlPIR *flPir, LTDeviceFlPIRZone *pirZone);
    /**< @brief gets current set PIR zone of floodlight PIR sensor
     *
     * @param[in]  flPir a pointer to the LTDeviceFlPIR object
     * @param[out] pirZone is pointer to the zone value to get from floodlight PIR sensor
     * @retval true if successfully get PIR zone value and false otherwise
     */

    bool (*SetPIRAlgo)(LTDeviceFlPIR *flPir, LTDeviceFlPIRAlgo pirAlgo);
    /**< @brief sets the algorithm of floodlight PIR sensor
     *          PIR filter algorithm switch i.e Whether PIR trigger requires logical
     *          judgment, in order to filter out some false alarms of PIR, such as wind blowing,
     *          rain, snow, and swaying leaves.
     *          kLTDeviceFlPIRAlgo_Off: If PIR is greater than a certain constant value, it is
     *          considered trigger
     *          kLTDeviceFlPIRAlgo_HualaiActivate: It will determine the slope and weighted value
     *          of the PIR value. It can only be triggered when certain conditions are met
     * @param[in] flPir a pointer to the LTDeviceFlPIR object
     * @param[in] pirAlgo is algorithm value to set for floodlight PIR sensor
     * @retval true if successfully get PIR algorithm value and false otherwise
     */

    bool (*GetPIRAlgo)(LTDeviceFlPIR *flPir, LTDeviceFlPIRAlgo *pirAlgo);
    /**< @brief gets the algorithm of floodlight PIR sensor
     *
     * @param[in]  flPir a pointer to the LTDeviceFlPIR object
     * @param[out] pirAlgo is pointer to the LTDeviceFlPIRAlgo types
     * @retval true if successfully get PIR algorithm value and false otherwise
     */

    void (*OnMotionEvent)(LTDeviceFlPIR *flPir, LTDeviceFlPIR_OnMotionStateChangeEventProc *proc, void *pClientData);
    /**< @brief registers client for the motion detect event
     *
     * @param[in] flPir a pointer to the LTDeviceFlPIR object
     * @param[in] proc pointer to the callback
     * @param[in] pClientData pointer to the client data for this event
     */

    bool (*NoMotionEvent)(LTDeviceFlPIR *flPir, LTDeviceFlPIR_OnMotionStateChangeEventProc *proc);
    /**< @brief unregisters client from the motion detect event
     *
     * @param[in] flPir a pointer to the LTDeviceFlPIR object
     * @param[in] proc pointer to the callback
     * @retval true if successfully unregistered from the motion detect event
     */

    bool (*MotionDelay)(LTDeviceFlPIR *flPir, u16 motionDelay);
    /**< @brief sets delay for motion event generation
     *          sets delay to motionDelay which must range from 1000 to 3000 milli seconds
     * @param[in] flPir a pointer to the LTDeviceFlPIR object
     * @param[in] motionDelay delay to set to generate motion detect event
     * @retval true if successfully set delay for motion event generation
     */
} LTOBJECT_API;

typedef void (LTDriverFlPIREventProc)(LTDriverFlPIREvent event, void *eventData, void *clientData);
/**<
 * @brief callback for PIR motion detect events
 *
 * @param event triggered event type
 * @param eventData data from driver specific to the event
 * @param clientData Client data for this event
 */

/*******************************************************************************
 * Floodlight PIR driver interface (ILTDriverFlPIR)
 *******************************************************************************/
typedef_LTLIBRARY_INTERFACE(ILTDriverFlPIR, 1) {
    /**< Interface for accessing the Floodlight PIR driver */
    bool (*SetPIRSettings)(u8 *data);
    /**<
     * @brief sets setting parameters for the floodlight PIR sensor
     * @param[in] data is a pointer to the buffer to set PIR setting parameters
     * @retval true if successfully set setting parameters and false otherwise
     */

    bool (*GetPIRSettings)(u8 *data);
    /**<
     * @brief gets setting parameters of the floodlight PIR sensor
     * @param[out] data is a pointer to the buffer to get PIR setting parameters
     * @retval true if successfully set setting parameters and false otherwise
     */

    void (*OnPIREvent)(LTDriverFlPIREventProc *proc, void *clientData);
    /**<
     * @brief registers client for the motion detect event.
     * @param[in] proc the callback function
     * @param[in] clientData the client data to pass to the callback
     */

    bool (*NoPIREvent)(LTDriverFlPIREventProc *proc);
    /**<
     * @brief unregisters client for the motion detect event.
     * @param[in] proc the callback function
     * @retval true if successfully unregistered from the PIR motion detect event
     */

    void (*EventTriggerDelay)(u16 delayMillisec);
    /**<
     * @brief sets delay for motion event trigger
     * @param[in] delayMillisec delay to set for motion detect event trigger
     */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_LTDEVICEFLPIR_H */
