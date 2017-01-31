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
#include "fu-dell-common.h"

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
_dell_smi_obj_free (FuDellSmiObj *obj)
{
	dell_smi_obj_free (obj->smi);
	g_free(obj);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FuDellSmiObj, _dell_smi_obj_free);

gboolean
fu_dell_supported (void)
{
	guint8 dell_supported = 0;
	struct smbios_struct *de_table;

	de_table = smbios_get_next_struct_by_type (0, 0xDE);
	smbios_struct_get_data (de_table, &(dell_supported), 0x00, sizeof(guint8));
	if (dell_supported != 0xDE)
		return FALSE;
	return TRUE;
}

/* don't actually clear if we're testing */
gboolean
fu_dell_clear_smi (FuDellSmiObj *obj)
{
	if (obj->fake_smbios)
		return TRUE;

	for (gint i=0; i < 4; i++) {
		obj->input[i] = 0;
		obj->output[i] = 0;
	}
	return TRUE;
}


gboolean
fu_dell_execute_smi (FuDellSmiObj *obj)
{
	gint ret;

	if (obj->fake_smbios)
		return TRUE;

	ret = dell_smi_obj_execute (obj->smi);
	if (ret != 0) {
		g_debug ("SMI execution failed: %i", ret);
		return FALSE;
	}
	return TRUE;
}

guint32
fu_dell_get_res (FuDellSmiObj *smi_obj, guint8 arg)
{
	if (smi_obj->fake_smbios)
		return smi_obj->output[arg];

	return dell_smi_obj_get_res (smi_obj->smi, arg);
}

