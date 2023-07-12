/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-hid-report.h"

#define FU_TYPE_HID_DESCRIPTOR (fu_hid_descriptor_get_type())
G_DECLARE_DERIVABLE_TYPE(FuHidDescriptor, fu_hid_descriptor, FU, HID_DESCRIPTOR, FuFirmware)

struct _FuHidDescriptorClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_hid_descriptor_new(void);

FuHidReport *
fu_hid_descriptor_find_report_by_id(FuHidDescriptor *self,
				    guint32 usage_page,
				    guint32 report_id,
				    GError **error);
FuHidReport *
fu_hid_descriptor_find_report_by_usage(FuHidDescriptor *self,
				       guint32 usage_page,
				       guint32 usage,
				       GError **error);
