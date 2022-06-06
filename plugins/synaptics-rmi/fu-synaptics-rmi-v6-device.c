/*
 * Copyright (C) 2012 Andrew Duggan
 * Copyright (C) 2012 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-synaptics-rmi-v6-device.h"

#define RMI_F34_CONFIG_BLOCKS_OFFSET 2

gboolean
fu_synaptics_rmi_v6_device_setup(FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	FuSynapticsRmiFunction *f34;
	g_autoptr(GByteArray) f34_data0 = NULL;
	g_autoptr(GByteArray) f34_data2 = NULL;
	g_autoptr(GByteArray) f34_data3 = NULL;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* get bootloader ID */
	f34_data0 = fu_synaptics_rmi_device_read(self, f34->query_base, 0x2, error);
	if (f34_data0 == NULL) {
		g_prefix_error(error, "failed to read bootloader ID: ");
		return FALSE;
	}
	if (!fu_memread_uint8_safe(f34_data0->data,
				   f34_data0->len,
				   0x0,
				   &flash->bootloader_id[0],
				   error))
		return FALSE;
	if (!fu_memread_uint8_safe(f34_data0->data,
				   f34_data0->len,
				   0x1,
				   &flash->bootloader_id[1],
				   error))
		return FALSE;

	/* get flash properties */
	f34_data2 = fu_synaptics_rmi_device_read(self, f34->query_base + 0x02, 2, error);
	if (f34_data2 == NULL)
		return FALSE;

	if (!fu_memread_uint16_safe(f34_data2->data,
				    f34_data2->len,
				    0x0,
				    &flash->block_size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	f34_data3 = fu_synaptics_rmi_device_read(self, f34->query_base + 0x03, 8, error);
	if (f34_data3 == NULL)
		return FALSE;
	if (!fu_memread_uint16_safe(f34_data3->data,
				    f34_data3->len,
				    0x0,
				    &flash->block_count_fw,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(f34_data3->data,
				    f34_data3->len,
				    RMI_F34_CONFIG_BLOCKS_OFFSET,
				    &flash->block_count_cfg,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	flash->status_addr = f34->data_base + 2;
	return TRUE;
}
