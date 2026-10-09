/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-engine-request.h"

static void
fu_engine_request_func(void)
{
	g_autoptr(FuEngineRequest) request = fu_engine_request_new("org.fwupd.sender");

	/* sender */
	g_assert_cmpstr(fu_engine_request_get_sender(request), ==, "org.fwupd.sender");

	/* flags */
	g_assert_false(fu_engine_request_has_flag(request, FU_ENGINE_REQUEST_FLAG_NO_REQUIREMENTS));
	fu_engine_request_add_flag(request, FU_ENGINE_REQUEST_FLAG_NO_REQUIREMENTS);
	g_assert_true(fu_engine_request_has_flag(request, FU_ENGINE_REQUEST_FLAG_NO_REQUIREMENTS));

	/* feature flags */
	fu_engine_request_set_feature_flags(request, FWUPD_FEATURE_FLAG_CAN_REPORT);
	g_assert_cmpint(fu_engine_request_get_feature_flags(request),
			==,
			FWUPD_FEATURE_FLAG_CAN_REPORT);
	g_assert_true(fu_engine_request_has_feature_flag(request, FWUPD_FEATURE_FLAG_CAN_REPORT));
	g_assert_false(
	    fu_engine_request_has_feature_flag(request, FWUPD_FEATURE_FLAG_SHOW_PROBLEMS));

	/* converter flags */
	fu_engine_request_set_converter_flags(request, FWUPD_CODEC_FLAG_TRUSTED);
	g_assert_cmpint(fu_engine_request_get_converter_flags(request),
			==,
			FWUPD_CODEC_FLAG_TRUSTED);
	g_assert_true(fu_engine_request_has_converter_flag(request, FWUPD_CODEC_FLAG_TRUSTED));

	/* locale */
	fu_engine_request_set_locale(request, "en_GB.UTF-8");
	g_assert_cmpstr(fu_engine_request_get_locale(request), ==, "en_GB");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/engine-request", fu_engine_request_func);
	return g_test_run();
}
