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

#include <fwupd.h>
#include <glib-object.h>
#include <gudev/gudev.h>
#include <string.h>

#include "fu-cleanup.h"
#include "fu-device.h"
#include "fu-provider-udev.h"

static void     fu_provider_udev_finalize	(GObject	*object);

#define FU_PROVIDER_UDEV_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_PROVIDER_UDEV, FuProviderUdevPrivate))

/**
 * FuProviderUdevPrivate:
 **/
struct _FuProviderUdevPrivate
{
	GHashTable		*devices;
	GUdevClient		*gudev_client;
};

G_DEFINE_TYPE (FuProviderUdev, fu_provider_udev, FU_TYPE_PROVIDER)

/**
 * fu_provider_udev_get_name:
 **/
static const gchar *
fu_provider_udev_get_name (FuProvider *provider)
{
	return "Udev";
}

/**
 * fu_provider_udev_get_id:
 **/
static gchar *
fu_provider_udev_get_id (GUdevDevice *device)
{
	gchar *id;
	id = g_strdup_printf ("ro-%s", g_udev_device_get_sysfs_path (device));
	g_strdelimit (id, "/:.-", '_');
	return id;
}

/**
 * fu_guid_is_valid:
 **/
static gboolean
fu_guid_is_valid (const gchar *guid)
{
	_cleanup_strv_free_ gchar **split = NULL;
	if (guid == NULL)
		return FALSE;
	split = g_strsplit (guid, "-", -1);
	if (g_strv_length (split) != 5)
		return FALSE;
	if (strlen (split[0]) != 8)
		return FALSE;
	if (strlen (split[1]) != 4)
		return FALSE;
	if (strlen (split[2]) != 4)
		return FALSE;
	if (strlen (split[3]) != 4)
		return FALSE;
	if (strlen (split[4]) != 12)
		return FALSE;
	return TRUE;
}

/**
 * fu_guid_generate:
 **/
static gchar *
fu_guid_generate (const gchar *guid)
{
	gchar *tmp;
	tmp = g_compute_checksum_for_string (G_CHECKSUM_SHA1, guid, -1);
	tmp[8] = '-';
	tmp[13] = '-';
	tmp[18] = '-';
	tmp[23] = '-';
	tmp[36] = '\0';
	g_assert (fu_guid_is_valid (tmp));
	return tmp;
}

/**
 * fu_strstr_bin:
 **/
static gchar *
fu_strstr_bin (const gchar *haystack, gsize haystack_len, const gchar *needle)
{
	guint i;
	guint needle_len = strlen (needle);
	for (i = 0; i < haystack_len - needle_len; i++) {
		if (strncmp (haystack + i, needle, needle_len) == 0)
			return g_strdup (haystack + i + needle_len);
	}
	return NULL;
}

/**
 * fu_provider_udev_get_rom_version:
 **/
static gchar *
fu_provider_udev_get_rom_version (const gchar *rom_fn, GError **error)
{
	gchar buffer[1024];
	gchar *str;
	gssize sz;
	_cleanup_object_unref_ GFile *file = NULL;
	_cleanup_object_unref_ GFileInputStream *input_stream = NULL;
	_cleanup_object_unref_ GFileOutputStream *output_stream = NULL;

	file = g_file_new_for_path (rom_fn);
	input_stream = g_file_read (file, NULL, error);
	if (input_stream == NULL)
		return NULL;

	/* we have to enable the read */
	output_stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error);
	if (output_stream == NULL)
		return NULL;
	if (g_output_stream_write (G_OUTPUT_STREAM (output_stream), "1", 1, NULL, error) < 0)
		return NULL;

	sz = g_input_stream_read (G_INPUT_STREAM (input_stream),
				  buffer,
				  sizeof (buffer),
				  NULL,
				  error);
	if (sz < 0)
		return NULL;

	/* NVIDIA */
	str = fu_strstr_bin (buffer, sizeof (buffer), "Version ");
	if (str != NULL)
		return str;

	/* ATI */
	str = fu_strstr_bin (buffer, sizeof (buffer), " VER");
	if (str != NULL)
		return str;

	/* not known */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Firmware version extractor not known");
	return NULL;
}

