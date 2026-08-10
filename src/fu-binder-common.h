/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <aidl/org/freedesktop/fwupd/FwupdDevice.h>
#include <aidl/org/freedesktop/fwupd/FwupdHwid.h>
#include <aidl/org/freedesktop/fwupd/FwupdInstallRequest.h>
#include <aidl/org/freedesktop/fwupd/FwupdMetadata.h>
#include <aidl/org/freedesktop/fwupd/FwupdProperties.h>
#include <aidl/org/freedesktop/fwupd/FwupdRelease.h>
#include <aidl/org/freedesktop/fwupd/FwupdRemote.h>
#include <aidl/org/freedesktop/fwupd/FwupdRequest.h>

#include <fwupd.h>

namespace aidl_fwupd = aidl::org::freedesktop::fwupd;

aidl_fwupd::FwupdRemote
fu_binder_remote_to_aidl(FwupdRemote *remote);
FwupdRemote *
fu_binder_remote_from_aidl(const aidl_fwupd::FwupdRemote &r);

aidl_fwupd::FwupdDevice
fu_binder_device_to_aidl(FwupdDevice *device);
FwupdDevice *
fu_binder_device_from_aidl(const aidl_fwupd::FwupdDevice &d);

aidl_fwupd::FwupdRelease
fu_binder_release_to_aidl(FwupdRelease *release);
FwupdRelease *
fu_binder_release_from_aidl(const aidl_fwupd::FwupdRelease &r);

aidl_fwupd::FwupdRequest
fu_binder_request_to_aidl(FwupdRequest *request);
