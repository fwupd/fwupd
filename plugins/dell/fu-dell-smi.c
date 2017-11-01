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

#include "config.h"

#include <appstream-glib.h>
#include "fu-dell-smi.h"
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>

/* These are for dock query capabilities */
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

static void
_dell_smi_obj_free (FuDellSmiObj *obj)
{
#ifdef HAVE_LIBSMBIOS
	dell_smi_obj_free (obj->smi);
#endif
	g_free(obj->buffer);
	g_free(obj);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FuDellSmiObj, _dell_smi_obj_free);

/* don't actually clear if we're testing */
gboolean
fu_dell_clear_smi (FuDellSmiObj *obj)
{
	if (obj->fake_smbios)
		return TRUE;

	for (gint i=0; i < 4; i++) {
		obj->buffer->std.input[i] = 0;
		obj->buffer->std.output[i] = 0;
	}
	return TRUE;
}

gboolean
fu_dell_execute_smi (FuDellSmiObj *obj)
{
	gint ret = -1;
	gint fd;

	if (obj->fake_smbios)
		return TRUE;
	else if (obj->wmi_smbios != NULL) {
		fd = g_open (obj->wmi_smbios, O_NONBLOCK);
		ret = ioctl (fd, DELL_WMI_SMBIOS_CMD, obj->buffer);
		close(fd);
	}
#ifdef HAVE_LIBSMBIOS
	else {
		dell_smi_obj_set_class (obj->smi, obj->buffer->std.class);
		dell_smi_obj_set_select (obj->smi, obj->buffer->std.select);
		if ((obj->buffer->ext.argattrib & 1) == 0)
			dell_smi_obj_set_arg (obj->smi, cbARG1,
				obj->buffer->std.input[0]);
		if ((obj->buffer->ext.argattrib & (1 << 8)) == 0)
			dell_smi_obj_set_arg (obj->smi, cbARG2,
				obj->buffer->std.input[1]);
		if ((obj->buffer->ext.argattrib & (1 << 16)) == 0)
			dell_smi_obj_set_arg (obj->smi, cbARG3,
				obj->buffer->std.input[2]);
		if ((obj->buffer->ext.argattrib & (1 << 24)) == 0)
			dell_smi_obj_set_arg (obj->smi, cbARG4,
				obj->buffer->std.input[3]);
		ret = dell_smi_obj_execute (obj->smi);
	}
#endif
	if (ret != 0)
		g_debug ("failed to run query %u/%u: %i",
			obj->buffer->std.class, obj->buffer->std.select, ret);
	return ret == 0 ? TRUE : FALSE;
}

guint32
fu_dell_get_res (FuDellSmiObj *obj, guint8 arg)
{
	if (obj->fake_smbios || obj->wmi_smbios != NULL)
		return obj->buffer->std.output[arg];
#ifdef HAVE_LIBSMBIOS
	else
		return dell_smi_obj_get_res (obj->smi, arg);
#endif
	return 0;
}

gboolean
fu_dell_detect_dock (FuDellSmiObj *obj, guint32 *location)
{
	struct dock_count_out *count_out;

	/* look up dock count */
	count_out  = (struct dock_count_out *) obj->buffer->std.output;
	if (!fu_dell_clear_smi (obj)) {
		g_debug ("failed to clear SMI buffers");
		return FALSE;
	}
	obj->buffer->std.class = DACI_DOCK_CLASS;
	obj->buffer->std.select = DACI_DOCK_SELECT;
	obj->buffer->std.input[0] = DACI_DOCK_ARG_COUNT;
	if (!fu_dell_execute_smi (obj))
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
	if (location != NULL)
		*location = count_out->location;
	return TRUE;
}

