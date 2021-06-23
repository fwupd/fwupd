/*
 * Copyright (C) 2021, TUXEDO Computers GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include <fwupdplugin.h>

#include "fu-ec-common.h"
#include "fu-ec-it5570-device.h"

/* ROM of IT5570 consists of 64KB blocks. Blocks can be further subdivided in
 * 256-byte chunks, which is especially visible when erasing the ROM. This is
 * because in case of erasure offset within a block is specified in chunks (even
 * though erasure is done one kilobyte at a time).
 *
 * Accessing ROM requires entering a special mode, which should be always left
 * to restore normal operation of EC (handling of buttons, keyboard, etc.). */

#define EC_CMD_WRITE_BLOCK	0x02
#define EC_CMD_READ_BLOCK	0x03
#define EC_CMD_ERASE_KBYTE	0x05
#define EC_CMD_WRITE_1ST_KBYTE	0x06
#define EC_CMD_GET_PRJ		0x92
#define EC_CMD_GET_VER		0x93
#define EC_CMD_ID		0xf0
#define EC_ROM_ACCESS_ON_1     	0xde
#define EC_ROM_ACCESS_ON_2     	0xdc
#define EC_ROM_ACCESS_OFF     	0xfe

#define BLOCK_SIZE		0x10000
#define CHUNK_SIZE		0x100
#define CHUNKS_IN_KBYTE		0x4
#define CHUNKS_IN_BLOCK		0x100

#define MAX_FLASHING_ATTEMPTS	5

struct _FuEcIt5570Device {
	FuEcDevice		 parent_instance;
	gchar			*prj_name;
	gboolean		 ac_adapter_present;
};

G_DEFINE_TYPE (FuEcIt5570Device, fu_ec_it5570_device, FU_TYPE_EC_DEVICE)

static gboolean
fu_ec_it5570_device_ac_connected (FuEcDevice *self, GError **error)
{
	guint8 tmp = 0;

	if (!fu_ec_device_read_reg (self, 0x10, &tmp, error)) {
		g_prefix_error (error, "AC check: ");
		return FALSE;
	}

	FU_EC_IT5570_DEVICE (self)->ac_adapter_present = ((tmp & 0x01) != 0x00);
	return TRUE;
}

static gboolean
fu_ec_it5570_device_ec_check (FuEcDevice *self, GError **error)
{
	guint8 tmp = 0;
	gboolean is_it5570;

	/* enter ROM access mode */
	if (!fu_ec_device_write_cmd (self, EC_ROM_ACCESS_ON_1, error) ||
	    !fu_ec_device_write_cmd (self, EC_ROM_ACCESS_ON_2, error))
		goto error;

	if (!fu_ec_device_write_cmd (self, EC_CMD_ID, error))
		goto error;
	if (!fu_ec_device_read (self, &tmp, error))
		goto error;
	is_it5570 = (tmp != 0x00 && tmp != 0xff);

	/* leave ROM access mode */
	if (!fu_ec_device_write_cmd (self, EC_ROM_ACCESS_OFF, error))
		return FALSE;

	if (!is_it5570) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "EC doesn't look like IT5570");
		return FALSE;
	}

	/* success */
	return TRUE;

error:
	/* try leave ROM access mode to leave system in usable state */
	(void)fu_ec_device_write_cmd (self, EC_ROM_ACCESS_OFF, NULL);
	return FALSE;
}

static gboolean
fu_ec_it5570_device_ec_project (FuEcDevice *self, GError **error)
{
	/* initialize array with zeros */
	gchar project[16] = "";

	if (!fu_ec_device_write_cmd (self, EC_CMD_GET_PRJ, error))
		return FALSE;

	for (guint i = 0; i < sizeof(project) - 1; ++i) {
		guint8 tmp = 0;
		if (!fu_ec_device_read (self, &tmp, error)) {
			g_prefix_error (error, "failed to read firmware project: ");
			return FALSE;
		}

		if (tmp == '$') {
			break;
		}

		project[i] = tmp;
	}

	FU_EC_IT5570_DEVICE (self)->prj_name = g_strdup (project);

	/* success */
	return TRUE;
}

