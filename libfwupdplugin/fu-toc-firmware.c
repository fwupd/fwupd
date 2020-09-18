/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuFirmware"

#include "config.h"

#include <xmlb.h>

#include "fu-common.h"
#include "fu-firmware-image-private.h"
#include "fu-toc-firmware.h"

/**
 * SECTION:fu-toc-firmware
 * @short_description: Table Of Contents firmware
 *
 * An object that can be used to contruct a #FuFirmware object from a directory
 * of files.
 *
 * See also: #FuFirmware
 */

struct _FuTocFirmware {
	FuFirmware		 parent_instance;
};

G_DEFINE_TYPE (FuTocFirmware, fu_toc_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_toc_firmware_tokenize (FuFirmware *firmware, GBytes *fw,
			  FwupdInstallFlags flags, GError **error)
{
	const gchar *tmp;
	g_autoptr(XbBuilder) builder = xb_builder_new ();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbNode) xb_firmware = NULL;
	g_autoptr(GPtrArray) xb_images = NULL;

	/* parse XML */
	if (!xb_builder_source_load_xml (source, g_bytes_get_data (fw, NULL),
					 XB_BUILDER_SOURCE_FLAG_NONE, error)) {
		g_prefix_error (error, "could not parse XML: ");
		return FALSE;
	}
	xb_builder_import_source (builder, source);
	silo = xb_builder_compile (builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;

	/* find version */
	xb_firmware = xb_silo_query_first (silo, "firmware", error);
	if (xb_firmware == NULL)
		return FALSE;
	tmp = xb_node_query_text (xb_firmware, "version", NULL);
	if (tmp != NULL)
		fu_firmware_set_version (firmware, tmp);

	/* parse images */
	xb_images = xb_node_query (xb_firmware, "image", 0, NULL);
	if (xb_images != NULL) {
		for (guint i = 0; i < xb_images->len; i++) {
			XbNode *xb_image = g_ptr_array_index (xb_images, i);
			guint64 tmpval;
			g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (NULL);
			tmp = xb_node_query_text (xb_image, "version", NULL);
			if (tmp != NULL)
				fu_firmware_image_set_version (img, tmp);
			tmp = xb_node_query_text (xb_image, "id", NULL);
			if (tmp != NULL)
				fu_firmware_image_set_id (img, tmp);
			tmpval = xb_node_query_text_as_uint (xb_image, "idx", NULL);
			if (tmpval != G_MAXUINT64)
				fu_firmware_image_set_idx (img, tmpval);
			tmpval = xb_node_query_text_as_uint (xb_image, "addr", NULL);
			if (tmpval != G_MAXUINT64)
				fu_firmware_image_set_addr (img, tmpval);
			tmp = xb_node_query_text (xb_image, "filename", NULL);
			if (tmp != NULL)
				fu_firmware_image_set_filename (img, tmp);
			fu_firmware_add_image (firmware, img);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_toc_firmware_parse (FuFirmware *firmware,
		       GBytes *fw,
		       guint64 addr_start,
		       guint64 addr_end,
		       FwupdInstallFlags flags,
		       GError **error)
{
	g_autoptr(GPtrArray) images = fu_firmware_get_images (firmware);

	/* load each file if required */
	for (guint i = 0; i < images->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (images, i);
		const gchar *fn = fu_firmware_image_get_filename (img);
		if (fn != NULL && fu_firmware_image_get_bytes (img) == NULL) {
			g_autoptr(GBytes) blob = NULL;
			blob = fu_common_get_contents_bytes (fn, error);
			if (blob == NULL)
				return FALSE;
			fu_firmware_image_set_bytes (img, blob);
		}
	}

	/* success */
	return TRUE;
}

static void
fu_toc_firmware_init (FuTocFirmware *self)
{
}

static void
fu_toc_firmware_class_init (FuTocFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_toc_firmware_parse;
	klass_firmware->tokenize = fu_toc_firmware_tokenize;
}

/**
 * fu_toc_firmware_new:
 *
 * Creates a new #FuFirmware of sub-type Table Of Contents
 *
 * Since: 1.5.0
 **/
FuFirmware *
fu_toc_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_TOC_FIRMWARE, NULL));
}