/**
 * fu_provider_udev_client_add:
 **/
static void
fu_provider_udev_client_add (FuProviderUdev *provider_udev, GUdevDevice *device)
{
	FuDevice *dev;
	const gchar *display_name;
	const gchar *guid;
	const gchar *product;
	const gchar *vendor;
	_cleanup_free_ gchar *guid_new = NULL;
	_cleanup_free_ gchar *id = NULL;
	_cleanup_free_ gchar *rom_fn = NULL;
	_cleanup_free_ gchar *version = NULL;
	_cleanup_strv_free_ gchar **split = NULL;

	/* interesting device? */
	guid = g_udev_device_get_property (device, "FWUPD_GUID");
	if (guid == NULL)
		return;

	/* get data */
	g_debug ("adding udev device: %s", g_udev_device_get_sysfs_path (device));
	if (0) {
		const gchar * const *keys;
		guint i;
		keys = g_udev_device_get_property_keys (device);
		for (i = 0; keys[i] != NULL; i++)
			g_debug ("KEY: %s=%s", keys[i],
				 g_udev_device_get_property (device, keys[i]));

		keys = g_udev_device_get_sysfs_attr_keys (device);
		for (i = 0; keys[i] != NULL; i++)
			g_debug ("SYS: %s=%s", keys[i],
				 g_udev_device_get_sysfs_attr (device, keys[i]));
	}

	/* is already in database */
	id = fu_provider_udev_get_id (device);
	dev = g_hash_table_lookup (provider_udev->priv->devices, id);
	if (dev != NULL) {
		g_debug ("ignoring duplicate %s", id);
		return;
	}

	/* get the FW version from the BCD device revision */
	product = g_udev_device_get_property (device, "PRODUCT");
	if (product != NULL) {
		split = g_strsplit (product, "/", -1);
		if (g_strv_length (split) != 3) {
			g_warning ("env{PRODUCT} is invalid: %s", product);
			return;
		}
		version = g_strdup (split[2]);
	}

	/* get the FW version from the rom */
	rom_fn = g_build_filename (g_udev_device_get_sysfs_path (device), "rom", NULL);
	if (g_file_test (rom_fn, G_FILE_TEST_EXISTS)) {
		_cleanup_error_free_ GError *error = NULL;
		version = fu_provider_udev_get_rom_version (rom_fn, &error);
		if (version == NULL) {
			g_warning ("Failed to get version from %s: %s",
				   rom_fn, error->message);
		}
	}

	/* we failed */
	if (version == NULL)
		return;

	/* check the guid */
	if (!fu_guid_is_valid (guid)) {
		guid_new = fu_guid_generate (guid);
	} else {
		guid_new = g_strdup (guid);
	}

	/* did we get enough data */
	dev = fu_device_new ();
	fu_device_set_id (dev, id);
	fu_device_set_guid (dev, guid_new);
	display_name = g_udev_device_get_property (device, "FWUPD_MODEL");
	if (display_name == NULL)
		display_name = g_udev_device_get_property (device, "ID_MODEL_FROM_DATABASE");
	if (display_name != NULL)
		fu_device_set_display_name (dev, display_name);
	vendor = g_udev_device_get_property (device, "FWUPD_VENDOR");
	if (vendor == NULL)
		vendor = g_udev_device_get_property (device, "ID_VENDOR_FROM_DATABASE");
	if (vendor != NULL)
		fu_device_set_metadata (dev, FU_DEVICE_KEY_VENDOR, vendor);
	fu_device_set_metadata (dev, FU_DEVICE_KEY_VERSION, version);

	/* insert to hash */
	g_hash_table_insert (provider_udev->priv->devices, g_strdup (id), dev);
	fu_provider_device_add (FU_PROVIDER (provider_udev), dev);
}

