/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-uf2-device.h"
#include "fu-uf2-firmware.h"

struct _FuUf2Device {
	FuBlockDevice parent_instance;
	guint family_id;
};

G_DEFINE_TYPE(FuUf2Device, fu_uf2_device, FU_TYPE_BLOCK_DEVICE)

static FuFirmware *
fu_uf2_device_prepare_firmware(FuDevice *device,
			       GBytes *fw,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_uf2_firmware_new();

	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;

	/* check the family_id matches if we can read the old firmware */
	if (self->family_id > 0 && fu_firmware_get_idx(firmware) > 0 &&
	    self->family_id != fu_firmware_get_idx(firmware)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "family ID was different, expected 0x%08x bytes and got 0x%08x",
			    self->family_id,
			    (guint32)fu_firmware_get_idx(firmware));
		return NULL;
	}

	/* success: but return the raw data */
	return fu_firmware_new_from_bytes(fw);
}

static gboolean
fu_uf2_device_probe_current_fw(FuDevice *device, GBytes *fw, GError **error)
{
	g_autofree gchar *csum_sha256 = NULL;
	g_autoptr(FuFirmware) firmware = fu_uf2_firmware_new();
	g_autoptr(GBytes) fw_raw = NULL;

	/* parse to get version */
	if (!fu_firmware_parse(firmware, fw, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;
	if (fu_firmware_get_version(firmware) != NULL)
		fu_device_set_version(device, fu_firmware_get_version(firmware));

	/* add instance ID for quirks */
	if (fu_firmware_get_idx(firmware) != 0x0) {
		g_autofree gchar *id0 = NULL;
		id0 = g_strdup_printf("UF2\\FID_%08X", (guint32)fu_firmware_get_idx(firmware));
		fu_device_add_instance_id_full(device, id0, FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}

	/* add device checksum */
	fw_raw = fu_firmware_get_bytes(firmware, error);
	if (fw_raw == NULL)
		return FALSE;
	csum_sha256 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, fw_raw);
	fu_device_add_checksum(device, csum_sha256);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_uf2_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_uf2_firmware_new();
	g_autoptr(GBytes) fw = NULL;

	fw = fu_device_dump_firmware(device, progress, error);
	if (fw == NULL)
		return NULL;
	if (!fu_firmware_parse(firmware, fw, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;

	return g_steal_pointer(&firmware);
}

static gboolean
fu_uf2_device_probe(FuDevice *device, GError **error)
{
	gsize bufsz = 0;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *fn2 = NULL;
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* FuBlockDevice->probe */
	if (!FU_DEVICE_CLASS(fu_uf2_device_parent_class)->probe(device, error))
		return FALSE;

	/* this has to exist */
	fn = g_build_filename(fu_device_get_logical_id(device), "INFO_UF2.TXT", NULL);
	if (!g_file_get_contents(fn, &buf, &bufsz, error))
		return FALSE;
	lines = fu_common_strnsplit(buf, bufsz, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix(lines[i], "Model: ")) {
			fu_device_set_name(device, lines[i] + 7);
		} else if (g_str_has_prefix(lines[i], "Board-ID: ")) {
			g_autofree gchar *id0 = NULL;
			id0 = g_strdup_printf("UF2\\BID_%s", lines[i] + 10);
			fu_device_add_instance_id(device, id0);
		}
	}

	/* this might exist */
	fn2 = g_build_filename(fu_device_get_logical_id(device), "CURRENT.UF2", NULL);
	fw = fu_common_get_contents_bytes(fn2, NULL);
	if (fw != NULL) {
		if (!fu_uf2_device_probe_current_fw(device, fw, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_uf2_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2);	/* reload */
}

static void
fu_uf2_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);

	/* FuBlockDevice->to_string */
	FU_DEVICE_CLASS(fu_uf2_device_parent_class)->to_string(device, idt, str);

	if (self->family_id > 0)
		fu_common_string_append_kx(str, idt, "FamilyId", self->family_id);
}

static void
fu_uf2_device_init(FuUf2Device *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_protocol(FU_DEVICE(self), "com.microsoft.uf2");
	fu_block_device_set_filename(FU_BLOCK_DEVICE(self), "CURRENT.UF2");
}

static void
fu_uf2_device_class_init(FuUf2DeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_uf2_device_to_string;
	klass_device->probe = fu_uf2_device_probe;
	klass_device->prepare_firmware = fu_uf2_device_prepare_firmware;
	klass_device->set_progress = fu_uf2_device_set_progress;
	klass_device->read_firmware = fu_uf2_device_read_firmware;
}
