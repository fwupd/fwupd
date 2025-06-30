/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCabFirmware"

#include "config.h"

#include "fu-cab-image.h"
#include "fu-common.h"

struct _FuCabImage {
	FuFirmware parent_instance;
	gchar *win32_filename;
	GDateTime *created;
};

G_DEFINE_TYPE(FuCabImage, fu_cab_image, FU_TYPE_FIRMWARE)

/**
 * fu_cab_image_get_win32_filename:
 * @self: a #FuCabImage
 *
 * Gets the in-archive Windows filename, with a possible path component -- creating from the
 * firmware ID if required.
 *
 * Returns: filename, or %NULL
 *
 * Since: 1.9.7
 **/
const gchar *
fu_cab_image_get_win32_filename(FuCabImage *self)
{
	g_return_val_if_fail(FU_IS_CAB_IMAGE(self), NULL);

	/* generate from the id */
	if (self->win32_filename == NULL) {
		g_autoptr(GString) str = g_string_new(fu_firmware_get_id(FU_FIRMWARE(self)));
		g_string_replace(str, "/", "\\", 0);
		if (str->len == 0)
			return NULL;
		fu_cab_image_set_win32_filename(self, str->str);
	}

	return self->win32_filename;
}

/**
 * fu_cab_image_set_win32_filename:
 * @self: a #FuCabImage
 * @win32_filename: filename, or %NULL
 *
 * Sets the in-archive Windows filename, with a possible path component.
 *
 * Since: 1.9.7
 **/
void
fu_cab_image_set_win32_filename(FuCabImage *self, const gchar *win32_filename)
{
	g_return_if_fail(FU_IS_CAB_IMAGE(self));
	g_free(self->win32_filename);
	self->win32_filename = g_strdup(win32_filename);
}

/**
 * fu_cab_image_get_created:
 * @self: a #FuCabImage
 *
 * Gets the created timestamp.
 *
 * Returns: (transfer none): a #GDateTime, or %NULL
 *
 * Since: 1.9.7
 **/
GDateTime *
fu_cab_image_get_created(FuCabImage *self)
{
	g_return_val_if_fail(FU_IS_CAB_IMAGE(self), NULL);
	return self->created;
}

/**
 * fu_cab_image_set_created:
 * @self: a #FuCabImage
 * @created: a #GDateTime, or %NULL
 *
 * Sets the created timestamp.
 *
 * Since: 1.9.7
 **/
void
fu_cab_image_set_created(FuCabImage *self, GDateTime *created)
{
	g_return_if_fail(FU_IS_CAB_IMAGE(self));
	if (self->created != NULL) {
		g_date_time_unref(self->created);
		self->created = NULL;
	}
	if (created != NULL)
		self->created = g_date_time_ref(created);
}

static gboolean
fu_cab_image_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuCabImage *self = FU_CAB_IMAGE(firmware);
	const gchar *tmp;

	/* simple properties */
	tmp = xb_node_query_text(n, "win32_filename", NULL);
	if (tmp != NULL)
		fu_cab_image_set_win32_filename(self, tmp);
	tmp = xb_node_query_text(n, "created", NULL);
	if (tmp != NULL) {
		g_autoptr(GDateTime) created = g_date_time_new_from_iso8601(tmp, NULL);
		if (created == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "not iso8601: %s",
				    tmp);
			return FALSE;
		}
		fu_cab_image_set_created(self, created);
	}

	/* success */
	return TRUE;
}

static void
fu_cab_image_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuCabImage *self = FU_CAB_IMAGE(firmware);
	fu_xmlb_builder_insert_kv(bn, "win32_filename", self->win32_filename);
	if (self->created != NULL) {
		g_autofree gchar *str = g_date_time_format_iso8601(self->created);
		fu_xmlb_builder_insert_kv(bn, "created", str);
	}
}

static void
fu_cab_image_finalize(GObject *object)
{
	FuCabImage *self = FU_CAB_IMAGE(object);
	g_free(self->win32_filename);
	if (self->created != NULL)
		g_date_time_unref(self->created);
	G_OBJECT_CLASS(fu_cab_image_parent_class)->finalize(object);
}

static void
fu_cab_image_class_init(FuCabImageClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_cab_image_finalize;
	firmware_class->build = fu_cab_image_build;
	firmware_class->export = fu_cab_image_export;
}

static void
fu_cab_image_init(FuCabImage *self)
{
}

/**
 * fu_cab_image_new:
 *
 * Returns: (transfer full): a #FuCabImage
 *
 * Since: 1.9.7
 **/
FuCabImage *
fu_cab_image_new(void)
{
	return g_object_new(FU_TYPE_CAB_IMAGE, NULL);
}
