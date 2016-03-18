/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <appstream-glib.h>
#include <archive_entry.h>
#include <archive.h>
#include <fwupd.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <string.h>

#include "fu-device.h"
#include "fu-provider-rpi.h"

static void	fu_provider_rpi_finalize	(GObject	*object);

#define FU_PROVIDER_RPI_FIRMWARE_FILENAME		"start.elf"

/**
 * FuProviderRpiPrivate:
 **/
typedef struct {
	gchar			*fw_dir;
} FuProviderRpiPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuProviderRpi, fu_provider_rpi, FU_TYPE_PROVIDER)
#define GET_PRIVATE(o) (fu_provider_rpi_get_instance_private (o))

/**
 * fu_provider_rpi_get_name:
 **/
static const gchar *
fu_provider_rpi_get_name (FuProvider *provider)
{
	return "RaspberryPi";
}

/**
 * fu_provider_rpi_strstr:
 **/
static gchar *
fu_provider_rpi_strstr (const guint8 *haystack,
			gsize haystack_len,
			const gchar *needle,
			guint *offset)
{
	guint i;
	guint needle_len;

	if (needle == NULL || needle[0] == '\0')
		return NULL;
	if (haystack == NULL || haystack_len == 0)
		return NULL;
	needle_len = strlen (needle);
	if (needle_len > haystack_len)
		return NULL;
	for (i = 0; i < haystack_len - needle_len; i++) {
		if (memcmp (haystack + i, needle, needle_len) == 0) {
			if (offset != NULL)
				*offset = i + needle_len;
			return g_strdup ((const gchar *) &haystack[i + needle_len]);
		}
	}
	return NULL;
}

/**
 * fu_provider_rpi_parse_firmware:
 **/
static gboolean
fu_provider_rpi_parse_firmware (FuDevice *device, const gchar *fn, GError **error)
{
	GDate *date;
	gsize len = 0;
	guint offset;
	g_autofree gchar *fwver = NULL;
	g_autofree gchar *platform = NULL;
	g_autofree gchar *vc_date = NULL;
	g_autofree gchar *vc_time = NULL;
	g_autofree guint8 *data = NULL;

	/* read file -- things we can find are:
	 *
	 * VC_BUILD_ID_USER: dc4
	 * VC_BUILD_ID_TIME: 14:58:37
	 * VC_BUILD_ID_BRANCH: master
	 * VC_BUILD_ID_TIME: Aug  3 2015
	 * VC_BUILD_ID_HOSTNAME: dc4-XPS13-9333
	 * VC_BUILD_ID_PLATFORM: raspberrypi_linux
	 * VC_BUILD_ID_VERSION: 4b51d81eb0068a875b336f4cc2c468cbdd06d0c5 (clean)
	 */
	if (!g_file_get_contents (fn, (gchar **) &data, &len, error))
		return FALSE;

	/* check the platform matches */
	platform = fu_provider_rpi_strstr (data, len,
					   "VC_BUILD_ID_PLATFORM: ",
					   NULL);
	if (g_strcmp0 (platform, "raspberrypi_linux") != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "not a RasberryPi, platform is %s",
			     platform);
		return FALSE;
	}

	/* find the VC_BUILD info which paradoxically is split into two
	 * string segments */
	vc_time = fu_provider_rpi_strstr (data, len,
					  "VC_BUILD_ID_TIME: ", &offset);
	if (vc_time == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to get 1st VC_BUILD_ID_TIME");
		return FALSE;
	}
	vc_date = fu_provider_rpi_strstr (data + offset, len - offset,
					  "VC_BUILD_ID_TIME: ", NULL);
	if (vc_date == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to get 2nd VC_BUILD_ID_TIME");
		return FALSE;
	}

	/* parse the date */
	date = g_date_new ();
	g_date_set_parse (date, vc_date);
	if (!g_date_valid (date)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to parse date '%s'",
			     vc_date);
		return FALSE;
	}

	/* create a version number from the date and time */
	fwver = g_strdup_printf ("%04i%02i%02i",
				 g_date_get_year (date),
				 g_date_get_month (date),
				 g_date_get_day (date));
	fu_device_set_version (device, fwver);

	g_date_free (date);
	return TRUE;
}

/**
 * fu_provider_rpi_explode_file:
 **/
static gboolean
fu_provider_rpi_explode_file (struct archive_entry *entry, const gchar *dir)
{
	const gchar *tmp;
	g_autofree gchar *buf = NULL;

	/* no output file */
	if (archive_entry_pathname (entry) == NULL)
		return FALSE;

	/* update output path */
	tmp = archive_entry_pathname (entry);
	buf = g_build_filename (dir, tmp, NULL);
	archive_entry_update_pathname_utf8 (entry, buf);
	return TRUE;
}

/**
 * fu_provider_rpi_update:
 **/
