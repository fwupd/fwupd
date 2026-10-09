/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bluez-device.h"
#include "fu-context-private.h"
#include "fu-device-private.h"

static const gchar *json_uuids = "{\n"
				 "  \"Uuids\": [\n"
				 "    {\n"
				 "      \"Uuid\": \"00002a26-0000-1000-8000-00805f9b34fb\",\n"
				 "      \"Path\": \"/org/bluez/hci0/dev_01/service0001/char0002\"\n"
				 "    },\n"
				 "    {\n"
				 "      \"Uuid\": \"00002a28-0000-1000-8000-00805f9b34fb\",\n"
				 "      \"Path\": \"/org/bluez/hci0/dev_01/service0001/char0003\"\n"
				 "    }\n"
				 "  ]\n"
				 "}";

static void
fu_bluez_device_json_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autofree gchar *str2 = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) dev = g_object_new(FU_TYPE_BLUEZ_DEVICE, "context", ctx, NULL);
	g_autoptr(FuDevice) dev2 = g_object_new(FU_TYPE_BLUEZ_DEVICE, "context", ctx, NULL);
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FwupdJsonObject) json_obj_out = fwupd_json_object_new();
	g_autoptr(FwupdJsonParser) parser = fwupd_json_parser_new();
	g_autoptr(FwupdJsonNode) node = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) json_out = NULL;

	/* parse the UUID -> path map from JSON */
	node =
	    fwupd_json_parser_load_from_data(parser, json_uuids, FWUPD_JSON_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(node);
	json_obj = fwupd_json_node_get_object(node, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_obj);
	ret = fu_device_from_json(dev, json_obj, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the UUIDs show up in the daemon string */
	str = fu_device_to_string(dev);
	g_assert_nonnull(g_strstr_len(str, -1, "00002a26-0000-1000-8000-00805f9b34fb"));
	g_assert_nonnull(g_strstr_len(str, -1, "char0003"));

	/* serialize back out */
	fu_device_add_json(dev, json_obj_out, FWUPD_CODEC_FLAG_NONE);
	json_out = fwupd_json_object_to_string(json_obj_out, FWUPD_JSON_EXPORT_FLAG_NONE);
	g_assert_nonnull(json_out);
	g_assert_nonnull(g_strstr_len(json_out->str, -1, "00002a28-0000-1000-8000-00805f9b34fb"));

	/* incorporating copies the UUID map to another device */
	fu_device_incorporate(dev2, dev, FU_DEVICE_INCORPORATE_FLAG_ALL);
	str2 = fu_device_to_string(dev2);
	g_assert_nonnull(g_strstr_len(str2, -1, "00002a26-0000-1000-8000-00805f9b34fb"));
}

static void
fu_bluez_device_json_invalid_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) dev = g_object_new(FU_TYPE_BLUEZ_DEVICE, "context", ctx, NULL);
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FwupdJsonParser) parser = fwupd_json_parser_new();
	g_autoptr(FwupdJsonNode) node = NULL;
	g_autoptr(GError) error = NULL;
	/* an entry missing the "Path" key */
	const gchar *json_bad = "{\"Uuids\":[{\"Uuid\":\"00002a26-0000-1000-8000-00805f9b34fb\"}]}";

	node =
	    fwupd_json_parser_load_from_data(parser, json_bad, FWUPD_JSON_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(node);
	json_obj = fwupd_json_node_get_object(node, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json_obj);
	ret = fu_device_from_json(dev, json_obj, &error);
	g_assert_false(ret);
	g_assert_nonnull(error);
}

static void
fu_bluez_device_version_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) dev = g_object_new(FU_TYPE_BLUEZ_DEVICE, "context", ctx, NULL);

	/* the convert_version vfunc formats the raw version as a BCD string */
	fu_device_set_version_format(dev, FWUPD_VERSION_FORMAT_BCD);
	fu_device_set_version_raw(dev, 0x0102);
	g_assert_cmpstr(fu_device_get_version(dev), ==, "1.2");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/bluez-device/json", fu_bluez_device_json_func);
	g_test_add_func("/fwupd/bluez-device/json-invalid", fu_bluez_device_json_invalid_func);
	g_test_add_func("/fwupd/bluez-device/version", fu_bluez_device_version_func);
	return g_test_run();
}