static gboolean
fu_ec_it5570_device_ec_version (FuEcDevice *self, GError **error)
{
	gchar version[16] = "1.";

	if (!fu_ec_device_write_cmd (self, EC_CMD_GET_VER, error))
		return FALSE;

	for (guint i = 2; i < sizeof(version) - 1; ++i) {
		guint8 tmp = 0;
		if (!fu_ec_device_read (self, &tmp, error)) {
			g_prefix_error (error, "failed to read firmware version: ");
			return FALSE;
		}

		if (tmp == '$') {
			break;
		}

		version[i] = tmp;
	}

	fu_device_set_version (FU_DEVICE (self), version);

	/* success */
	return TRUE;
}

static gboolean
fu_ec_it5570_device_ec_size (FuEcDevice *self, GError **error)
{
	guint8 tmp = 0;

	if (!fu_ec_device_read_reg (self, 0xf9, &tmp, error))
		return FALSE;

	switch (tmp & 0xf0) {
		case 0xf0:
			fu_device_set_firmware_size (FU_DEVICE (self), 0x40000);
			break;
		case 0x40:
			fu_device_set_firmware_size (FU_DEVICE (self), 0x30000);
			break;
		default:
			fu_device_set_firmware_size (FU_DEVICE (self), 0x20000);
			break;
	}

	return TRUE;
}

static gboolean
fu_ec_it5570_device_setup (FuDevice *device, GError **error)
{
	FuEcDevice *self = FU_EC_DEVICE (device);

	/* FuEcDevice->setup */
	if (!FU_DEVICE_CLASS (fu_ec_it5570_device_parent_class)->setup (device, error))
		return FALSE;

	/* basic initialization */
	if (!fu_ec_device_write_reg (self, 0xf9, 0x20, error) ||
	    !fu_ec_device_write_reg (self, 0xfa, 0x02, error) ||
	    !fu_ec_device_write_reg (self, 0xfb, 0x00, error) ||
	    !fu_ec_device_write_reg (self, 0xf8, 0xb1, error)) {
		g_prefix_error (error, "initialization: ");
		return FALSE;
	}

	/* Order of interactions with EC below matters. Additionally, reading EC
	 * project seems to be mandatory for successful firmware operations.
	 * Test after making changes here! */

	/* get size from the EC */
	if (!fu_ec_it5570_device_ec_size (self, error))
		return FALSE;

	/* FWUPD_DEVICE_FLAG_REQUIRE_AC doesn't always work */
	if (!fu_ec_it5570_device_ac_connected (FU_EC_DEVICE (device), error))
		return FALSE;

	/* get installed firmware project from the EC */
	if (!fu_ec_it5570_device_ec_project (self, error))
		return FALSE;

	/* get installed firmware version from the EC */
	if (!fu_ec_it5570_device_ec_version (self, error))
		return FALSE;

	/* make sure this is in fact IT5570 */
	if (!fu_ec_it5570_device_ec_check (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_ec_it5570_device_progress_cb (goffset current, goffset total, gpointer user_data)
{
	FuDevice *device = FU_DEVICE (user_data);
	fu_device_set_progress_full (device, (gsize) current, (gsize) total);
}

static GBytes *
fu_plugin_ec_patch_autoload (FuDevice *device, GBytes *fw, GError **error)
{
	guint offset;
	gsize sz = 0;
	guint autoload_action;
	const guint8 *unpatched = g_bytes_get_data (fw, &sz);
	gboolean small_flash = (sz <= 0x20000);
	g_autofree guint8 *patched = NULL;

	autoload_action = fu_device_get_metadata_integer (device, "AutoloadAction");
	if (autoload_action == G_MAXUINT || autoload_action == AUTOLOAD_NO_ACTION)
		return g_bytes_ref (fw);

	for (offset = 0; offset < sz - 6; ++offset) {
		if (unpatched[offset] == 0xa5 &&
		    (unpatched[offset + 1] == 0xa5 || unpatched[offset + 1] == 0xa4) &&
		    unpatched[offset + 5] == 0x5a)
			break;
	}

	if (offset >= sz - 6)
		return g_bytes_ref (fw);

	/* not big enough */
	if (offset + 8 >= sz) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "image is too small to patch");
		return NULL;
	}

	patched = fu_memdup_safe (unpatched, sz, error);
	if (patched == NULL)
		return NULL;

	if (autoload_action == AUTOLOAD_DISABLE) {
		patched[offset + 2] = (small_flash ? 0x94 : 0x85);
		patched[offset + 8] = 0x00;
	} else if (autoload_action == AUTOLOAD_SET_ON) {
		patched[offset + 2] = (small_flash ? 0x94 : 0x85);
		patched[offset + 8] = (small_flash ? 0x7f : 0xbe);
	} else if (autoload_action == AUTOLOAD_SET_OFF) {
		patched[offset + 2] = (small_flash ? 0xa5 : 0xb5);
		patched[offset + 8] = 0xaa;
	}

	return g_bytes_new_take (g_steal_pointer (&patched), sz);
}

/* progress callback is optional to not affect device progress during writing
 * firmware */
static GBytes *
fu_ec_it5570_device_get_firmware (FuDevice *device,
				  GFileProgressCallback progress_cb,
				  GError **error)
{
	FuEcDevice *self = FU_EC_DEVICE (device);
	guint64 fwsize = fu_device_get_firmware_size_min (device);
	guint64 block_count = (fwsize + BLOCK_SIZE - 1)/BLOCK_SIZE;
	goffset offset = 0;
	g_autofree guint8 *buf = NULL;

	buf = g_malloc0 (fwsize);

	for (guint i = 0; i < block_count; ++i) {
		if (!fu_ec_device_write_cmd (self, EC_CMD_READ_BLOCK, error) ||
		    !fu_ec_device_write_cmd (self, i, error))
			return NULL;

		for (guint j = 0; j < BLOCK_SIZE; ++j, ++offset) {
			if (!fu_ec_device_read (self, &buf[offset], error))
				return NULL;

			if (progress_cb != NULL)
				progress_cb (offset, (goffset) fwsize, self);
		}
	}

	return g_bytes_new_take (g_steal_pointer (&buf), fwsize);
}

static GBytes *
fu_ec_it5570_device_dump_firmware (FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* require detach -> attach */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) fu_device_detach,
					    (FuDeviceLockerFunc) fu_device_attach,
					    error);
	if (locker == NULL)
		return NULL;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);
	return fu_ec_it5570_device_get_firmware (device,
						 fu_ec_it5570_device_progress_cb,
						 error);
}