static gboolean
fu_provider_rpi_update (FuProvider *provider,
			FuDevice *device,
			GBytes *blob_fw,
			FwupdInstallFlags flags,
			GError **error)
{
	FuProviderRpi *provider_rpi = FU_PROVIDER_RPI (provider);
	FuProviderRpiPrivate *priv = GET_PRIVATE (provider_rpi);
	gboolean ret = TRUE;
	gboolean valid;
	int r;
	struct archive *arch = NULL;
	struct archive_entry *entry;
	g_autofree gchar *fwfn = NULL;

	/* decompress anything matching either glob */
	fu_provider_set_status (provider, FWUPD_STATUS_DECOMPRESSING);
	arch = archive_read_new ();
	archive_read_support_format_all (arch);
	archive_read_support_filter_all (arch);
	r = archive_read_open_memory (arch,
				      (void *) g_bytes_get_data (blob_fw, NULL),
				      (size_t) g_bytes_get_size (blob_fw));
	if (r) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Cannot open: %s",
			     archive_error_string (arch));
		goto out;
	}
	fu_provider_set_status (provider, FWUPD_STATUS_DEVICE_WRITE);
	for (;;) {
		g_autofree gchar *path = NULL;
		r = archive_read_next_header (arch, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			ret = FALSE;
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Cannot read header: %s",
				     archive_error_string (arch));
			goto out;
		}

		/* only extract if valid */
		valid = fu_provider_rpi_explode_file (entry, priv->fw_dir);
		if (!valid)
			continue;
		r = archive_read_extract (arch, entry, 0);
		if (r != ARCHIVE_OK) {
			ret = FALSE;
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Cannot extract: %s",
				     archive_error_string (arch));
			goto out;
		}
	}

	/* get the new VC build info */
	fu_provider_set_status (provider, FWUPD_STATUS_DEVICE_VERIFY);
	fwfn = g_build_filename (priv->fw_dir,
				 FU_PROVIDER_RPI_FIRMWARE_FILENAME,
				 NULL);
	if (!fu_provider_rpi_parse_firmware (device, fwfn, error))
		return FALSE;
out:
	if (arch != NULL) {
		archive_read_close (arch);
		archive_read_free (arch);
	}
	return ret;
}

/**
 * fu_provider_rpi_coldplug:
 **/
static gboolean
fu_provider_rpi_coldplug (FuProvider *provider, GError **error)
{
	FuProviderRpi *provider_rpi = FU_PROVIDER_RPI (provider);
	FuProviderRpiPrivate *priv = GET_PRIVATE (provider_rpi);
	g_autofree gchar *fwfn = NULL;
	g_autofree gchar *fwver = NULL;
	g_autofree gchar *guid = NULL;
	g_autoptr(FuDevice) device = NULL;

	/* anything interesting */
	fwfn = g_build_filename (priv->fw_dir,
				 FU_PROVIDER_RPI_FIRMWARE_FILENAME,
				 NULL);
	if (!g_file_test (fwfn, G_FILE_TEST_EXISTS))
		return TRUE;

	/* create fake device */
	device = fu_device_new ();
	fu_device_set_id (device, "raspberry-pi");
	guid = as_utils_guid_from_string ("raspberrypi");
	fu_device_set_guid (device, guid);
	fu_device_set_name (device, "Raspberry Pi");
	fu_device_add_flag (device, FU_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (device, FU_DEVICE_FLAG_ALLOW_OFFLINE);
	fu_device_add_flag (device, FU_DEVICE_FLAG_ALLOW_ONLINE);
	fu_device_add_flag (device, FU_DEVICE_FLAG_REQUIRE_AC);

	/* get the VC build info */
	if (!fu_provider_rpi_parse_firmware (device, fwfn, error))
		return FALSE;

	fu_provider_device_add (provider, device);
	return TRUE;
}

/**
 * fu_provider_rpi_class_init:
 **/
static void
fu_provider_rpi_class_init (FuProviderRpiClass *klass)
{
	FuProviderClass *provider_class = FU_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->get_name = fu_provider_rpi_get_name;
	provider_class->coldplug = fu_provider_rpi_coldplug;
	provider_class->update_online = fu_provider_rpi_update;
	object_class->finalize = fu_provider_rpi_finalize;
}

/**
 * fu_provider_rpi_init:
 **/
static void
fu_provider_rpi_init (FuProviderRpi *provider_rpi)
{
	FuProviderRpiPrivate *priv = GET_PRIVATE (provider_rpi);
	const gchar *tmp;

	/* allow this to be overidden for testing */
	priv->fw_dir = g_strdup ("/boot");
	tmp = g_getenv ("FWUPD_RPI_FW_DIR");
	if (tmp != NULL)
		fu_provider_rpi_set_fw_dir (provider_rpi, tmp);
}

/**
 * fu_provider_rpi_set_fw_dir:
 **/
void
fu_provider_rpi_set_fw_dir (FuProviderRpi *provider_rpi, const gchar *fw_dir)
{
	FuProviderRpiPrivate *priv = GET_PRIVATE (provider_rpi);
	g_free (priv->fw_dir);
	priv->fw_dir = g_strdup (fw_dir);
	g_mkdir_with_parents (fw_dir, 0700);
}

/**
 * fu_provider_rpi_finalize:
 **/
static void
fu_provider_rpi_finalize (GObject *object)
{
	FuProviderRpi *provider_rpi = FU_PROVIDER_RPI (object);
	FuProviderRpiPrivate *priv = GET_PRIVATE (provider_rpi);

	g_free (priv->fw_dir);

	G_OBJECT_CLASS (fu_provider_rpi_parent_class)->finalize (object);
}

/**
 * fu_provider_rpi_new:
 **/
FuProvider *
fu_provider_rpi_new (void)
{
	FuProviderRpi *provider;
	provider = g_object_new (FU_TYPE_PROVIDER_RPI, NULL);
	return FU_PROVIDER (provider);
}
