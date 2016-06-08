/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:dfu-common
 * @short_description: Common functions for DFU
 *
 * These helper objects allow converting from enum values to strings.
 */

#include "config.h"

#include "dfu-common.h"

/**
 * dfu_state_to_string:
 * @state: a #DfuState, e.g. %DFU_STATE_DFU_MANIFEST
 *
 * Converts an enumerated value to a string.
 *
 * Return value: a string
 *
 * Since: 0.5.4
 **/
const gchar *
dfu_state_to_string (DfuState state)
{
	if (state == DFU_STATE_APP_IDLE)
		return "appIDLE";
	if (state == DFU_STATE_APP_DETACH)
		return "appDETACH";
	if (state == DFU_STATE_DFU_IDLE)
		return "dfuIDLE";
	if (state == DFU_STATE_DFU_DNLOAD_SYNC)
		return "dfuDNLOAD-SYNC";
	if (state == DFU_STATE_DFU_DNBUSY)
		return "dfuDNBUSY";
	if (state == DFU_STATE_DFU_DNLOAD_IDLE)
		return "dfuDNLOAD-IDLE";
	if (state == DFU_STATE_DFU_MANIFEST_SYNC)
		return "dfuMANIFEST-SYNC";
	if (state == DFU_STATE_DFU_MANIFEST)
		return "dfuMANIFEST";
	if (state == DFU_STATE_DFU_MANIFEST_WAIT_RESET)
		return "dfuMANIFEST-WAIT-RESET";
	if (state == DFU_STATE_DFU_UPLOAD_IDLE)
		return "dfuUPLOAD-IDLE";
	if (state == DFU_STATE_DFU_ERROR)
		return "dfuERROR";
	return NULL;
}

/**
 * dfu_status_to_string:
 * @status: a #DfuStatus, e.g. %DFU_STATUS_ERR_ERASE
 *
 * Converts an enumerated value to a string.
 *
 * Return value: a string
 *
 * Since: 0.5.4
 **/
const gchar *
dfu_status_to_string (DfuStatus status)
{
	if (status == DFU_STATUS_OK)
		return "OK";
	if (status == DFU_STATUS_ERR_TARGET)
		return "errTARGET";
	if (status == DFU_STATUS_ERR_FILE)
		return "errFILE";
	if (status == DFU_STATUS_ERR_WRITE)
		return "errwrite";
	if (status == DFU_STATUS_ERR_ERASE)
		return "errERASE";
	if (status == DFU_STATUS_ERR_CHECK_ERASED)
		return "errCHECK_ERASED";
	if (status == DFU_STATUS_ERR_PROG)
		return "errPROG";
	if (status == DFU_STATUS_ERR_VERIFY)
		return "errVERIFY";
	if (status == DFU_STATUS_ERR_ADDRESS)
		return "errADDRESS";
	if (status == DFU_STATUS_ERR_NOTDONE)
		return "errNOTDONE";
	if (status == DFU_STATUS_ERR_FIRMWARE)
		return "errFIRMWARE";
	if (status == DFU_STATUS_ERR_VENDOR)
		return "errVENDOR";
	if (status == DFU_STATUS_ERR_USBR)
		return "errUSBR";
	if (status == DFU_STATUS_ERR_POR)
		return "errPOR";
	if (status == DFU_STATUS_ERR_UNKNOWN)
		return "errUNKNOWN";
	if (status == DFU_STATUS_ERR_STALLDPKT)
		return "errSTALLDPKT";
	return NULL;
}

/**
 * dfu_mode_to_string:
 * @mode: a #DfuMode, e.g. %DFU_MODE_RUNTIME
 *
 * Converts an enumerated value to a string.
 *
 * Return value: a string
 *
 * Since: 0.5.4
 **/
const gchar *
dfu_mode_to_string (DfuMode mode)
{
	if (mode == DFU_MODE_RUNTIME)
		return "runtime";
	if (mode == DFU_MODE_DFU)
		return "DFU";
	return NULL;
}

/**
 * dfu_cipher_kind_to_string:
 * @cipher_kind: a #DfuCipherKind, e.g. %DFU_CIPHER_KIND_XTEA
 *
 * Converts an enumerated value to a string.
 *
 * Return value: a string
 *
 * Since: 0.5.4
 **/
const gchar *
dfu_cipher_kind_to_string (DfuCipherKind cipher_kind)
{
	if (cipher_kind == DFU_CIPHER_KIND_NONE)
		return "none";
	if (cipher_kind == DFU_CIPHER_KIND_XTEA)
		return "xtea";
	return NULL;
}

/**
 * dfu_version_to_string:
 * @version: a #DfuVersion, e.g. %DFU_VERSION_DFU_1_1
 *
 * Converts an enumerated value to a string.
 *
 * Return value: a string
 *
 * Since: 0.7.2
 **/
const gchar *
dfu_version_to_string (DfuVersion version)
{
	if (version == DFU_VERSION_DFU_1_0)
		return "1.0";
	if (version == DFU_VERSION_DFU_1_1)
		return "1.1";
	if (version == DFU_VERSION_DFUSE)
		return "DfuSe";
	return NULL;
}
