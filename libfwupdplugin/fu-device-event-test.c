/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-device-event-private.h"
#include "fu-device-private.h"

static void
fu_device_event_donor_func(void)
{
	g_autoptr(FuDevice) device1 = fu_device_new(NULL);
	g_autoptr(FuDevice) device2 = fu_device_new(NULL);
	g_autoptr(FuDeviceEvent) event1 = fu_device_event_new("foo:bar:baz");
	g_autoptr(FuDeviceEvent) event2 = fu_device_event_new("aaa:bbb:ccc");
	g_autoptr(FuDeviceEvent) event3 = fu_device_event_new("foo:111:222");
	GPtrArray *events;

	fu_device_add_event(device1, event1);
	fu_device_add_event(device2, event2);
	fu_device_set_target(device1, device2);

	/* did we incorporate */
	events = fu_device_get_events(device2);
	g_assert_nonnull(events);
	g_assert_cmpint(events->len, ==, 2);

	/* make sure it is redirected */
	fu_device_add_event(device1, event3);
	events = fu_device_get_events(device2);
	g_assert_nonnull(events);
	g_assert_cmpint(events->len, ==, 3);
}

static void
fu_device_event_func(void)
{
	gboolean ret;
	const gchar *str;
	g_autofree gchar *json = NULL;
	g_autoptr(FuDeviceEvent) event1 = fu_device_event_new("foo:bar:baz");
	g_autoptr(FuDeviceEvent) event2 = fu_device_event_new(NULL);
	g_autoptr(GBytes) blob1 = g_bytes_new_static("hello", 6);
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GBytes) blob3 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error_copy = NULL;

	fu_device_event_set_str(event1, "Name", "Richard");
	fu_device_event_set_i64(event1, "Age", 123);
	fu_device_event_set_bytes(event1, "Blob", blob1);
	fu_device_event_set_data(event1, "Data", NULL, 0);

	/* no event set */
	ret = fu_device_event_check_error(event1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	json = fwupd_codec_to_json_string(FWUPD_CODEC(event1), FWUPD_CODEC_FLAG_COMPRESSED, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(json,
			==,
			"{\n"
			"  \"Id\": \"#f9f98a90\",\n"
			"  \"Name\": \"Richard\",\n"
			"  \"Age\": 123,\n"
			"  \"Blob\": \"aGVsbG8A\",\n"
			"  \"Data\": \"\"\n"
			"}");

	ret = fwupd_codec_from_json_string(FWUPD_CODEC(event2), json, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_event_get_id(event2), ==, "#f9f98a90");
	g_assert_cmpint(fu_device_event_get_i64(event2, "Age", NULL), ==, 123);
	g_assert_cmpstr(fu_device_event_get_str(event2, "Name", NULL), ==, "Richard");
	blob2 = fu_device_event_get_bytes(event2, "Blob", &error);
	g_assert_nonnull(blob2);
	g_assert_cmpstr(g_bytes_get_data(blob2, NULL), ==, "hello");
	blob3 = fu_device_event_get_bytes(event2, "Data", &error);
	g_assert_nonnull(blob3);
	g_assert_cmpstr(g_bytes_get_data(blob3, NULL), ==, NULL);

	/* invalid type */
	str = fu_device_event_get_str(event2, "Age", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(str);

	/* set error */
	fu_device_event_set_error(event2, error);
	ret = fu_device_event_check_error(event2, &error_copy);
	g_assert_error(error_copy, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_cmpstr(error_copy->message, ==, "invalid event type for key Age");
	g_assert_false(ret);
}

static void
fu_device_event_uncompressed_func(void)
{
	g_autofree gchar *json = NULL;
	g_autoptr(FuDeviceEvent) event = fu_device_event_new("foo:bar:baz");
	g_autoptr(GError) error = NULL;

	/* uncompressed */
	fu_device_event_set_str(event, "Name", "Richard");
	json = fwupd_codec_to_json_string(FWUPD_CODEC(event), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(json,
			==,
			"{\n"
			"  \"Id\": \"foo:bar:baz\",\n"
			"  \"Name\": \"Richard\"\n"
			"}");
}

static void
fu_device_event_strict_order_func(void)
{
	FuDeviceEvent *event_tmp;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDeviceEvent) event1 = fu_device_event_new("foo:bar:baz");
	g_autoptr(FuDeviceEvent) event2 = fu_device_event_new("aaa:bbb:ccc");
	g_autoptr(FuDeviceEvent) event3 = fu_device_event_new("www:yyy:zzz");
	g_autoptr(FuDeviceEvent) event4 = fu_device_event_new("ddd:eee:fff");
	g_autoptr(FuDeviceEvent) event5 = fu_device_event_new("mmm.nnn.ooo");
	g_autoptr(GError) error = NULL;

	fu_device_add_event(device, event1);
	fu_device_add_event(device, event2);
	fu_device_add_event(device, event3);
	fu_device_add_event(device, event4);
	fu_device_add_event(device, event5);

	/* allows skipping first event */
	event_tmp = fu_device_load_event(device, "aaa:bbb:ccc", &error);
	g_assert_no_error(error);
	g_assert_nonnull(event_tmp);

	/* only accept strict order from now on */
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_STRICT_EMULATION_ORDER);
	event_tmp = fu_device_load_event(device, "www:yyy:zzz", &error);
	g_assert_no_error(error);
	g_assert_nonnull(event_tmp);
	event_tmp = fu_device_load_event(device, "mmm.nnn.ooo", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(event_tmp);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/device-event", fu_device_event_func);
	g_test_add_func("/fwupd/device-event/uncompressed", fu_device_event_uncompressed_func);
	g_test_add_func("/fwupd/device-event/donor", fu_device_event_donor_func);
	g_test_add_func("/fwupd/device-event/strict-order", fu_device_event_strict_order_func);
	return g_test_run();
}
