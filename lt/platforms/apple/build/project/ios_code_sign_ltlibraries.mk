################################################################################
#
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
#
################################################################################

LT_SPACE   :=
LT_NEWLINE := $(LT_SPACE)
LT_BANNER  := ____________________________
LT_PROG    := ios_code_sign_ltlibraries.mk

LT_IDENTITY ?= $(shell security find-identity -p codesigning | grep 'Apple Development' | tail -n 1 | cut -f2 -d'"')

ifneq (1, $(LT_QUIET))
LT_OUT := $(info $(LT_BANNER))
LT_OUT := $(info $(LT_PROG))
endif

ifeq (, $(LT_IDENTITY))
ifneq (1, $(LT_QUIET))
	LT_OUT := $(info $(LT_NEWLINE))
	LT_OUT := $(info $(LT_PROG): No valid codesigning identity found.  Cannot sign libraries.))
	LT_OUT := $(info $(LT_PROG): Register your Apple ID with Xcode to generate a codesigning identity.)
    LT_OUT := $(info $(LT_NEWLINE))
endif
	LT_OUT := $(error Identity not found)
endif

ifneq (1, $(LT_QUIET))
LT_OUT := $(info $(LT_NEWLINE))
LT_OUT := $(info Validating signing identity:)
LT_OUT := $(info $(LT_SPACE) $(LT_SPACE) IDENTITY = $(LT_IDENTITY))
endif

LT_IDENTITY_VERIFY := $(shell echo "$(LT_IDENTITY)" | cut -f1 -d':')
ifneq ("Apple Development", "$(LT_IDENTITY_VERIFY)")
ifneq (1, $(LT_QUIET))
	LT_OUT := $(info $(LT_SPACE)   $(LT_SPACE) Invalid codesigning identity.  Cannot sign libraries..)
	LT_OUT := $(info $(LT_SPACE)   $(LT_SPACE)) Code signing identity must begin with "Apple Developer")
	LT_OUT := $(info $(LT_NEWLINE))
endif
	LT_OUT := $(error Invalid identity)
endif

ifneq (1, $(LT_QUIET))
	LT_OUT := $(info $(LT_NEWLINE))
endif

.PHONY: all
all: do_codesign

.PHONY: do_codesign
do_codesign:
	$(LT_QUIET_CMD) @echo SIGN $(LT_TARGET_BIN_DIR)
	$(LT_EXEC_CMD)  codesign -f -s "$(LT_IDENTITY)" $(LT_TARGET_BIN_DIR)/*.so
