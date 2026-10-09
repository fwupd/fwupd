/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-cfu-firmware.h"
#include "fu-cfu-module.h"
#include "fu-context-private.h"

static void
fu_cfu_firmware_func(void)
{
	g_autoptr(FuFirmware) firmware = fu_cfu_firmware_new();
	g_assert_nonnull(firmware);
	g_assert_true(FU_IS_FIRMWARE(firmware));
}

static void
fu_cfu_module_setup_func(void)
{
	gboolean ret;
	/* fw_version=0x01020304, flags=0x03 (bank=3), component_id=0x42 */
	const guint8 buf[] = {0x04, 0x03, 0x02, 0x01, 0x03, 0x42, 0x00, 0x00};
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_NO_CACHE);
	g_autoptr(FuCfuModule) module = g_object_new(FU_TYPE_CFU_MODULE, "context", ctx, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_add_instance_str(FU_DEVICE(module), "VID", "1234");
	fu_device_add_instance_str(FU_DEVICE(module), "PID", "5678");

	ret = fu_cfu_module_setup(module, buf, sizeof(buf), 0x0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_cfu_module_get_component_id(module), ==, 0x42);
	g_assert_cmpint(fu_device_get_version_raw(FU_DEVICE(module)), ==, 0x01020304);
	g_assert_cmpstr(fu_device_get_logical_id(FU_DEVICE(module)), ==, "CID:0x42,BANK:0x03");

	/* truncated buffer must fail */
	ret = fu_cfu_module_setup(module, buf, 0x2, 0x0, &error);
	g_assert_nonnull(error);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/cfu/firmware", fu_cfu_firmware_func);
	g_test_add_func("/cfu/module/setup", fu_cfu_module_setup_func);
	return g_test_run();
}
