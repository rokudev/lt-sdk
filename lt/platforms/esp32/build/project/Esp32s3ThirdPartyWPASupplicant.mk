################################################################################
# Esp32s3ThirdPartyWPASupplicant.mk
#
# Esp32s3ThirdPartyWPASupplicant.mk - project Esp32s3ThirdPartyWPASupplicant
#
# The WPA supplicant the esp32s3 Wi-Fi blobs call into.  Espressif ships every
# other piece of the radio stack as a binary but ships this one as source, so
# source/esp32/thirdparty/wpa_supplicant carries ESP-IDF v4.4.8's
# components/wpa_supplicant verbatim (minus its host test harness) and this file
# builds it.  v4.4.8 is the revision the esp32s3 blobs under
# source/esp32/mastering/lib/esp32s3 were cut from, and libnet80211 range checks
# the supplicant it links against, so the two have to stay in step.
#
# The esp32 does not use this project.  It links a prebuilt
# mastering/lib/esp32/libwpa_supplicant.a instead, and that archive is a
# different build of a different IDF vintage - see the note on crypto below.
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026 Roku Inc. All rights reserved.
################################################################################

# targets
LT_PROJECT_BUILD_SHARED_LIB := no
LT_PROJECT_BUILD_STATIC_LIB := yes
LT_PROJECT_BUILD_EXECUTABLE := no

WIFI_DRV_DIR                := esp-wireless-drivers-3rdparty

LT_PROJECT_SOURCE_DIR       := $(LT_PROJECT_SOURCE_DIR_BASE)/esp32/thirdparty/wpa_supplicant

LT_PROJECT_SOURCE_SUBDIRS   += port src/ap src/common src/crypto src/eap_peer \
                               src/rsn_supp src/tls src/utils src/wps esp_supplicant/src \
                               roku-lt-port

################################################################################
# Sources.  The lists below are IDF's, transcribed from the CMakeLists.txt that
# sits beside the sources, resolved for this configuration:
#
#   CONFIG_WPA_MBEDTLS_CRYPTO   0   internal crypto and internal TLS
#   CONFIG_WPA_11KV_SUPPORT     off no roaming/BSS transition
#   CONFIG_WPA_MBO_SUPPORT      off
#   CONFIG_WPA_DPP_SUPPORT      off
#
# The first of those is the one that matters and is not a free choice.  IDF's
# other branch routes the supplicant's crypto through mbedTLS, which LT does not
# carry - the prebuilt esp32 archive works that way only because it was built
# with a whole mbedTLS folded into it under CONFIG_ESPSUPP_* names.  Building
# against the internal implementations keeps this self contained, at the cost of
# the features that need elliptic curve: IDF implements crypto_ec_* only in
# src/crypto/crypto_mbedtls-ec.c, so WPA3-SAE and DPP cannot be enabled here.
# WPA/WPA2 personal and enterprise, and WPS, do not need it.
LT_PROJECT_SOURCE_FILES     := port/os_xtensa.c

LT_PROJECT_SOURCE_FILES     += src/ap/ap_config.c            \
                               src/ap/ieee802_1x.c           \
                               src/ap/wpa_auth.c             \
                               src/ap/wpa_auth_ie.c

LT_PROJECT_SOURCE_FILES     += src/common/dragonfly.c        \
                               src/common/sae.c              \
                               src/common/wpa_common.c

LT_PROJECT_SOURCE_FILES     += src/eap_peer/chap.c           \
                               src/eap_peer/eap.c            \
                               src/eap_peer/eap_common.c     \
                               src/eap_peer/eap_fast.c       \
                               src/eap_peer/eap_fast_common.c\
                               src/eap_peer/eap_fast_pac.c   \
                               src/eap_peer/eap_mschapv2.c   \
                               src/eap_peer/eap_peap.c       \
                               src/eap_peer/eap_peap_common.c\
                               src/eap_peer/eap_tls.c        \
                               src/eap_peer/eap_tls_common.c \
                               src/eap_peer/eap_ttls.c       \
                               src/eap_peer/mschapv2.c

LT_PROJECT_SOURCE_FILES     += src/rsn_supp/pmksa_cache.c    \
                               src/rsn_supp/wpa.c            \
                               src/rsn_supp/wpa_ie.c

