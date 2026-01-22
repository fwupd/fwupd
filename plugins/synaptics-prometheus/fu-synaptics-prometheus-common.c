/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-synaptics-prometheus-common.h"
#include "fu-synaptics-prometheus-struct.h"

GByteArray *
fu_synaptics_prometheus_reply_new(gsize cmdlen)
{
	GByteArray *blob = g_byte_array_new();
	fu_byte_array_set_size(blob, cmdlen, 0x00);
	return blob;
}

gboolean
fu_synaptics_prometheus_error_from_status(guint16 status, GError **error)
{
	const gchar *msg = fu_synaptics_prometheus_result_to_string(status);
	const FuErrorMapEntry entries[] = {
	    {FU_SYNAPTICS_PROMETHEUS_RESULT_OK, FWUPD_ERROR_LAST, NULL},
	    {FU_SYNAPTICS_PROMETHEUS_RESULT_GEN_OPERATION_CANCELED, FWUPD_ERROR_INTERNAL, msg},
	    {FU_SYNAPTICS_PROMETHEUS_RESULT_GEN_BAD_PARAM, FWUPD_ERROR_INVALID_DATA, msg},
	    {FU_SYNAPTICS_PROMETHEUS_RESULT_GEN_NULL_POINTER, FWUPD_ERROR_INVALID_DATA, msg},
	    {FU_SYNAPTICS_PROMETHEUS_RESULT_GEN_UNEXPECTED_FORMAT, FWUPD_ERROR_INVALID_DATA, msg},
	    {FU_SYNAPTICS_PROMETHEUS_RESULT_GEN_TIMEOUT, FWUPD_ERROR_TIMED_OUT, msg},
	    {FU_SYNAPTICS_PROMETHEUS_RESULT_GEN_OBJECT_DOESNT_EXIST, FWUPD_ERROR_NOT_FOUND, msg},
	    {FU_SYNAPTICS_PROMETHEUS_RESULT_GEN_ERROR, FWUPD_ERROR_INTERNAL, msg},
	    {FU_SYNAPTICS_PROMETHEUS_RESULT_SENSOR_MALFUNCTIONED, FWUPD_ERROR_INTERNAL, msg},
	    {FU_SYNAPTICS_PROMETHEUS_RESULT_SYS_OUT_OF_MEMORY, FWUPD_ERROR_INTERNAL, msg},
	};
	return fu_error_map_entry_to_gerror(status, entries, G_N_ELEMENTS(entries), error);
}
