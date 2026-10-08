/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-usb-hid-descriptor-private.h"

static void
fu_usb_hid_descriptor_func(void)
{
	gboolean ret;
	g_autofree gchar *json = NULL;
	GBytes *blob; /* transfer none */
	g_autoptr(FuUsbHidDescriptor) desc = g_object_new(FU_TYPE_USB_HID_DESCRIPTOR, NULL);
	g_autoptr(GBytes) blob_in = g_bytes_new_static("\x01\x02\x03\x04", 4);
	g_autoptr(GError) error = NULL;

	/* trivial setters and getters */
	fu_usb_hid_descriptor_set_iface_number(desc, 0x3);
	g_assert_cmpint(fu_usb_hid_descriptor_get_iface_number(desc), ==, 0x3);
	fu_usb_hid_descriptor_set_blob(desc, blob_in);
	g_assert_true(fu_usb_hid_descriptor_get_blob(desc) == blob_in);

	/* the blob is serialized to the "Data" JSON key, base64-encoded */
	json = fwupd_codec_to_json_string(FWUPD_CODEC(desc), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json);
	g_assert_nonnull(g_strstr_len(json, -1, "AQIDBA==")); /* base64 of 01 02 03 04 */

	/* and parsed back */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(desc), json, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob = fu_usb_hid_descriptor_get_blob(desc);
	g_assert_nonnull(blob);
	g_assert_cmpint(g_bytes_get_size(blob), ==, 4);
}

static void
fu_usb_hid_descriptor_parse_func(void)
{
	gboolean ret;
	g_autoptr(FuUsbHidDescriptor) desc = g_object_new(FU_TYPE_USB_HID_DESCRIPTOR, NULL);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 buf[] = {
	    0x09, /* bLength */
	    0x21, /* bDescriptorType, HID */
	    0x11,
	    0x01, /* bcdHID */
	    0x00, /* bCountryCode */
	    0x01, /* bNumDescriptors */
	    0x22, /* bDescriptorType, Report */
	    0x40,
	    0x00, /* wDescriptorLength */
	};

	blob = g_bytes_new_static(buf, sizeof(buf));
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(desc),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_usb_hid_descriptor_get_descriptor_length(desc), ==, 0x40);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/usb-hid-descriptor", fu_usb_hid_descriptor_func);
	g_test_add_func("/fwupd/usb-hid-descriptor/parse", fu_usb_hid_descriptor_parse_func);
	return g_test_run();
}