gboolean
fu_dell_query_dock (FuDellSmiObj *obj, DOCK_UNION *buf)
{
	const char *bufpat = "DSCI";
	guint32 location;
	gint result;

	if (!fu_dell_detect_dock (obj, &location))
		return FALSE;

	if (!fu_dell_clear_smi (obj)) {
		g_debug ("failed to clear SMI buffers");
		return FALSE;
	}

	obj->buffer->std.class = DACI_DOCK_CLASS;
	obj->buffer->std.select = DACI_DOCK_SELECT;
	obj->buffer->std.input[0] = DACI_DOCK_ARG_INFO;
	obj->buffer->std.input[1] = location;
	/* size of the returned buffer */
	obj->buffer->ext.blength = sizeof (DOCK_INFO_RECORD);
	/* cbARG3 is a buffer */
	obj->buffer->ext.argattrib = 1 << 16;

	/* look up more information on dock */
	if (obj->fake_smbios)
		buf->buf = obj->fake_buffer;
	else if (obj->wmi_smbios != NULL) {
		buf->buf = obj->buffer->ext.data;
		/* our return buffer will start at offset 40 - buffer length*/
		obj->buffer->std.input[2] = 40;
		/* write out the requisite pattern */
		for (guint i=0; i < obj->buffer->ext.blength; i++)
			obj->buffer->ext.data[i] = bufpat[i%4];
	}
#ifdef HAVE_LIBSMBIOS
	else {
		/* create the buffer */
		buf->buf = dell_smi_obj_make_buffer_frombios_auto (obj->smi,
								  cbARG3,
								  obj->buffer->ext.blength);
		if (!buf->buf) {
			g_debug ("Failed to initialize buffer");
			return FALSE;
		}
	}
#endif
	if (!fu_dell_execute_smi (obj))
		return FALSE;
	result = fu_dell_get_res (obj, cbARG1);
	if (result != SMI_SUCCESS) {
		if (result == SMI_INVALID_BUFFER) {
			g_debug ("Invalid buffer size, needed %" G_GUINT32_FORMAT,
				 fu_dell_get_res (obj, cbARG2));
		} else {
			g_debug ("SMI execution returned error: %d",
				 result);
		}
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_dell_toggle_dock_mode (FuDellSmiObj *obj, guint32 new_mode,
			  guint32 dock_location, GError **error)
{
	if (!fu_dell_clear_smi (obj)) {
		g_debug ("failed to clear SMI buffers");
		return FALSE;
	}

	/* Put into mode to accept AR/MST */
	obj->buffer->std.class = DACI_DOCK_CLASS;
	obj->buffer->std.select = DACI_DOCK_SELECT;
	obj->buffer->std.input[0] = DACI_DOCK_ARG_MODE;
	obj->buffer->std.input[1] = dock_location;
	obj->buffer->std.input[2] = new_mode;

	if (!fu_dell_execute_smi (obj))
		return FALSE;
	if (obj->buffer->std.output[1] != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "Failed to set dock flash mode: %u",
			     obj->buffer->std.output[1]);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_dell_toggle_host_mode (FuDellSmiObj *obj, const efi_guid_t guid, int mode)
{
	gint ret;
	ADDR_UNION buf;

	if (!fu_dell_clear_smi (obj)) {
		g_debug ("failed to clear SMI buffers");
		return FALSE;
	}

	obj->buffer->std.class = DACI_FLASH_INTERFACE_CLASS;
	obj->buffer->std.select = DACI_FLASH_INTERFACE_SELECT;
	obj->buffer->std.input[0] = DACI_FLASH_ARG_FLASH_MODE;
	obj->buffer->std.input[3] = mode;
	obj->buffer->ext.argattrib = 1 << 8;
	/* needs to be padded with an empty GUID */
	obj->buffer->ext.blength = sizeof(efi_guid_t) * 2;

	/* cbARG2 is a buffer */
	if (obj->wmi_smbios != NULL) {
		buf.buf = obj->buffer->ext.data;
		/* our return buffer will start at offset 40 - buffer length */
		obj->buffer->std.input[1] = 40;
		/* clear the return area */
		memset(obj->buffer->ext.data, 0, obj->buffer->ext.blength);
	}
#ifdef HAVE_LIBSMBIOS
	else {
		buf.buf =
			dell_smi_obj_make_buffer_frombios_withoutheader(obj->smi,
									cbARG2,
									obj->buffer->ext.blength);
		if (!buf.buf) {
			g_debug ("Failed to initialize SMI buffer");
			return FALSE;
		}
	}
#endif
	/* set the GUID */
	*buf.guid = guid;
	if (!fu_dell_execute_smi (obj))
		return FALSE;
	ret = fu_dell_get_res (obj, cbARG1);
	if (ret != SMI_SUCCESS){
		g_debug ("failed to execute SMI: %d", ret);
		return FALSE;
	}
	return TRUE;
}
