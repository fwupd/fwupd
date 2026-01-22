/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPTICS_PROMETHEUS_DEVICE (fu_synaptics_prometheus_device_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsPrometheusDevice,
		     fu_synaptics_prometheus_device,
		     FU,
		     SYNAPTICS_PROMETHEUS_DEVICE,
		     FuUsbDevice)

FuSynapticsPrometheusDevice *
fu_synaptics_prometheus_device_new(FuUsbDevice *device);
gboolean
fu_synaptics_prometheus_device_cmd_send(FuSynapticsPrometheusDevice *device,
					GByteArray *request,
					GByteArray *reply,
					FuProgress *progress,
					guint timeout_ms,
					GError **error);
gboolean
fu_synaptics_prometheus_device_write_fw(FuSynapticsPrometheusDevice *self,
					GBytes *fw,
					FuProgress *progress,
					GError **error);

/* for self tests */
void
fu_synaptics_prometheus_device_set_version(FuSynapticsPrometheusDevice *self,
					   guint8 vmajor,
					   guint8 vminor,
					   guint32 buildnum);
FuFirmware *
fu_synaptics_prometheus_device_prepare_firmware(FuDevice *device,
						GInputStream *stream,
						FuProgress *progress,
						FuFirmwareParseFlags flags,
						GError **error);

guint32
fu_synaptics_prometheus_device_get_product_type(FuSynapticsPrometheusDevice *self);
