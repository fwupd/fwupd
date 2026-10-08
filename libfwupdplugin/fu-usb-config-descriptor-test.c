/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-usb-config-descriptor-private.h"

static void
fu_usb_config_descriptor_json_func(void)
{
	gboolean ret;
	g_autofree gchar *json = NULL;
	g_autoptr(FuUsbConfigDescriptor) desc = fu_usb_config_descriptor_new();
	g_autoptr(GError) error = NULL;

	/* populate from JSON */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(desc),
					   "{\"Configuration\":7,\"ConfigurationValue\":5}",
					   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_usb_config_descriptor_get_configuration(desc), ==, 0x7);
	g_assert_cmpint(fu_usb_config_descriptor_get_configuration_value(desc), ==, 0x5);

	/* and serialize back out */
	json = fwupd_codec_to_json_string(FWUPD_CODEC(desc), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json);
	g_assert_nonnull(g_strstr_len(json, -1, "Configuration"));
	g_assert_nonnull(g_strstr_len(json, -1, "ConfigurationValue"));
}

static void
fu_usb_config_descriptor_parse_func(void)
{
	gboolean ret;
	g_autoptr(FuUsbConfigDescriptor) desc = fu_usb_config_descriptor_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 buf[] = {
	    0x09, /* bLength */
	    0x02, /* bDescriptorType, Config */
	    0x20,
	    0x00, /* wTotalLength */
	    0x01, /* bNumInterfaces */
	    0x05, /* bConfigurationValue */
	    0x07, /* iConfiguration */
	    0x80, /* bmAttributes */
	    0x32, /* bMaxPower */
	};

	blob = g_bytes_new_static(buf, sizeof(buf));
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(desc),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_usb_config_descriptor_get_configuration_value(desc), ==, 0x5);
	g_assert_cmpint(fu_usb_config_descriptor_get_configuration(desc), ==, 0x7);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/usb-config-descriptor/json", fu_usb_config_descriptor_json_func);
	g_test_add_func("/fwupd/usb-config-descriptor/parse", fu_usb_config_descriptor_parse_func);
	return g_test_run();
}
