################################################################################
# CommonDriverBleNimble.mk
#
# CommonDriverBleNimble.mk - BLE driver implementation
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR        := $(LT_ROOTS_BASE)/platforms/common/source/common/driver/blenimble
NIMBLE_VERSION_DIRNAME       := apache-mynewt-nimble-1.5.0
NIMBLE_ROOT_DIR              := ${LT_PROJECT_SOURCE_DIR}/${NIMBLE_VERSION_DIRNAME}

LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/porting/nimble/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/nimble/host/store/ram/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/nimble/host/store/config/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/nimble/host/services/gap/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/nimble/host/services/gatt/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/nimble/host/util/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/nimble/controller/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/nimble/host/services/ias/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/nimble/host/services/ans/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/nimble/host/services/lls/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/nimble/host/services/tps/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/ext/tinycrypt/src
LT_PROJECT_SOURCE_SUBDIRS    += ${NIMBLE_VERSION_DIRNAME}/nimble/transport/src
LT_PROJECT_SOURCE_SUBDIRS    += nimble_port_ltos/nimble/src

LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/porting/nimble/src/os_mbuf.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/porting/nimble/src/os_mempool.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/porting/nimble/src/os_msys_init.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/porting/nimble/src/nimble_port.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/porting/nimble/src/endian.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/porting/nimble/src/mem.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_cfg.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_id.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_adv.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_flow.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_startup.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_atomic.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_hci.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_hci_cmd.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_hci_evt.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_log.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_conn.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_stop.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_mqueue.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_mbuf.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_pvcy.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_misc.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_hs_hci_util.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_gap.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_gattc.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_gatts.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_att.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_att_svr.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_att_clt.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_att_cmd.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_l2cap.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_l2cap_sig.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_l2cap_sig_cmd.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_sm.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_sm_sc.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_sm_cmd.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_sm_alg.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_sm_lgcy.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_store.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_store_util.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/src/ble_uuid.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/services/gap/src/ble_svc_gap.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/services/gatt/src/ble_svc_gatt.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/store/ram/src/ble_store_ram.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/store/config/src/ble_store_config.c
ifeq ($(LT_BLE_PAIRING_PERSISTENT), 1)
    LT_PROJECT_SOURCE_FILES  += ${NIMBLE_VERSION_DIRNAME}/nimble/host/store/config/src/ble_store_config_conf_lt_settings.c
else
    LT_PROJECT_SOURCE_FILES  += ${NIMBLE_VERSION_DIRNAME}/nimble/host/store/config/src/ble_store_config_conf.c
endif
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/host/util/src/addr.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/ext/tinycrypt/src/ecc.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/ext/tinycrypt/src/ecc_dh.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/ext/tinycrypt/src/aes_encrypt.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/ext/tinycrypt/src/utils.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/ext/tinycrypt/src/cmac_mode.c
LT_PROJECT_SOURCE_FILES      += ${NIMBLE_VERSION_DIRNAME}/nimble/transport/src/transport.c

LT_PROJECT_SOURCE_FILES      += nimble_port_ltos/nimble/src/os_atomic.c
LT_PROJECT_SOURCE_FILES      += nimble_port_ltos/nimble/src/os_callout.c
LT_PROJECT_SOURCE_FILES      += nimble_port_ltos/nimble/src/os_eventq.c
LT_PROJECT_SOURCE_FILES      += nimble_port_ltos/nimble/src/os_mutex.c
LT_PROJECT_SOURCE_FILES      += nimble_port_ltos/nimble/src/os_sem.c
LT_PROJECT_SOURCE_FILES      += nimble_port_ltos/nimble/src/os_task.c
LT_PROJECT_SOURCE_FILES      += nimble_port_ltos/nimble/src/os_time.c
LT_PROJECT_SOURCE_FILES      += nimble_port_ltos/nimble/src/osif_lt.c

LT_PROJECT_SOURCE_FILES      += CommonDriverBleNimble.c

LT_PUBLIC_INCLUDE_FLAGS      += -I${LT_PROJECT_SOURCE_DIR}/nimble_port_ltos/nimble/inc

LT_PUBLIC_INCLUDE_FLAGS      += -I${NIMBLE_ROOT_DIR}/nimble/include
LT_PUBLIC_INCLUDE_FLAGS      += -I${NIMBLE_ROOT_DIR}/nimble/host/include
LT_PUBLIC_INCLUDE_FLAGS      += -I${NIMBLE_ROOT_DIR}/nimble/host/store/ram/include
LT_PUBLIC_INCLUDE_FLAGS      += -I${NIMBLE_ROOT_DIR}/nimble/host/store/config/include
LT_PUBLIC_INCLUDE_FLAGS      += -I${NIMBLE_ROOT_DIR}/nimble/host/services/gap/include
LT_PUBLIC_INCLUDE_FLAGS      += -I${NIMBLE_ROOT_DIR}/nimble/host/services/gatt/include
LT_PUBLIC_INCLUDE_FLAGS      += -I${NIMBLE_ROOT_DIR}/nimble/host/util/include
LT_PUBLIC_INCLUDE_FLAGS      += -I${NIMBLE_ROOT_DIR}/nimble/transport/include
LT_PUBLIC_INCLUDE_FLAGS      += -I${NIMBLE_ROOT_DIR}/porting/nimble/include
LT_PUBLIC_INCLUDE_FLAGS      += -I${NIMBLE_ROOT_DIR}/ext/tinycrypt/include

# Treatment for nimble code
LT_CFLAGS_LIBRARY += -DROKU_LTOS
LT_CFLAGS_GENERIC += -Wno-unused-parameter -Wno-sign-compare -Wno-old-style-declaration -Wno-pointer-arith -Wno-undef
LT_CFLAGS_GENERIC += $(SDK_DEFINES)


