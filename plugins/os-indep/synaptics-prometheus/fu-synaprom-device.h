/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPROM_DEVICE (fu_synaprom_device_get_type())
G_DECLARE_FINAL_TYPE(FuSynapromDevice, fu_synaprom_device, FU, SYNAPROM_DEVICE, FuUsbDevice)

FuSynapromDevice *
fu_synaprom_device_new(FuUsbDevice *device);
gboolean
fu_synaprom_device_cmd_send(FuSynapromDevice *device,
			    GByteArray *request,
			    GByteArray *reply,
			    FuProgress *progress,
			    guint timeout_ms,
			    GError **error);
gboolean
fu_synaprom_device_write_fw(FuSynapromDevice *self,
			    GBytes *fw,
			    FuProgress *progress,
			    GError **error);

/* for self tests */
void
fu_synaprom_device_set_version(FuSynapromDevice *self,
			       guint8 vmajor,
			       guint8 vminor,
			       guint32 buildnum);
FuFirmware *
fu_synaprom_device_prepare_firmware(FuDevice *device,
				    GInputStream *stream,
				    FuProgress *progress,
				    FuFirmwareParseFlags flags,
				    GError **error);

guint32
fu_synaprom_device_get_product_type(FuSynapromDevice *self);
