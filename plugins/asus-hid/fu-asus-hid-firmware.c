/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-asus-hid-firmware.h"
#include "fu-asus-hid-struct.h"

#define FGA_OFFSET 0x2010

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

const gchar *
fu_asus_hid_firmware_get_product(FuFirmware *firmware)
{
	FuAsusHidFirmware *self = FU_ASUS_HID_FIRMWARE(firmware);
	return self->product;
}

const gchar *
fu_asus_hid_firmware_get_version(FuFirmware *firmware)
{
	FuAsusHidFirmware *self = FU_ASUS_HID_FIRMWARE(firmware);
	return self->version;
}

static gboolean
fu_asus_hid_firmware_parse(FuFirmware *firmware,
			   GInputStream *stream,
			   gsize offset,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuAsusHidFirmware *self = FU_ASUS_HID_FIRMWARE(firmware);
	g_autoptr(GByteArray) desc = NULL;
	g_autoptr(FuFirmware) img_payload = fu_firmware_new();
	g_autoptr(GInputStream) stream_payload = NULL;

	desc = fu_struct_asus_hid_desc_parse_stream(stream, FGA_OFFSET, error);
	if (desc == NULL)
		return FALSE;
	self->fga = g_strdup(fu_struct_asus_hid_desc_get_fga(desc));
	self->product = g_strdup(fu_struct_asus_hid_desc_get_product(desc));
	self->version = g_strdup(fu_struct_asus_hid_desc_get_version(desc));

	stream_payload = fu_partial_input_stream_new(stream, 0x2000, 0x3e000, error);
	if (stream_payload == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_payload, stream_payload, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_id(img_payload, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(firmware, img_payload);

	return TRUE;
}

static void
fu_asus_hid_firmware_init(FuAsusHidFirmware *self)
{
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
	object_class->finalize = fu_asus_hid_firmware_finalize;
	firmware_class->parse = fu_asus_hid_firmware_parse;
	firmware_class->export = fu_asus_hid_firmware_export;
}

FuFirmware *
fu_asus_hid_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ASUS_HID_FIRMWARE, NULL));
}