static gboolean
fu_ec_it5570_device_attach (FuDevice *device, GError **error)
{
	FuEcDevice *self = FU_EC_DEVICE (device);

	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* leave ROM access mode */
	if (!fu_ec_device_write_cmd (self, EC_ROM_ACCESS_OFF, error))
		return FALSE;

	/* success */
	fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_ec_it5570_device_detach (FuDevice *device, GError **error)
{
	FuEcDevice *self = FU_EC_DEVICE (device);

	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* enter ROM access mode */
	if (!fu_ec_device_write_cmd (self, EC_ROM_ACCESS_ON_1, error) ||
	    !fu_ec_device_write_cmd (self, EC_ROM_ACCESS_ON_2, error))
		return FALSE;

	/* success */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_ec_it5570_device_erase (FuDevice *device, GError **error)
{
	FuEcDevice *self = FU_EC_DEVICE (device);
	guint64 fwsize = fu_device_get_firmware_size_min (device);
	guint64 chunk_count = (fwsize + CHUNK_SIZE - 1)/CHUNK_SIZE;

	for (guint i = 0; i < chunk_count; i += CHUNKS_IN_KBYTE) {
		if (!fu_ec_device_write_cmd (self, EC_CMD_ERASE_KBYTE, error) ||
		    !fu_ec_device_write_cmd (self, i/CHUNKS_IN_BLOCK, error) ||
		    !fu_ec_device_write_cmd (self, i%CHUNKS_IN_BLOCK, error) ||
		    !fu_ec_device_write_cmd (self, 0x00, error))
			return FALSE;

		g_usleep (1000);
	}

	g_usleep (100000);
	return TRUE;
}

static gboolean
fu_ec_it5570_device_write_attempt (FuDevice *device, GBytes *firmware, GError **error)
{
	FuEcDevice *self = FU_EC_DEVICE (device);
	gsize fwsize = g_bytes_get_size (firmware);
	guint64 total = (fwsize + CHUNK_SIZE - 1)/CHUNK_SIZE;
	const guint8 *fw_data = NULL;
	g_autoptr(GBytes) erased_fw = NULL;
	g_autoptr(GBytes) written_fw = NULL;
	g_autoptr(GPtrArray) blocks = NULL;

	if (!fu_ec_it5570_device_erase (device, error))
		return FALSE;

	erased_fw = fu_ec_it5570_device_get_firmware (device, NULL, error);
	if (erased_fw == NULL) {
		g_prefix_error (error, "failed to read erased firmware");
		return FALSE;
	}
	if (!fu_common_bytes_is_empty (erased_fw)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "firmware was not erased");
		return FALSE;
	}

	blocks = fu_chunk_array_new_from_bytes (firmware, 0x00, 0x00, BLOCK_SIZE);

	/* write everything but the first kilobyte */
	for (guint i = 0; i < blocks->len; ++i) {
		FuChunk *block = g_ptr_array_index (blocks, i);
		gboolean first = (i == 0);
		guint32 offset = 0;
		guint32 bytes_left = fu_chunk_get_data_sz (block);
		const guint8 *data = fu_chunk_get_data (block);

		if (!fu_ec_device_write_cmd (self, EC_CMD_WRITE_BLOCK, error) ||
		    !fu_ec_device_write_cmd (self, 0x00, error) ||
		    !fu_ec_device_write_cmd (self, i, error) ||
		    !fu_ec_device_write_cmd (self, first ? 0x04 : 0x00, error) ||
		    !fu_ec_device_write_cmd (self, 0x00, error))
			return FALSE;

		for (guint j = 0; j < CHUNKS_IN_BLOCK; ++j) {
			gsize progress = i*CHUNKS_IN_BLOCK + j;

			if (first && j < CHUNKS_IN_KBYTE) {
				offset += CHUNK_SIZE;
				bytes_left -= CHUNK_SIZE;
				fu_device_set_progress_full (device, progress, (gsize) total);
				continue;
			}

			for (guint k = 0; k < CHUNK_SIZE; ++k) {
				if (bytes_left == 0) {
					if (!fu_ec_device_write (self, 0xff, error))
						return FALSE;
					continue;
				}

				if (!fu_ec_device_write (self, data[offset], error))
					return FALSE;

				++offset;
				--bytes_left;
			}

			fu_device_set_progress_full (device, progress, (gsize) total);
		}
	}

	/* now write the first kilobyte */
	if (!fu_ec_device_write_cmd (self, EC_CMD_WRITE_1ST_KBYTE, error))
		return FALSE;
	fw_data = g_bytes_get_data (firmware, NULL);
	for (guint i = 0; i < CHUNK_SIZE*CHUNKS_IN_KBYTE; ++i)
		if (!fu_ec_device_write (self, fw_data[i], error))
			return FALSE;

	g_usleep (1000);

	written_fw = fu_ec_it5570_device_get_firmware (device, NULL, error);
	if (written_fw == NULL) {
		g_prefix_error (error, "failed to read erased firmware");
		return FALSE;
	}
	if (!fu_common_bytes_compare (written_fw, firmware, error)) {
		g_prefix_error (error, "firmware verification");
		return FALSE;
	}

	/* success */
	fu_device_set_progress (device, 100);
	return TRUE;
}

static gboolean
fu_ec_it5570_device_write_firmware (FuDevice *device,
				    FuFirmware *firmware,
				    FwupdInstallFlags flags,
				    GError **error)
{
	gsize fwsize;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_patched = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	if (fu_device_get_metadata_boolean (device, "RequireAC") &&
			!FU_EC_IT5570_DEVICE (device)->ac_adapter_present) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "AC adapter is not connected");
		return FALSE;
	}

	/* require detach -> attach */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) fu_device_detach,
					    (FuDeviceLockerFunc) fu_device_attach,
					    error);
	if (locker == NULL)
		return FALSE;

	/* get default image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	fwsize = g_bytes_get_size (fw);
	if (fwsize < 1024) {
		g_prefix_error (error, "firmware is too small: %u", (guint) fwsize);
		return FALSE;
	}

	fw_patched = fu_plugin_ec_patch_autoload (device, fw, error);
	if (fw_patched == NULL)
		return FALSE;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);

	/* try this many times; the failure-to-flash case leaves you without a
	 * keyboard and future boot may completely fail */
	for (guint i = 1;; ++i) {
		g_autoptr(GError) error_chk = NULL;
		if (fu_ec_it5570_device_write_attempt (device, fw_patched, &error_chk))
			break;

		if (i == MAX_FLASHING_ATTEMPTS) {
			g_propagate_error (error, g_steal_pointer (&error_chk));
			return FALSE;
		}

		g_warning ("failure %u: %s", i, error_chk->message);
	}

	/* success */
	return TRUE;
}

