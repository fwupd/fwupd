/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-asus-hid-firmware.h"
#include "fu-asus-hid-struct.h"

#define FGA_OFFSET     0x2010
#define TRAILER_OFFSET 0x400 /* from the end */

struct _FuAsusHidFirmware {
	FuFirmware parent_instance;
	gchar *fga;
	gchar *product;
	gchar *version;
};

G_DEFINE_TYPE(FuAsusHidFirmware, fu_asus_hid_firmware, FU_TYPE_FIRMWARE)

static void
fu_asus_hid_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuAsusHidFirmware *self = FU_ASUS_HID_FIRMWARE(firmware);

	fu_xmlb_builder_insert_kv(bn, "fga", self->fga);
	fu_xmlb_builder_insert_kv(bn, "product", self->product);
	fu_xmlb_builder_insert_kv(bn, "version", self->version);
}

static gboolean
fu_asus_hid_firmware_parse(FuFirmware *firmware,
			   FuInputStream *stream,
			   FuFirmwareParseFlags flags,
			   GError **error)
{
	FuAsusHidFirmware *self = FU_ASUS_HID_FIRMWARE(firmware);
	gsize streamsz = 0;
	guint32 region_end;
	guint32 region_start;
	g_autoptr(FuStructAsusHidDesc) st = NULL;
	g_autoptr(FuStructAsusHidTrailer) st_trailer = NULL;
	g_autoptr(FuFirmware) img_payload = fu_firmware_new();
	g_autoptr(FuInputStream) stream_payload = NULL;

	st = fu_struct_asus_hid_desc_parse_stream(stream, FGA_OFFSET, error);
	if (st == NULL)
		return FALSE;
	self->fga = fu_struct_asus_hid_desc_get_fga(st);
	self->product = fu_struct_asus_hid_desc_get_product(st);
	self->version = fu_struct_asus_hid_desc_get_version(st);

	/* the trailer says which part of the flash this payload may be written
	 * to -- the rest of the image is padding, except for the recovery
	 * bootloader region which the device already holds and which is not
	 * shipped in the update at all */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < TRAILER_OFFSET) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "image is 0x%x bytes, too small to hold a trailer",
			    (guint)streamsz);
		return FALSE;
	}
	st_trailer =
	    fu_struct_asus_hid_trailer_parse_stream(stream, streamsz - TRAILER_OFFSET, error);
	if (st_trailer == NULL)
		return FALSE;
	region_start = fu_struct_asus_hid_trailer_get_region1_start(st_trailer);
	region_end = fu_struct_asus_hid_trailer_get_region1_end(st_trailer);
	if (region_start >= region_end || region_end >= streamsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "trailer region 0x%x..0x%x is not within the 0x%x byte image",
			    region_start,
			    region_end,
			    (guint)streamsz);
		return FALSE;
	}

	stream_payload =
	    fu_partial_input_stream_new(stream, region_start, region_end - region_start + 1, error);
	if (stream_payload == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_payload, stream_payload, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_addr(img_payload, region_start);
	fu_firmware_set_id(img_payload, FU_FIRMWARE_ID_PAYLOAD);
	return fu_firmware_add_image(firmware, img_payload, error);
}

static void
fu_asus_hid_firmware_init(FuAsusHidFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_asus_hid_firmware_finalize(GObject *object)
{
	FuAsusHidFirmware *self = FU_ASUS_HID_FIRMWARE(object);
	g_free(self->fga);
	g_free(self->product);
	g_free(self->version);
	G_OBJECT_CLASS(fu_asus_hid_firmware_parent_class)->finalize(object);
}

static void
fu_asus_hid_firmware_class_init(FuAsusHidFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_FIRMWARE);
	object_class->finalize = fu_asus_hid_firmware_finalize;
	firmware_class->parse = fu_asus_hid_firmware_parse;
	firmware_class->export = fu_asus_hid_firmware_export;
	fu_firmware_set_size_max(firmware_class, 32 * FU_MB);
}