DEBUG_LOG := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/debug" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_GATT_MAX_PROCS := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/ble_gatt_max_procs" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_MSYS_1_BLOCK_COUNT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/ble_msys_block_count" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_ROLE_CENTRAL := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/central_role" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_ROLE_OBSERVER := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/observer_role" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_ROLE_PERIPHERAL := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/peripheral_role" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_ROLE_BROADCASTER := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/broadcaster_role" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_TRANSPORT_EVT_DISCARDABLE_COUNT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/transport_event_discardable_count" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_TRANSPORT_EVT_COUNT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/transport_event_count" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_SM_SC := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/sec_mgr_sec_conn" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/max_att_server_prep_write" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_LL_COUNT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/transport_acl_from_ll_count" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_TRANSPORT_EVT_SIZE := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/transport_evt_size" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_STORE_CONFIG_PERSIST := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/store_config_persist" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_STORE_MAX_BONDS := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/store_max_bonds" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
ROKU_NIMBLE_TCM_RAM := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/roku_nimble_tcm_ram" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
OS_CALLOUT_SUPPORT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/os_callout_support" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_HS_FLOW_CTRL := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/hs_flow_ctrl" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_HS_FLOW_CTRL_ITVL := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/hs_flow_ctrl_itvl" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_HS_FLOW_CTRL_THRESH := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/hs_flow_ctrl_thresh" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_HS_FLOW_CTRL_TX_ON_DISCONNECT := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/hs_flow_ctrl_tx_on_disconnect" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))
MYNEWT_VAL_BLE_L2CAP_JOIN_RX_FRAGS := $(shell $(LT_TARGET_DIR_BUILDTOOLS)/bin/peekjson "config/ble/l2cap_join_rx_frags" $(LT_DEVICE_CONFIG_ARBOLATED_JSON_FILE))


# Conditionally append to LT_CFLAGS_GENERIC if variables are non-empty
ifeq ($(strip $(DEBUG_LOG)),1)
LT_CFLAGS_GENERIC += -DDEBUG_LOG
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_GATT_MAX_PROCS)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_GATT_MAX_PROCS=$(MYNEWT_VAL_BLE_GATT_MAX_PROCS)
endif

ifneq ($(strip $(MYNEWT_VAL_MSYS_1_BLOCK_COUNT)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_MSYS_1_BLOCK_COUNT=$(MYNEWT_VAL_MSYS_1_BLOCK_COUNT)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_ROLE_CENTRAL)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_ROLE_CENTRAL=$(MYNEWT_VAL_BLE_ROLE_CENTRAL)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_ROLE_OBSERVER)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_ROLE_OBSERVER=$(MYNEWT_VAL_BLE_ROLE_OBSERVER)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_ROLE_PERIPHERAL)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_ROLE_PERIPHERAL=$(MYNEWT_VAL_BLE_ROLE_PERIPHERAL)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_ROLE_BROADCASTER)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_ROLE_BROADCASTER=$(MYNEWT_VAL_BLE_ROLE_BROADCASTER)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_TRANSPORT_EVT_DISCARDABLE_COUNT)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_TRANSPORT_EVT_DISCARDABLE_COUNT=$(MYNEWT_VAL_BLE_TRANSPORT_EVT_DISCARDABLE_COUNT)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_TRANSPORT_EVT_COUNT)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_TRANSPORT_EVT_COUNT=$(MYNEWT_VAL_BLE_TRANSPORT_EVT_COUNT)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_TRANSPORT_EVT_SIZE)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_TRANSPORT_EVT_SIZE=$(MYNEWT_VAL_BLE_TRANSPORT_EVT_SIZE)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_SM_SC)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_SM_SC=$(MYNEWT_VAL_BLE_SM_SC)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES=$(MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_LL_COUNT)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_LL_COUNT=$(MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_LL_COUNT)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_STORE_CONFIG_PERSIST)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_STORE_CONFIG_PERSIST
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_STORE_MAX_BONDS)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_STORE_MAX_BONDS=$(MYNEWT_VAL_BLE_STORE_MAX_BONDS)
endif

ifeq ($(strip $(OS_CALLOUT_SUPPORT)),1)
LT_CFLAGS_GENERIC += -DOS_CALLOUT_SUPPORT
endif

ifeq ($(strip $(ROKU_NIMBLE_TCM_RAM)),1)
LT_CFLAGS_GENERIC += -DROKU_NIMBLE_TCM_RAM
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_HS_FLOW_CTRL)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_HS_FLOW_CTRL=$(MYNEWT_VAL_BLE_HS_FLOW_CTRL)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_HS_FLOW_CTRL_ITVL)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_HS_FLOW_CTRL_ITVL=$(MYNEWT_VAL_BLE_HS_FLOW_CTRL_ITVL)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_HS_FLOW_CTRL_THRESH)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_HS_FLOW_CTRL_THRESH=$(MYNEWT_VAL_BLE_HS_FLOW_CTRL_THRESH)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_HS_FLOW_CTRL_TX_ON_DISCONNECT)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_HS_FLOW_CTRL_TX_ON_DISCONNECT=$(MYNEWT_VAL_BLE_HS_FLOW_CTRL_TX_ON_DISCONNECT)
endif

ifneq ($(strip $(MYNEWT_VAL_BLE_L2CAP_JOIN_RX_FRAGS)),)
LT_CFLAGS_GENERIC += -DMYNEWT_VAL_BLE_L2CAP_JOIN_RX_FRAGS=$(MYNEWT_VAL_BLE_L2CAP_JOIN_RX_FRAGS)
endif

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   09-Sep-22   vespasian   created
#   17-Jul-24   domitian    removed LT_EXEC_CMD from being called within the shell commands