static gchar *
fu_ec_extract_field (GBytes *fw, const gchar *name, GError **error)
{
	guint offset;
	gsize prefix_len;
	gsize fwsz = 0;
	const gchar *value;
	const guint8 *buf = g_bytes_get_data (fw, &fwsz);
	g_autofree gchar *field = g_strdup_printf ("%s:", name);

	prefix_len = strlen (field);

	for (offset = 0; offset < fwsz - prefix_len; ++offset) {
		if (memcmp (&buf[offset], field, prefix_len) == 0)
			break;
	}

	if (offset >= fwsz - prefix_len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "did not find %s field in the firmware image",
			     name);
		return NULL;
	}

	offset += prefix_len;
	value = (const gchar *) &buf[offset];

	for (; offset < fwsz; ++offset) {
		if (buf[offset] == '$')
			return g_strndup (value, (const gchar *)&buf[offset] - value);
	}

	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_INVALID_FILE,
		     "couldn't extract %s field value from the firmware image",
		     name);
	return NULL;
}

static FuFirmware *
fu_ec_device_prepare_firmware (FuDevice *device,
			       GBytes *fw,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuEcIt5570Device *self = FU_EC_IT5570_DEVICE (device);
	g_autofree gchar *prj_name = NULL;
	g_autofree gchar *version = NULL;
	g_autofree gchar *date = NULL;

	prj_name = fu_ec_extract_field (fw, "PRJ", error);
	if (prj_name == NULL)
		return NULL;

	version = fu_ec_extract_field (fw, "VER", error);
	if (version == NULL)
		version = g_strdup ("(unknown version)");

	date = fu_ec_extract_field (fw, "DATE", error);
	if (date == NULL)
		date = g_strdup ("(unknown build date)");

	g_debug ("New firmware: %s %s built on %s", prj_name, version, date);

	if (g_strcmp0 (prj_name, self->prj_name) != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "firmware targets %s instead of %s",
			     prj_name,
			     self->prj_name);
		return NULL;
	}

	return fu_firmware_new_from_bytes (fw);
}

static void
fu_ec_it5570_device_init (FuEcIt5570Device *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_ONLY_OFFLINE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	/* version string example: 1.07.02TR1 */
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PLAIN);
}

static void
fu_ec_it5570_device_finalize (GObject *obj)
{
	FuEcIt5570Device *self = FU_EC_IT5570_DEVICE (obj);
	g_free (self->prj_name);
}

static void
fu_ec_it5570_device_class_init (FuEcIt5570DeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	G_OBJECT_CLASS (klass)->finalize = fu_ec_it5570_device_finalize;
	klass_device->attach = fu_ec_it5570_device_attach;
	klass_device->detach = fu_ec_it5570_device_detach;
	klass_device->dump_firmware = fu_ec_it5570_device_dump_firmware;
	klass_device->write_firmware = fu_ec_it5570_device_write_firmware;
	klass_device->setup = fu_ec_it5570_device_setup;
	klass_device->prepare_firmware = fu_ec_device_prepare_firmware;
}