/**
 * fu_provider_udev_client_remove:
 **/
static void
fu_provider_udev_client_remove (FuProviderUdev *provider_udev, GUdevDevice *device)
{
	FuDevice *dev;
	_cleanup_free_ gchar *id = NULL;

	/* interesting device? */
	if (g_udev_device_get_property (device, "FWUPD_GUID") == NULL)
		return;

	/* already in database */
	id = fu_provider_udev_get_id (device);
	dev = g_hash_table_lookup (provider_udev->priv->devices, id);
	if (dev == NULL)
		return;
	fu_provider_device_remove (FU_PROVIDER (provider_udev), dev);
}

/**
 * fu_provider_udev_client_uevent_cb:
 **/
static void
fu_provider_udev_client_uevent_cb (GUdevClient *gudev_client,
				   const gchar *action,
				   GUdevDevice *udev_device,
				   FuProviderUdev *provider_udev)
{
	if (g_strcmp0 (action, "remove") == 0) {
		fu_provider_udev_client_remove (provider_udev, udev_device);
		return;
	}
	if (g_strcmp0 (action, "add") == 0) {
		fu_provider_udev_client_add (provider_udev, udev_device);
		return;
	}
}

/**
 * fu_provider_udev_coldplug:
 **/
static gboolean
fu_provider_udev_coldplug (FuProvider *provider, GError **error)
{
	FuProviderUdev *provider_udev = FU_PROVIDER_UDEV (provider);
	GList *devices;
	GList *l;
	GUdevDevice *udev_device;
	const gchar *devclass[] = { "usb", "pci", NULL };
	guint i;

	/* get all devices of class */
	for (i = 0; devclass[i] != NULL; i++) {
		devices = g_udev_client_query_by_subsystem (provider_udev->priv->gudev_client,
							    devclass[i]);
		for (l = devices; l != NULL; l = l->next) {
			udev_device = l->data;
			fu_provider_udev_client_add (provider_udev, udev_device);
		}
		g_list_foreach (devices, (GFunc) g_object_unref, NULL);
		g_list_free (devices);
	}

	return TRUE;
}

/**
 * fu_provider_udev_class_init:
 **/
static void
fu_provider_udev_class_init (FuProviderUdevClass *klass)
{
	FuProviderClass *provider_class = FU_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->get_name = fu_provider_udev_get_name;
	provider_class->coldplug = fu_provider_udev_coldplug;
	object_class->finalize = fu_provider_udev_finalize;

	g_type_class_add_private (klass, sizeof (FuProviderUdevPrivate));
}

/**
 * fu_provider_udev_init:
 **/
static void
fu_provider_udev_init (FuProviderUdev *provider_udev)
{
	const gchar *subsystems[] = { NULL };
	provider_udev->priv = FU_PROVIDER_UDEV_GET_PRIVATE (provider_udev);
	provider_udev->priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
							      g_free, (GDestroyNotify) g_object_unref);
	provider_udev->priv->gudev_client = g_udev_client_new (subsystems);
	g_signal_connect (provider_udev->priv->gudev_client, "uevent",
			  G_CALLBACK (fu_provider_udev_client_uevent_cb), provider_udev);
}

/**
 * fu_provider_udev_finalize:
 **/
static void
fu_provider_udev_finalize (GObject *object)
{
	FuProviderUdev *provider_udev = FU_PROVIDER_UDEV (object);
	FuProviderUdevPrivate *priv = provider_udev->priv;

	g_hash_table_unref (priv->devices);
	g_object_unref (priv->gudev_client);

	G_OBJECT_CLASS (fu_provider_udev_parent_class)->finalize (object);
}

/**
 * fu_provider_udev_new:
 **/
FuProvider *
fu_provider_udev_new (void)
{
	FuProviderUdev *provider;
	provider = g_object_new (FU_TYPE_PROVIDER_UDEV, NULL);
	return FU_PROVIDER (provider);
}
