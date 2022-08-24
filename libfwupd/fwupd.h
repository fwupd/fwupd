/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#define __FWUPD_H_INSIDE__

#include <libfwupd/fwupd-bios-setting.h>
#include <libfwupd/fwupd-client-sync.h>
#include <libfwupd/fwupd-client.h>
#include <libfwupd/fwupd-common.h>
#include <libfwupd/fwupd-device.h>
#include <libfwupd/fwupd-enums.h>
#include <libfwupd/fwupd-error.h>
#include <libfwupd/fwupd-plugin.h>
#include <libfwupd/fwupd-release.h>
#include <libfwupd/fwupd-remote.h>
#include <libfwupd/fwupd-request.h>
#include <libfwupd/fwupd-security-attr.h>
#include <libfwupd/fwupd-version.h>

#ifndef FWUPD_DISABLE_DEPRECATED
#include <libfwupd/fwupd-deprecated.h>
#endif

#undef __FWUPD_H_INSIDE__
