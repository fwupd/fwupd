/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_QC_FIREHOSE_USB_DEVICE (fu_qc_firehose_usb_device_get_type())
G_DECLARE_FINAL_TYPE(FuQcFirehoseUsbDevice,
		     fu_qc_firehose_usb_device,
		     FU,
		     QC_FIREHOSE_USB_DEVICE,
		     FuUsbDevice)
