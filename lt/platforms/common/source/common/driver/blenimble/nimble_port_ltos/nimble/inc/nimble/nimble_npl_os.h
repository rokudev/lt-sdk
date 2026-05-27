/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef _NIMBLE_NPL_OS_H_
#define _NIMBLE_NPL_OS_H_

#include "os_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_NPL_OS_ALIGNMENT    4

#define BLE_NPL_TIME_FOREVER    UINT32_MAX


// NIMBLEDO: declared here in order not to edit nimble_port.c from apache
extern void ble_transport_init(void);

/* NPL cleanup functions for flow control and other features */
struct ble_npl_event;
struct ble_npl_callout;
void ble_npl_event_deinit(struct ble_npl_event *ev);
void ble_npl_callout_deinit(struct ble_npl_callout *c);

#ifdef __cplusplus
}
#endif

#endif  /* _NPL_H_ */
