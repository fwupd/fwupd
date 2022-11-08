/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include "fu-ti-tps6598x-common.h"
#include "fu-ti-tps6598x-device.h"
#include "fu-ti-tps6598x-firmware.h"
#include "fu-ti-tps6598x-pd-device.h"

struct _FuTiTps6598xPdDevice {
	FuDevice parent_instance;
	guint8 target;
};

G_DEFINE_TYPE(FuTiTps6598xPdDevice, fu_ti_tps6598x_pd_device, FU_TYPE_DEVICE)

static gboolean
fu_ti_tps6598x_pd_device_probe(FuDevice *device, GError **error)
{
	FuTiTps6598xPdDevice *self = FU_TI_TPS6598X_PD_DEVICE(device);
	FuTiTps6598xDevice *parent = FU_TI_TPS6598X_DEVICE(fu_device_get_parent(device));
	g_autofree gchar *name = g_strdup_printf("TPS6598X PD#%u", self->target);
	g_autofree gchar *logical_id = g_strdup_printf("PD%u", self->target);

	/* do as few register reads as possible as they are s...l...o...w... */
	fu_device_set_name(device, name);
	fu_device_set_logical_id(device, logical_id);

	/* fake GUID */
	fu_device_add_instance_u16(device, "VID", fu_usb_device_get_vid(FU_USB_DEVICE(parent)));
	fu_device_add_instance_u16(device, "PID", fu_usb_device_get_pid(FU_USB_DEVICE(parent)));
	fu_device_add_instance_u8(device, "PD", self->target);
	return fu_device_build_instance_id(device, error, "USB", "VID", "PID", "PD", NULL);
}

static gboolean
fu_ti_tps6598x_pd_device_setup(FuDevice *device, GError **error)
{
	FuTiTps6598xPdDevice *self = FU_TI_TPS6598X_PD_DEVICE(device);
	FuTiTps6598xDevice *parent = FU_TI_TPS6598X_DEVICE(fu_device_get_parent(device));
	g_autoptr(GByteArray) buf = NULL;
	g_autofree gchar *version = NULL;
	g_autofree gchar *config = NULL;

	/* register reads are s...l...o...w... */
	buf = fu_ti_tps6598x_device_read_target_register(parent,
							 self->target,
							 TI_TPS6598X_REGISTER_VERSION,
							 4,
							 error);
	if (buf == NULL)
		return FALSE;
	version = g_strdup_printf("%02X%02X.%02X.%02X",
				  buf->data[3],
				  buf->data[2],
				  buf->data[1],
				  buf->data[0]);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_version(device, version);

	/* the PD OTP config should be unique enough */
	buf = fu_ti_tps6598x_device_read_target_register(parent,
							 self->target,
							 TI_TPS6598X_REGISTER_OTP_CONFIG,
							 12,
							 error);
	if (buf == NULL)
		return FALSE;
	config = fu_byte_array_to_string(buf);
	fu_device_add_instance_strup(device, "CONFIG", config);

	/* success */
	return fu_device_build_instance_id(device,
					   error,
					   "USB",
					   "VID",
					   "PID",
					   "PD",
					   "CONFIG",
					   NULL);
}

static void
fu_ti_tps6598x_pd_device_report_metadata_pre(FuDevice *device, GHashTable *metadata)
{
	FuTiTps6598xPdDevice *self = FU_TI_TPS6598X_PD_DEVICE(device);
	FuTiTps6598xDevice *parent = FU_TI_TPS6598X_DEVICE(fu_device_get_parent(device));

	/* this is too slow to do for each update... */
	if (g_getenv("FWUPD_TI_TPS6598X_VERBOSE") == NULL)
		return;
	for (guint i = 0; i < 0x30; i++) {
		g_autoptr(GByteArray) buf = NULL;
		g_autoptr(GError) error_local = NULL;

		buf = fu_ti_tps6598x_device_read_target_register(parent,
								 self->target,
								 i,
								 63,
								 &error_local);
		if (buf == NULL) {
			g_debug("failed to get target 0x%02x register 0x%02x: %s",
				self->target,
				i,
				error_local->message);
			continue;
		}
		if (!fu_ti_tps6598x_byte_array_is_nonzero(buf))
			continue;
		g_hash_table_insert(
		    metadata,
		    g_strdup_printf("Tps6598xPd%02xRegister@0x%02x", self->target, i),
		    fu_byte_array_to_string(buf));
	}
}

static gboolean
fu_ti_tps6598x_pd_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuTiTps6598xDevice *parent = FU_TI_TPS6598X_DEVICE(fu_device_get_parent(device));
	return fu_device_attach_full(FU_DEVICE(parent), progress, error);
}

static gboolean
fu_ti_tps6598x_pd_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuTiTps6598xDevice *parent = FU_TI_TPS6598X_DEVICE(fu_device_get_parent(device));
	return fu_ti_tps6598x_device_write_firmware(FU_DEVICE(parent),
						    firmware,
						    progress,
						    flags,
						    error);
}

static void
fu_ti_tps6598x_pd_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 91, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 9, "reload");
}

static void
fu_ti_tps6598x_pd_device_init(FuTiTps6598xPdDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.ti.tps6598x");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_VENDOR);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_TI_TPS6598X_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), 30000);
}

static void
fu_ti_tps6598x_pd_device_class_init(FuTiTps6598xPdDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->write_firmware = fu_ti_tps6598x_pd_device_write_firmware;
	klass_device->attach = fu_ti_tps6598x_pd_device_attach;
	klass_device->setup = fu_ti_tps6598x_pd_device_setup;
	klass_device->probe = fu_ti_tps6598x_pd_device_probe;
	klass_device->report_metadata_pre = fu_ti_tps6598x_pd_device_report_metadata_pre;
	klass_device->set_progress = fu_ti_tps6598x_pd_device_set_progress;
}

FuDevice *
fu_ti_tps6598x_pd_device_new(FuContext *ctx, guint8 target)
{
	FuTiTps6598xPdDevice *self =
	    g_object_new(FU_TYPE_TI_TPS6598X_PD_DEVICE, "context", ctx, NULL);
	self->target = target;
	return FU_DEVICE(self);
}