LT_PROJECT_SOURCE_FILES     += src/utils/base64.c            \
                               src/utils/bitfield.c          \
                               src/utils/common.c            \
                               src/utils/ext_password.c      \
                               src/utils/json.c              \
                               src/utils/uuid.c              \
                               src/utils/wpa_debug.c         \
                               src/utils/wpabuf.c

LT_PROJECT_SOURCE_FILES     += src/wps/wps.c                 \
                               src/wps/wps_attr_build.c      \
                               src/wps/wps_attr_parse.c      \
                               src/wps/wps_attr_process.c    \
                               src/wps/wps_common.c          \
                               src/wps/wps_dev_attr.c        \
                               src/wps/wps_enrollee.c        \
                               src/wps/wps_registrar.c       \
                               src/wps/wps_validate.c

# Internal TLS, standing in for src/crypto/tls_mbedtls.c
LT_PROJECT_SOURCE_FILES     += src/tls/asn1.c                \
                               src/tls/bignum.c              \
                               src/tls/pkcs1.c               \
                               src/tls/pkcs5.c               \
                               src/tls/pkcs8.c               \
                               src/tls/rsa.c                 \
                               src/tls/tls_internal.c        \
                               src/tls/tlsv1_client.c        \
                               src/tls/tlsv1_client_read.c   \
                               src/tls/tlsv1_client_write.c  \
                               src/tls/tlsv1_common.c        \
                               src/tls/tlsv1_cred.c          \
                               src/tls/tlsv1_record.c        \
                               src/tls/tlsv1_server.c        \
                               src/tls/tlsv1_server_read.c   \
                               src/tls/tlsv1_server_write.c  \
                               src/tls/x509v3.c

# Crypto that is common to both of IDF's branches
LT_PROJECT_SOURCE_FILES     += src/crypto/aes-gcm.c          \
                               src/crypto/aes-siv.c          \
                               src/crypto/ccmp.c             \
                               src/crypto/crypto_ops.c       \
                               src/crypto/dh_group5.c        \
                               src/crypto/dh_groups.c        \
                               src/crypto/md4-internal.c     \
                               src/crypto/ms_funcs.c         \
                               src/crypto/sha1-prf.c         \
                               src/crypto/sha1-tlsprf.c      \
                               src/crypto/sha256-kdf.c       \
                               src/crypto/sha256-prf.c       \
                               src/crypto/sha256-tlsprf.c    \
                               src/crypto/sha384-prf.c       \
                               src/crypto/sha384-tlsprf.c

# ...and the internal implementations behind it.  crypto_mbedtls-rsa.c is on
# IDF's internal list too and is not a mistake: everything in it sits behind
# USE_MBEDTLS_CRYPTO, so it compiles away to nothing here.
LT_PROJECT_SOURCE_FILES     += src/crypto/aes-cbc.c              \
                               src/crypto/aes-ccm.c              \
                               src/crypto/aes-ctr.c              \
                               src/crypto/aes-internal.c         \
                               src/crypto/aes-internal-dec.c     \
                               src/crypto/aes-internal-enc.c     \
                               src/crypto/aes-omac1.c            \
                               src/crypto/aes-unwrap.c           \
                               src/crypto/aes-wrap.c             \
                               src/crypto/crypto_internal.c      \
                               src/crypto/crypto_internal-cipher.c \
                               src/crypto/crypto_internal-modexp.c \
                               src/crypto/crypto_internal-rsa.c  \
                               src/crypto/crypto_mbedtls-rsa.c   \
                               src/crypto/des-internal.c         \
                               src/crypto/md5.c                  \
                               src/crypto/md5-internal.c         \
                               src/crypto/rc4.c                  \
                               src/crypto/sha1.c                 \
                               src/crypto/sha1-internal.c        \
                               src/crypto/sha1-pbkdf2.c          \
                               src/crypto/sha1-tprf.c            \
                               src/crypto/sha256.c               \
                               src/crypto/sha256-internal.c      \
                               src/crypto/sha384-internal.c      \
                               src/crypto/sha512-internal.c

# The Espressif side of the component - the glue between the generic supplicant
# above and the esp_wifi_* entry points the blobs export.
LT_PROJECT_SOURCE_FILES     += esp_supplicant/src/esp_common.c    \
                               esp_supplicant/src/esp_hostap.c    \
                               esp_supplicant/src/esp_wpa2.c      \
                               esp_supplicant/src/esp_wpa_main.c  \
                               esp_supplicant/src/esp_wpas_glue.c \
                               esp_supplicant/src/esp_wps.c

