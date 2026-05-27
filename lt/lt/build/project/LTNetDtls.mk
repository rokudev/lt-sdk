################################################################################
# LTNetDtls.mk - project makefile for LT Library LTNetDtls
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR		:=	    $(LT_PROJECT_SOURCE_DIR_BASE)/lt/net/dtls
LT_PROJECT_SOURCE_FILES		+= 	    LTNetDtls.c
LT_PROJECT_SOURCE_FILES		+=      LTNetDtlsCrypto.c

LT_PROJECT_SOURCE_SUBDIRS   :=      mbedtls

LT_PROJECT_SOURCE_FILES		+=      mbedtls/asn1parse.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/asn1write.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/base64.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/bignum.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/bignum_core.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/bignum_mod.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/bignum_mod_raw.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/cipher.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/cipher_wrap.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/constant_time.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/debug.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/ecdh.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/ecp.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/ecp_curves.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/entropy.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/hash_info.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/md.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/oid.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/pem.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/pk.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/pk_wrap.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/pkparse.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/platform_util.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/rsa.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/rsa_alt_helpers.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/ssl_ciphersuites.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/ssl_client.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/ssl_cookie.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/ssl_debug_helpers_generated.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/ssl_msg.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/ssl_ticket.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/ssl_tls.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/ssl_tls12_client.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/x509.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/x509_crl.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/x509_crt.c
LT_PROJECT_SOURCE_FILES		+=      mbedtls/x509_csr.c

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   13-Aug-23   valerian    created
