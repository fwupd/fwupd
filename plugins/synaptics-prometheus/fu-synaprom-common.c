/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-synaprom-common.h"

enum {
	FU_SYNAPROM_RESULT_OK = 0,
	FU_SYNAPROM_RESULT_GEN_OPERATION_CANCELED = 103,
	FU_SYNAPROM_RESULT_GEN_INVALID = 110,
	FU_SYNAPROM_RESULT_GEN_BAD_PARAM = 111,
	FU_SYNAPROM_RESULT_GEN_NULL_POINTER = 112,
	FU_SYNAPROM_RESULT_GEN_UNEXPECTED_FORMAT = 114,
	FU_SYNAPROM_RESULT_GEN_TIMEOUT = 117,
	FU_SYNAPROM_RESULT_GEN_OBJECT_DOESNT_EXIST = 118,
	FU_SYNAPROM_RESULT_GEN_ERROR = 119,
	FU_SYNAPROM_RESULT_SENSOR_MALFUNCTIONED = 202,
	FU_SYNAPROM_RESULT_SYS_OUT_OF_MEMORY = 602,
};

GByteArray *
fu_synaprom_request_new(guint8 cmd, const guint8 *buf, gsize bufsz)
{
	GByteArray *blob = g_byte_array_new();
	fu_byte_array_append_uint8(blob, cmd);
	if (buf != NULL)
		g_byte_array_append(blob, buf, bufsz);
	return blob;
}

GByteArray *
fu_synaprom_reply_new(gsize cmdlen)
{
	GByteArray *blob = g_byte_array_new();
	fu_byte_array_set_size(blob, cmdlen, 0x00);
	return blob;
}

gboolean
fu_synaprom_error_from_status(guint16 status, GError **error)
{
	if (status == FU_SYNAPROM_RESULT_OK)
		return TRUE;
	switch (status) {
	case FU_SYNAPROM_RESULT_GEN_OPERATION_CANCELED:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "cancelled");
		break;
	case FU_SYNAPROM_RESULT_GEN_BAD_PARAM:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "bad parameter");
		break;
	case FU_SYNAPROM_RESULT_GEN_NULL_POINTER:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "NULL pointer");
		break;
	case FU_SYNAPROM_RESULT_GEN_UNEXPECTED_FORMAT:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unexpected format");
		break;
	case FU_SYNAPROM_RESULT_GEN_TIMEOUT:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT, "timed out");
		break;
	case FU_SYNAPROM_RESULT_GEN_OBJECT_DOESNT_EXIST:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "object does not exist");
		break;
	case FU_SYNAPROM_RESULT_GEN_ERROR:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "generic error");
		break;
	case FU_SYNAPROM_RESULT_SENSOR_MALFUNCTIONED:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "sensor malfunctioned");
		break;
	case FU_SYNAPROM_RESULT_SYS_OUT_OF_MEMORY:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "out of heap memory");
		break;
	default:
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "error status: 0x%x", status);
	}
	return FALSE;
}
