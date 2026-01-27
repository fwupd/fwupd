/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-codec.h"
#include "fwupd-request.h"
#include "fwupd-test.h"

static void
fwupd_request_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdRequest) request = fwupd_request_new();
	g_autoptr(FwupdRequest) request2 = fwupd_request_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) data = NULL;

	/* create dummy */
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_DO_NOT_POWER_OFF);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	g_assert_cmpstr(fwupd_request_get_message(request),
			==,
			"Do not turn off your computer or remove the AC adaptor.");
	fwupd_request_set_message(request, "foo");
	fwupd_request_set_image(request, "bar");
	fwupd_request_set_device_id(request, "950da62d4c753a26e64f7f7d687104ce38e32ca5");
	str = fwupd_codec_to_string(FWUPD_CODEC(request));
	g_debug("%s", str);

	g_assert_true(fwupd_request_has_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE));
	g_assert_cmpint(fwupd_request_get_flags(request),
			==,
			FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fwupd_request_remove_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	g_assert_false(fwupd_request_has_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE));
	g_assert_cmpint(fwupd_request_get_flags(request), ==, FWUPD_REQUEST_FLAG_NONE);

	/* set in init */
	g_assert_cmpint(fwupd_request_get_created(request), >, 0);

	/* to serialized and back again */
	data = fwupd_codec_to_variant(FWUPD_CODEC(request), FWUPD_CODEC_FLAG_NONE);
	ret = fwupd_codec_from_variant(FWUPD_CODEC(request2), data, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fwupd_request_get_kind(request2), ==, FWUPD_REQUEST_KIND_IMMEDIATE);
	g_assert_cmpint(fwupd_request_get_created(request2), >, 0);
	g_assert_cmpstr(fwupd_request_get_id(request2), ==, FWUPD_REQUEST_ID_DO_NOT_POWER_OFF);
	g_assert_cmpstr(fwupd_request_get_message(request2), ==, "foo");
	g_assert_cmpstr(fwupd_request_get_image(request2), ==, "bar");
	g_assert_cmpstr(fwupd_request_get_device_id(request2),
			==,
			"950da62d4c753a26e64f7f7d687104ce38e32ca5");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/request", fwupd_request_func);
	return g_test_run();
}
