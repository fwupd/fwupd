/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuHidDevice"

#include "config.h"

#include "fu-hid-report.h"

/**
 * FuHidReport:
 *
 * See also: [class@FuHidDescriptor]
 */

struct _FuHidReport {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuHidReport, fu_hid_report, FU_TYPE_FIRMWARE)

static void
fu_hid_report_init(FuHidReport *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_DEDUPE_IDX);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
	fu_firmware_set_images_max(FU_FIRMWARE(self), G_MAXUINT8);
}

static void
fu_hid_report_class_init(FuHidReportClass *klass)
{
}

/**
 * fu_hid_report_new:
 *
 * Creates a new HID report item
 *
 * Returns: (transfer full): a #FuHidReport
 *
 * Since: 1.9.4
 **/
FuHidReport *
fu_hid_report_new(void)
{
	return g_object_new(FU_TYPE_HID_REPORT, NULL);
}
