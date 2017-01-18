/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Mario Limonciello <mario_limonciello@dell.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <appstream-glib.h>
#include <smbios_c/smi.h>
#include <smbios_c/obj/smi.h>
#include "fu-dell-flash.h"
#include "fu-plugin.h"

/* These are for dock query capabilities */
struct dock_count_in {
	guint32 argument;
	guint32 reserved1;
	guint32 reserved2;
	guint32 reserved3;
};

struct dock_count_out {
	guint32 ret;
	guint32 count;
	guint32 location;
	guint32 reserved;
};

/* This is used for host flash GUIDs */
typedef union _ADDR_UNION{
	uint8_t *buf;
	efi_guid_t *guid;
} ADDR_UNION;
#pragma pack()

/* supported host related GUIDs */
#define TBT_GPIO_GUID		EFI_GUID (0x2EFD333F, 0x65EC, 0x41D3, 0x86D3, 0x08, 0xF0, 0x9F, 0x4F, 0xB1, 0x14)
#define MST_GPIO_GUID		EFI_GUID (0xF24F9bE4, 0x2a13, 0x4344, 0xBC05, 0x01, 0xCE, 0xF7, 0xDA, 0xEF, 0x92)

static void
_dell_smi_obj_free (fu_dell_smi_obj *smi)
{
	dell_smi_obj_free (smi);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(fu_dell_smi_obj, _dell_smi_obj_free);

gboolean
fu_dell_execute_simple_smi (FuPlugin *plugin,
			    guint16 class, guint16 select,
			    guint32  *args, guint32 *out)
{
	FuPluginData *data;

	if (plugin != NULL) {
		data = fu_plugin_get_data (plugin);
		if (data->fake_smbios) {
			for (guint i = 0; i < 4; i++)
				out[i] = data->fake_output[i];
			return TRUE;
		}
	}
	if (dell_simple_ci_smi (class, select, args, out)) {
		g_debug ("Dell: failed to run query %u/%u", class, select);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_dell_detect_dock (FuPlugin *plugin, guint32 *location)
{
	g_autofree struct dock_count_in *count_args = NULL;
	g_autofree struct dock_count_out *count_out = NULL;

	/* look up dock count */
	count_args = g_malloc0 (sizeof(struct dock_count_in));
	count_out  = g_malloc0 (sizeof(struct dock_count_out));
	count_args->argument = DACI_DOCK_ARG_COUNT;
	if (!fu_dell_execute_simple_smi (plugin,
					 DACI_DOCK_CLASS,
					 DACI_DOCK_SELECT,
					 (guint32 *) count_args,
					 (guint32 *) count_out))
		return FALSE;
	if (count_out->ret != 0) {
		g_debug ("Dell: Failed to query system for dock count: "
			 "(%" G_GUINT32_FORMAT ")", count_out->ret);
		return FALSE;
	}
	if (count_out->count < 1) {
		g_debug ("Dell: no dock plugged in");
		return FALSE;
	}
	*location = count_out->location;
	g_debug ("Dell: Dock count %u, location %u.",
		 count_out->count, *location);
	return TRUE;
}

static gboolean
fu_dell_toggle_dock_mode (guint32 new_mode, guint32 dock_location,
				 GError **error)
{
	g_autofree guint32 *input = NULL;
	g_autofree guint32 *output = NULL;

	input = g_malloc0 (sizeof(guint32) *4);
	output = g_malloc0 (sizeof(guint32) *4);

	/* Put into mode to accept AR/MST */
	input[0] = DACI_DOCK_ARG_MODE;
	input[1] = dock_location;
	input[2] = new_mode;
	if (!fu_dell_execute_simple_smi(NULL,
					DACI_DOCK_CLASS,
					DACI_DOCK_SELECT,
					input,
					output))
		return FALSE;
	if (output[1] != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Dell: Failed to set dock flash mode: %u",
			     output[1]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_toggle_host_mode (const efi_guid_t guid, int mode)
{
	gint ret;
	g_autoptr(fu_dell_smi_obj) smi = NULL;
	ADDR_UNION buf;
	smi = dell_smi_factory (DELL_SMI_DEFAULTS);
	dell_smi_obj_set_class (smi, DACI_FLASH_INTERFACE_CLASS);
	dell_smi_obj_set_select (smi, DACI_FLASH_INTERFACE_SELECT);
	dell_smi_obj_set_arg (smi, cbARG1, DACI_FLASH_ARG_FLASH_MODE);
	dell_smi_obj_set_arg (smi, cbARG4, mode);
	/* needs to be padded with an empty GUID */
	buf.buf = dell_smi_obj_make_buffer_frombios_withoutheader(smi, cbARG2,
								  sizeof(efi_guid_t) * 2);
	if (!buf.buf) {
		g_debug ("Dell: Failed to initialize SMI buffer");
		return FALSE;
	}
	*buf.guid = guid;
	ret = dell_smi_obj_execute(smi);
	if (ret != SMI_SUCCESS){
		g_debug ("Dell: failed to execute SMI: %d", ret);
		return FALSE;
	}

	ret = dell_smi_obj_get_res(smi, cbRES1);
	if (ret != SMI_SUCCESS) {
		g_debug ("Dell: SMI execution returned error: %d", ret);
		return FALSE;
	}
	return TRUE;
}


gboolean
fu_dell_toggle_flash (FuDevice *device, GError **error, gboolean enable)
{
	guint32 dock_location;
	FwupdDeviceFlags flags;
	const gchar *tmp;

	if (device) {
		flags = fu_device_get_flags (device);
		if (!(flags & FWUPD_DEVICE_FLAG_ALLOW_ONLINE))
			return TRUE;
		tmp = fu_device_get_plugin(device);
		if (!((g_strcmp0 (tmp, "thunderbolt") == 0) ||
			(g_strcmp0 (tmp, "synapticsmst") == 0)))
			return TRUE;
		g_debug("Dell: preparing/cleaning update for %s", tmp);
	}

	/* Dock MST Hub / TBT Controller */
	if (fu_dell_detect_dock (NULL, &dock_location)) {
		if (!fu_dell_toggle_dock_mode (enable, dock_location, error))
			g_debug("Dell: unable to change dock to %d", enable);
		else
			g_debug("Dell: Toggled dock mode to %d", enable);
	}

	/* System MST hub / TBT controller */
	if (!fu_dell_toggle_host_mode (TBT_GPIO_GUID, enable))
		g_debug("Dell: Unable to toggle TBT GPIO to %d", enable);
	else
		g_debug("Dell: Toggled TBT GPIO to %d", enable);
	if (!fu_dell_toggle_host_mode (MST_GPIO_GUID, enable))
		g_debug("Dell: Unable to toggle MST hub GPIO to %d", enable);
	else
		g_debug("Dell: Toggled MST hub GPIO to %d", enable);

	return TRUE;
}