gboolean
fu_dell_execute_simple_smi (FuDellSmiObj *obj, guint16 class, guint16 select)
{
	/* test suite will mean don't actually call */
	if (obj->fake_smbios)
		return TRUE;

	if (dell_simple_ci_smi (class,
				select,
				obj->input,
				obj->output)) {
		g_debug ("failed to run query %u/%u",
			 class,
			 select);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_dell_detect_dock (FuDellSmiObj *smi_obj, guint32 *location)
{
	struct dock_count_in *count_args;
	struct dock_count_out *count_out;

	/* look up dock count */
	count_args = (struct dock_count_in *) smi_obj->input;
	count_out  = (struct dock_count_out *) smi_obj->output;
	if (!fu_dell_clear_smi (smi_obj)) {
		g_debug ("failed to clear SMI buffers");
		return FALSE;
	}
	count_args->argument = DACI_DOCK_ARG_COUNT;

	if (!fu_dell_execute_simple_smi (smi_obj,
					 DACI_DOCK_CLASS,
					 DACI_DOCK_SELECT))
		return FALSE;
	if (count_out->ret != 0) {
		g_debug ("Failed to query system for dock count: "
			 "(%" G_GUINT32_FORMAT ")", count_out->ret);
		return FALSE;
	}
	if (count_out->count < 1) {
		g_debug ("no dock plugged in");
		return FALSE;
	}
	*location = count_out->location;
	g_debug ("Dock count %u, location %u.",
		 count_out->count, *location);
	return TRUE;
}

gboolean
fu_dell_query_dock (FuDellSmiObj *smi_obj, DOCK_UNION *buf)
{
	gint result;
	guint32 location;
	guint buf_size;

	if (!fu_dell_detect_dock (smi_obj, &location))
		return FALSE;

	fu_dell_clear_smi (smi_obj);

	/* look up more information on dock */
	if (smi_obj->fake_smbios)
		buf->buf = smi_obj->fake_buffer;
	else {
		dell_smi_obj_set_class (smi_obj->smi, DACI_DOCK_CLASS);
		dell_smi_obj_set_select (smi_obj->smi, DACI_DOCK_SELECT);
		dell_smi_obj_set_arg (smi_obj->smi, cbARG1, DACI_DOCK_ARG_INFO);
		dell_smi_obj_set_arg (smi_obj->smi, cbARG2, location);
		buf_size = sizeof (DOCK_INFO_RECORD);
		buf->buf = dell_smi_obj_make_buffer_frombios_auto (smi_obj->smi,
								  cbARG3,
								  buf_size);
		if (!buf->buf) {
			g_debug ("Failed to initialize buffer");
			return FALSE;
		}
	}
	if (!fu_dell_execute_smi (smi_obj))
		return FALSE;
	result = fu_dell_get_res (smi_obj, cbARG1);
	if (result != SMI_SUCCESS) {
		if (result == SMI_INVALID_BUFFER) {
			g_debug ("Invalid buffer size, needed %" G_GUINT32_FORMAT,
				 fu_dell_get_res (smi_obj, cbARG2));
		} else {
			g_debug ("SMI execution returned error: %d",
				 result);
		}
		return FALSE;
	}
	return TRUE;
}

const gchar*
fu_dell_get_dock_type (guint8 type)
{
	g_autoptr (FuDellSmiObj) smi_obj = NULL;
	DOCK_UNION buf;

	/* not yet initialized, look it up */
	if (type == DOCK_TYPE_NONE) {
		smi_obj = g_malloc0 (sizeof(FuDellSmiObj));
		smi_obj->smi = dell_smi_factory (DELL_SMI_DEFAULTS);
		if (!fu_dell_query_dock (smi_obj, &buf))
			return NULL;
		type = buf.record->dock_info_header.dock_type;
	}

	switch (type) {
	case DOCK_TYPE_TB16:
		return "TB16";
	case DOCK_TYPE_WD15:
		return "WD15";
	default:
		g_debug ("Dock type %d unknown",
			 type);
	}

	return NULL;
}

guint32
fu_dell_get_cable_type (guint8 type)
{
	g_autoptr (FuDellSmiObj) smi_obj = NULL;
	DOCK_UNION buf;

	/* not yet initialized, look it up */
	if (type == CABLE_TYPE_NONE) {
		smi_obj = g_malloc0 (sizeof(FuDellSmiObj));
		smi_obj->smi = dell_smi_factory (DELL_SMI_DEFAULTS);
		if (!fu_dell_query_dock (smi_obj, &buf))
			return 0;
		type = (buf.record->dock_info).cable_type;
	}
	return type;
}

static gboolean
fu_dell_toggle_dock_mode (FuDellSmiObj *smi_obj, guint32 new_mode,
			  guint32 dock_location, GError **error)
{
	/* Put into mode to accept AR/MST */
	fu_dell_clear_smi (smi_obj);
	smi_obj->input[0] = DACI_DOCK_ARG_MODE;
	smi_obj->input[1] = dock_location;
	smi_obj->input[2] = new_mode;

	if (!fu_dell_execute_simple_smi (smi_obj,
					 DACI_DOCK_CLASS,
					 DACI_DOCK_SELECT))
		return FALSE;
	if (smi_obj->output[1] != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Failed to set dock flash mode: %u",
			     smi_obj->output[1]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_toggle_host_mode (FuDellSmiObj *smi_obj, const efi_guid_t guid, int mode)
{
	gint ret;
	ADDR_UNION buf;

	dell_smi_obj_set_class (smi_obj->smi, DACI_FLASH_INTERFACE_CLASS);
	dell_smi_obj_set_select (smi_obj->smi, DACI_FLASH_INTERFACE_SELECT);
	dell_smi_obj_set_arg (smi_obj->smi, cbARG1, DACI_FLASH_ARG_FLASH_MODE);
	dell_smi_obj_set_arg (smi_obj->smi, cbARG4, mode);
	/* needs to be padded with an empty GUID */
	buf.buf = dell_smi_obj_make_buffer_frombios_withoutheader(smi_obj->smi,
								  cbARG2,
								  sizeof(efi_guid_t) * 2);
	if (!buf.buf) {
		g_debug ("Failed to initialize SMI buffer");
		return FALSE;
	}
	*buf.guid = guid;
	ret = dell_smi_obj_execute(smi_obj->smi);
	if (ret != SMI_SUCCESS){
		g_debug ("failed to execute SMI: %d", ret);
		return FALSE;
	}

	ret = dell_smi_obj_get_res(smi_obj->smi, cbRES1);
	if (ret != SMI_SUCCESS) {
		g_debug ("SMI execution returned error: %d", ret);
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
	g_autoptr (FuDellSmiObj) smi_obj = NULL;

	if (device) {
		flags = fu_device_get_flags (device);
		if (!(flags & FWUPD_DEVICE_FLAG_ALLOW_ONLINE))
			return TRUE;
		tmp = fu_device_get_plugin(device);
		if (!((g_strcmp0 (tmp, "thunderbolt") == 0) ||
			(g_strcmp0 (tmp, "synapticsmst") == 0)))
			return TRUE;
		g_debug("preparing/cleaning update for %s", tmp);
	}

	/* Dock MST Hub / TBT Controller */
	smi_obj = g_malloc0 (sizeof(FuDellSmiObj));
	smi_obj->smi = dell_smi_factory (DELL_SMI_DEFAULTS);

	if (fu_dell_detect_dock (smi_obj, &dock_location)) {
		if (!fu_dell_toggle_dock_mode (smi_obj, enable, dock_location,
					       error))
			g_debug("unable to change dock to %d", enable);
		else
			g_debug("Toggled dock mode to %d", enable);
	}

	/* System MST hub / TBT controller */
	if (!fu_dell_toggle_host_mode (smi_obj, TBT_GPIO_GUID, enable))
		g_debug("Unable to toggle TBT GPIO to %d", enable);
	else
		g_debug("Toggled TBT GPIO to %d", enable);
	if (!fu_dell_toggle_host_mode (smi_obj, MST_GPIO_GUID, enable))
		g_debug("Unable to toggle MST hub GPIO to %d", enable);
	else
		g_debug("Toggled MST hub GPIO to %d", enable);

	return TRUE;
}
