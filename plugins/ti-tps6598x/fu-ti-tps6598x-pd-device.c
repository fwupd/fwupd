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
	g_autofree gchar *name = g_strdup_printf("TPS6598X PD#%u", self->target);
	g_autofree gchar *logical_id = g_strdup_printf("PD%u", self->target);
	fu_device_set_name(device, name);
	fu_device_set_logical_id(device, logical_id);
	fu_device_add_instance_u8(device, "PD", self->target);
	return TRUE;
}

static gboolean
fu_ti_tps6598x_pd_device_ensure_version(FuTiTps6598xPdDevice *self, GError **error)
{
	FuTiTps6598xDevice *proxy = FU_TI_TPS6598X_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	g_autoptr(GByteArray) buf = NULL;
	g_autofree gchar *str = NULL;

	buf = fu_ti_tps6598x_device_read_target_register(proxy,
							 self->target,
							 TI_TPS6598X_REGISTER_VERSION,
							 4,
							 error);
	if (buf == NULL)
		return FALSE;
	str = g_strdup_printf("%02X%02X.%02X.%02X",
			      buf->data[3],
			      buf->data[2],
			      buf->data[1],
			      buf->data[0]);
	fu_device_set_version(FU_DEVICE(self), str);
	return TRUE;
}

static gboolean
fu_ti_tps6598x_pd_device_ensure_tx_identity(FuTiTps6598xPdDevice *self, GError **error)
{
	FuTiTps6598xDevice *proxy = FU_TI_TPS6598X_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	guint16 val = 0;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_ti_tps6598x_device_read_target_register(proxy,
							 self->target,
							 TI_TPS6598X_REGISTER_TX_IDENTITY,
							 47,
							 error);
	if (buf == NULL)
		return FALSE;
	if (!fu_memread_uint16_safe(buf->data, buf->len, 0x01, &val, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (val != 0x0 && val != 0xFF)
		fu_device_add_instance_u16(FU_DEVICE(self), "VID", val);
	if (!fu_memread_uint16_safe(buf->data, buf->len, 0x0B, &val, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (val != 0x0 && val != 0xFF)
		fu_device_add_instance_u16(FU_DEVICE(self), "PID", val);
	if (!fu_memread_uint16_safe(buf->data, buf->len, 0x09, &val, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (val != 0x0 && val != 0xFF)
		fu_device_add_instance_u16(FU_DEVICE(self), "REV", val);

	/* success */
	return TRUE;
}

static gboolean
fu_ti_tps6598x_pd_device_setup(FuDevice *device, GError **error)
{
	FuTiTps6598xPdDevice *self = FU_TI_TPS6598X_PD_DEVICE(device);

	/* register reads are slow, so do as few as possible */
	if (!fu_ti_tps6598x_pd_device_ensure_version(self, error))
		return FALSE;
	if (!fu_ti_tps6598x_pd_device_ensure_tx_identity(self, error))
		return FALSE;

	/* add new instance IDs */
	if (!fu_device_build_instance_id(FU_DEVICE(self), error, "USB", "VID", "PID", "PD", NULL))
		return FALSE;
	return fu_device_build_instance_id(FU_DEVICE(self),
					   error,
					   "USB",
					   "VID",
					   "PID",
					   "REV",
					   "PD",
					   NULL);
}

static void
fu_ti_tps6598x_pd_device_report_metadata_pre(FuDevice *device, GHashTable *metadata)
{
	FuTiTps6598xPdDevice *self = FU_TI_TPS6598X_PD_DEVICE(device);
	FuTiTps6598xDevice *proxy = FU_TI_TPS6598X_DEVICE(fu_device_get_proxy(device));

	/* this is too slow to do for each update... */
	if (g_getenv("FWUPD_TI_TPS6598X_VERBOSE") == NULL)
		return;
	for (guint i = 0; i < 0x80; i++) {
		g_autoptr(GByteArray) buf = NULL;
		g_autoptr(GError) error_local = NULL;

		buf = fu_ti_tps6598x_device_read_target_register(proxy,
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
	FuTiTps6598xDevice *proxy = FU_TI_TPS6598X_DEVICE(fu_device_get_proxy(device));
	return fu_device_attach_full(FU_DEVICE(proxy), progress, error);
}

static gboolean
fu_ti_tps6598x_pd_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuTiTps6598xDevice *proxy = FU_TI_TPS6598X_DEVICE(fu_device_get_proxy(device));
	return fu_ti_tps6598x_device_write_firmware(FU_DEVICE(proxy),
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
fu_ti_tps6598x_pd_device_new(FuDevice *proxy, guint8 target)
{
	FuTiTps6598xPdDevice *self = g_object_new(FU_TYPE_TI_TPS6598X_PD_DEVICE,
						  "context",
						  fu_device_get_context(proxy),
						  "proxy",
						  proxy,
						  NULL);
	self->target = target;
	return FU_DEVICE(self);
}
