/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-linear-firmware.h"

/**
 * FuLinearFirmware:
 *
 * A firmware made up of concatenated blobs of a different firmware type.
 *
 * NOTE: All the child images will be of the specified `GType`.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	GType image_gtype;
} FuLinearFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuLinearFirmware, fu_linear_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_linear_firmware_get_instance_private(o))

enum { PROP_0, PROP_IMAGE_GTYPE, PROP_LAST };

static void
fu_linear_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuLinearFirmware *self = FU_LINEAR_FIRMWARE(firmware);
	FuLinearFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kv(bn, "image_gtype", g_type_name(priv->image_gtype));
}

/**
 * fu_linear_firmware_get_image_gtype:
 * @self: a #FuLinearFirmware
 *
 * Gets the image #GType to use when parsing a byte buffer.
 *
 * Returns: integer
 *
 * Since: 1.8.2
 **/
GType
fu_linear_firmware_get_image_gtype(FuLinearFirmware *self)
{
	FuLinearFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINEAR_FIRMWARE(self), G_TYPE_INVALID);
	return priv->image_gtype;
}

static gboolean
fu_linear_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuLinearFirmware *self = FU_LINEAR_FIRMWARE(firmware);
	FuLinearFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	/* simple properties */
	tmp = xb_node_query_text(n, "image_gtype", NULL);
	if (tmp != NULL) {
		priv->image_gtype = g_type_from_name(tmp);
		if (priv->image_gtype == G_TYPE_INVALID) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_FOUND,
				    "GType %s not registered",
				    tmp);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_linear_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuLinearFirmware *self = FU_LINEAR_FIRMWARE(firmware);
	FuLinearFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize bufsz = g_bytes_get_size(fw);

	while (offset < bufsz) {
		g_autoptr(FuFirmware) img = g_object_new(priv->image_gtype, NULL);
		g_autoptr(GBytes) fw_tmp = NULL;

		fw_tmp = fu_bytes_new_offset(fw, offset, bufsz - offset, error);
		if (fw_tmp == NULL)
			return FALSE;
		if (!fu_firmware_parse(img, fw_tmp, flags | FWUPD_INSTALL_FLAG_NO_SEARCH, error)) {
			g_prefix_error(error, "failed to parse at 0x%x: ", (guint)offset);
			return FALSE;
		}
		fu_firmware_set_offset(firmware, offset);
		fu_firmware_add_image(firmware, img);

		/* next! */
		offset += fu_firmware_get_size(img);
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_linear_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);

	/* add each file */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) blob = NULL;
		fu_firmware_set_offset(img, buf->len);
		blob = fu_firmware_write(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf, blob);
	}

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_linear_firmware_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuLinearFirmware *self = FU_LINEAR_FIRMWARE(object);
	FuLinearFirmwarePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_IMAGE_GTYPE:
		g_value_set_gtype(value, priv->image_gtype);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_linear_firmware_set_property(GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	FuLinearFirmware *self = FU_LINEAR_FIRMWARE(object);
	FuLinearFirmwarePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_IMAGE_GTYPE:
		priv->image_gtype = g_value_get_gtype(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_linear_firmware_init(FuLinearFirmware *self)
{
}

static void
fu_linear_firmware_class_init(FuLinearFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->get_property = fu_linear_firmware_get_property;
	object_class->set_property = fu_linear_firmware_set_property;
	klass_firmware->parse = fu_linear_firmware_parse;
	klass_firmware->write = fu_linear_firmware_write;
	klass_firmware->export = fu_linear_firmware_export;
	klass_firmware->build = fu_linear_firmware_build;

	/**
	 * FuLinearFirmware:image-gtype:
	 *
	 * The image #GType
	 *
	 * Since: 1.8.2
	 */
	pspec =
	    g_param_spec_gtype("image-gtype",
			       NULL,
			       NULL,
			       FU_TYPE_FIRMWARE,
			       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_IMAGE_GTYPE, pspec);
}

/**
 * fu_linear_firmware_new:
 * @image_gtype: a #GType, e.g. %FU_TYPE_OPROM_FIRMWARE
 *
 * Creates a new #FuFirmware made up of concatenated images.
 *
 * Since: 1.8.2
 **/
FuFirmware *
fu_linear_firmware_new(GType image_gtype)
{
	return g_object_new(FU_TYPE_LINEAR_FIRMWARE, "image-gtype", image_gtype, NULL);
}
