/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include <libmm-glib.h>

#define FU_TYPE_MM_DEVICE (fu_mm_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuMmDevice, fu_mm_device, FU, MM_DEVICE, FuUdevDevice)

#define FU_MM_DEVICE_FLAG_USE_BRANCH "use-branch"

/* less ifdefs */
#if !MM_CHECK_VERSION(1, 24, 0)
#define MM_MODEM_FIRMWARE_UPDATE_METHOD_DFOTA	      (1 << 5)
#define MM_MODEM_FIRMWARE_UPDATE_METHOD_CINTERION_FDL (1 << 6)
#endif

struct _FuMmDeviceClass {
	FuUdevDeviceClass parent_class;
};

void
fu_mm_device_set_inhibited(FuMmDevice *self, gboolean inhibited) G_GNUC_NON_NULL(1);
gboolean
fu_mm_device_get_inhibited(FuMmDevice *self) G_GNUC_NON_NULL(1);
const gchar *
fu_mm_device_get_inhibition_uid(FuMmDevice *self) G_GNUC_NON_NULL(1);
gboolean
fu_mm_device_set_device_file(FuMmDevice *self, MMModemPortType port_type, GError **error)
    G_GNUC_NON_NULL(1);

gboolean
fu_mm_device_probe_from_omodem(FuMmDevice *self, MMObject *omodem, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_mm_device_at_cmd(FuMmDevice *self, const gchar *cmd, gboolean has_response, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_mm_device_set_autosuspend_delay(FuMmDevice *self, guint timeout_ms, GError **error)
    G_GNUC_NON_NULL(1);
void
fu_mm_device_add_instance_id(FuMmDevice *self, const gchar *device_id) G_GNUC_NON_NULL(1, 2);
