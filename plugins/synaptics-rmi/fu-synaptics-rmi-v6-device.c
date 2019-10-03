/*
 * Copyright (C) 2012-2014 Andrew Duggan
 * Copyright (C) 2012-2019 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-synaptics-rmi-v6-device.h"

#include "fwupd-error.h"

#define RMI_F34_CONFIG_BLOCKS_OFFSET			2

gboolean
fu_synaptics_rmi_v6_device_setup (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash (self);
	FuSynapticsRmiFunction *f34;
	g_autoptr(GByteArray) f34_data0 = NULL;
	g_autoptr(GByteArray) f34_data2 = NULL;
	g_autoptr(GByteArray) f34_data3 = NULL;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* get bootloader ID */
	f34_data0 = fu_synaptics_rmi_device_read (self, f34->query_base, 0x2, error);
	if (f34_data0 == NULL) {
		g_prefix_error (error, "failed to read bootloader ID: ");
		return FALSE;
	}
	flash->bootloader_id[0] = f34_data0->data[0];
	flash->bootloader_id[1] = f34_data0->data[1];

	/* get flash properties */
	f34_data2 = fu_synaptics_rmi_device_read (self, f34->query_base + 0x02, 2, error);
	if (f34_data2 == NULL)
		return FALSE;
	flash->block_size = fu_common_read_uint16 (f34_data2->data, G_LITTLE_ENDIAN);
	f34_data3 = fu_synaptics_rmi_device_read (self, f34->query_base + 0x03, 8, error);
	if (f34_data3 == NULL)
		return FALSE;
	flash->block_count_fw = fu_common_read_uint16 (f34_data3->data, G_LITTLE_ENDIAN);
	flash->block_count_cfg = fu_common_read_uint16 (f34_data3->data + RMI_F34_CONFIG_BLOCKS_OFFSET, G_LITTLE_ENDIAN);
	flash->status_addr = f34->data_base + 2;
	return TRUE;
}
