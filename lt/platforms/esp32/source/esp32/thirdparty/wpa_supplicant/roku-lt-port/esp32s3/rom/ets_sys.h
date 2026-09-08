/******************************************************************************
 * ets_sys.h                                       ESP32-S3 WPA supplicant port
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The status, timer and event slices of IDF's esp32s3/rom/ets_sys.h,
 * transcribed verbatim from
 * source/esp32/ltbootloader/include/esp32s3/rom/ets_sys.h.  That header is not
 * usable here: it pulls in soc/soc.h, and the soc/ and hal/ trees beside it
 * shadow the wireless headers this component is built against.  Only these three
 * groups are needed - src/ap/wpa_auth_i.h, src/rsn_supp/wpa_i.h and
 * esp_supplicant/src/esp_wps.c embed ETSTimer and arm it, esp_wpa2.c passes
 * ETSEvent through its work queue, and esp_wps.c takes a STATUS from the
 * scan-done callback.
 *
 * IDF reaches these on the esp32s3 through esp_rom's global include directory,
 * which LT does not have, so the four #if CONFIG_IDF_TARGET_* chains in the
 * component that stop at the esp32s2 gained an esp32s3 branch pointing here.
 *
 * Only ets_delay_us() is in this part's ROM.  The esp32 gets the whole ets_timer
 * group from ROM through mastering/ld/esp32/rom/esp32.rom.redefined.ld; the
 * esp32s3 ROM does not export it and there is no equivalent script, so the seven
 * entries below are defined in Esp32s3_LTOSAdapter.c on top of the OS adapter's
 * LT timer wrappers - IDF does the same job in esp_timer's ets_timer_legacy.c on
 * every target but the esp32.
 */

#ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_ESP32S3_ROM_ETS_SYS_H
#define PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_ESP32S3_ROM_ETS_SYS_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    OK = 0,
    FAIL,
    PENDING,
    BUSY,
    CANCEL,
} STATUS;

typedef uint32_t ETSSignal;
typedef uint32_t ETSParam;

typedef struct ETSEventTag ETSEvent;    /**< Event transmit/receive in ets*/

struct ETSEventTag {
    ETSSignal sig;  /**< Event signal, in same task, different Event with different signal*/
    ETSParam  par;  /**< Event parameter, sometimes without usage, then will be set as 0*/
};

typedef void ETSTimerFunc(void *timer_arg);/**< timer handler*/

typedef struct _ETSTIMER_ {
    struct _ETSTIMER_    *timer_next;   /**< timer linker*/
    uint32_t              timer_expire; /**< abstruct time when timer expire*/
    uint32_t              timer_period; /**< timer period, 0 means timer is not periodic repeated*/
    ETSTimerFunc         *timer_func;   /**< timer handler*/
    void                 *timer_arg;    /**< timer handler argument*/
} ETSTimer;

void ets_timer_init(void);
void ets_timer_deinit(void);
void ets_timer_arm(ETSTimer *timer, uint32_t tmout, bool repeat);
void ets_timer_arm_us(ETSTimer *ptimer, uint32_t us, bool repeat);
void ets_timer_disarm(ETSTimer *timer);
void ets_timer_setfn(ETSTimer *ptimer, ETSTimerFunc *pfunction, void *parg);
void ets_timer_done(ETSTimer *ptimer);
void ets_delay_us(uint32_t us);

#endif // #ifndef PLATFORMS_ESP32_WPA_SUPPLICANT_PORT_ESP32S3_ROM_ETS_SYS_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  28-Aug-26   claudius    created
 */
