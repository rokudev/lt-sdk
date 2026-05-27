/******************************************************************************
 * source/lt/net/socketproxy/LTSocketProxy.h - LTSocket proxy
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_SOCKETPROXY_LTSOCKETPROXY_H
#define ROKU_LT_SOURCE_LT_NET_SOCKETPROXY_LTSOCKETPROXY_H

typedef enum {
    kSocketOp_RegisterForEvents,
    kSocketOp_UnregisterFromEvents,
    kLTSocketOp_Event,
    kLTSocketOp_Write,
    kLTSocketOp_WriteStatus,
} LTSocketOp;

#endif /* #ifndef ROKU_LT_SOURCE_LT_NET_SOCKETPROXY_LTSOCKETPROXY_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  05-Nov-24   trajan      created
 */
