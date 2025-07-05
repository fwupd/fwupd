/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-vli-pd-common.h"

static void
fu_test_vli_pd_common_func(void)
{
	struct {
		guint32 fwver;
		FuVliDeviceKind device_kind;
	} map[] = {{0x00, FU_VLI_DEVICE_KIND_UNKNOWN}, {0x01, FU_VLI_DEVICE_KIND_VL100},
		   {0x02, FU_VLI_DEVICE_KIND_VL100},   {0x03, FU_VLI_DEVICE_KIND_VL100},
		   {0x04, FU_VLI_DEVICE_KIND_VL101},   {0x05, FU_VLI_DEVICE_KIND_VL101},
		   {0x06, FU_VLI_DEVICE_KIND_VL101},   {0x07, FU_VLI_DEVICE_KIND_VL102},
		   {0x08, FU_VLI_DEVICE_KIND_VL102},   {0x09, FU_VLI_DEVICE_KIND_VL103},
		   {0x0A, FU_VLI_DEVICE_KIND_VL103},   {0x0B, FU_VLI_DEVICE_KIND_VL104},
		   {0x0C, FU_VLI_DEVICE_KIND_VL105},   {0x0D, FU_VLI_DEVICE_KIND_VL106},
		   {0x0E, FU_VLI_DEVICE_KIND_VL107},   {0x0F, FU_VLI_DEVICE_KIND_UNKNOWN},
		   {0xA0, FU_VLI_DEVICE_KIND_UNKNOWN}, {0xA1, FU_VLI_DEVICE_KIND_VL108},
		   {0xA2, FU_VLI_DEVICE_KIND_VL109},   {0xA3, FU_VLI_DEVICE_KIND_UNKNOWN},
		   {0xA4, FU_VLI_DEVICE_KIND_UNKNOWN}, {0xB1, FU_VLI_DEVICE_KIND_VL108},
		   {0xB2, FU_VLI_DEVICE_KIND_VL109},   {0xB3, FU_VLI_DEVICE_KIND_UNKNOWN},
		   {0xB4, FU_VLI_DEVICE_KIND_UNKNOWN}, {0xFF, FU_VLI_DEVICE_KIND_UNKNOWN}};
	for (guint i = 0; map[i].fwver != 0xFF; i++) {
		g_debug("checking fwver 0x%02X", map[i].fwver);
		g_assert_cmpint(fu_vli_pd_common_guess_device_kind((guint32)map[i].fwver << 24),
				==,
				map[i].device_kind);
	}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_test_add_func("/vli/pd-common", fu_test_vli_pd_common_func);
	return g_test_run();
}