# The port layer.  roku-lt-port supplies what this component includes that LT
# has no equivalent for: esp_types.h and esp_bit_defs.h copied verbatim from the
# bootloader's vendored IDF headers, an esp_log.h that lands wpa_printf() in
# LT's logger, the freertos/ subset esp_wpa2.c uses, and empty mbedtls/
# placeholders.  Each file says why it exists.
LT_PROJECT_SOURCE_FILES     += roku-lt-port/esp_log.c   \
                               roku-lt-port/freertos.c  \
                               roku-lt-port/os_unistd.c

################################################################################
# Includes.  The first three are IDF's INCLUDE_DIRS and the next three its
# PRIV_INCLUDE_DIRS; then the port layer, then the vendored Wi-Fi driver headers,
# which is where esp_wifi.h and the esp32s3 sdkconfig.h come from.
LT_PUBLIC_INCLUDE_FLAGS     += -I$(LT_PROJECT_SOURCE_DIR)/include                \
                               -I$(LT_PROJECT_SOURCE_DIR)/port/include           \
                               -I$(LT_PROJECT_SOURCE_DIR)/esp_supplicant/include \
                               -I$(LT_PROJECT_SOURCE_DIR)/src                    \
                               -I$(LT_PROJECT_SOURCE_DIR)/src/utils              \
                               -I$(LT_PROJECT_SOURCE_DIR)/esp_supplicant/src     \
                               -I$(LT_PROJECT_SOURCE_DIR)/roku-lt-port

LT_PUBLIC_INCLUDE_FLAGS     += -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(WIFI_DRV_DIR)/include \
                               -I$(LT_PLATFORM_PUBLIC_INCLUDE_DIR)/$(WIFI_DRV_DIR)/include/esp32s3

################################################################################
# IDF's unconditional target_compile_definitions, less CONFIG_FAST_PBKDF2 and
# CONFIG_DPP: the first needs src/crypto/fastpbkdf2.c and the second
# src/common/dpp.c, and neither is on IDF's own list for this configuration.
LT_CFLAGS_GENERIC           += -DESP_PLATFORM -D__ets__ -DESP_SUPPLICANT -DESPRESSIF_USE \
                               -DESP32_WORKAROUND -DIEEE8021X_EAPOL -DEAP_PEER_METHOD    \
                               -DEAP_MSCHAPv2 -DEAP_TLS -DEAP_TTLS -DEAP_PEAP -DEAP_FAST \
                               -DUSE_WPA2_TASK -DUSE_WPS_TASK -DCONFIG_WPS2              \
                               -DCONFIG_WPS_PIN -DCONFIG_ECC -DCONFIG_IEEE80211W         \
                               -DCONFIG_SHA256 -DCONFIG_WNM

# IDF's own target_compile_options for the component
LT_CFLAGS_GENERIC           += -Wno-strict-aliasing -Wno-write-strings

# LT builds everything -Wall -Wextra -Werror; IDF builds this component without
# -Wextra, and hostap upstream has never been clean under it.  Rather than carry
# a patch per warning in vendored code, turn off the -Wextra warnings this
# component trips and leave the rest of -Wall -Werror in force.
LT_CFLAGS_GENERIC           += -Wno-unused-parameter -Wno-sign-compare \
                               -Wno-missing-field-initializers -Wno-old-style-declaration

# src/eap_peer/eap.h and esp_supplicant/src/esp_wpa2.c declare the g_wpa_*
# credential globals as tentative definitions in a header, so every translation
# unit that includes it defines them.  That links only where the compiler
# defaults to -fcommon, which GCC 8 - the compiler IDF v4.4 ships for this part -
# does and GCC 11 does not.  Restore the assumption the vendored code was
# written under rather than patch the headers.
LT_CFLAGS_GENERIC           += -fcommon

# The supplicant prints its u32 with %X throughout, and u32 is uint32_t, which
# this toolchain's newlib makes unsigned long rather than unsigned int - same
# width, same varargs promotion, different spelling, so every one of those is a
# -Wformat diagnostic and none is a defect.  GCC has no narrower switch than the
# whole -Wformat group.
LT_CFLAGS_GENERIC           += -Wno-format

# make
include $(LT_PROJECT_RULES_MAKEFILE)

################################################################################
#   LOG
################################################################################
#   28-Aug-26   claudius    created
