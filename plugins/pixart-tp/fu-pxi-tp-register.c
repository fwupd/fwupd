/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-pxi-tp-register.h"

#define REPORT_ID_SINGLE 0x42
#define REPORT_ID_BURST	 0x41
#define REPORT_ID_USER	 0x43
#define OP_READ		 0x10

gboolean
fu_pxi_tp_register_write(FuPxiTpDevice *self, guint8 bank, guint8 addr, guint8 val, GError **error)
{
	guint8 buf[4] = {REPORT_ID_SINGLE, addr, bank, val};
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PXI_TP_DEVICE(self), FALSE);

	if (!fu_pxi_tp_common_send_feature(self, buf, sizeof(buf), &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "register write failed: bank=0x%02x addr=0x%02x val=0x%02x: %s",
			    bank,
			    addr,
			    val,
			    error_local != NULL ? error_local->message : "no details");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_pxi_tp_register_read(FuPxiTpDevice *self,
			guint8 bank,
			guint8 addr,
			guint8 *out_val,
			GError **error)
{
	guint8 cmd[4] = {REPORT_ID_SINGLE, addr, (guint8)(bank | OP_READ), 0x00};
	guint8 resp[4] = {REPORT_ID_SINGLE};
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PXI_TP_DEVICE(self), FALSE);
	g_return_val_if_fail(out_val != NULL, FALSE);

	if (!fu_pxi_tp_common_send_feature(self, cmd, sizeof(cmd), &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "register read command failed: bank=0x%02x addr=0x%02x: %s",
			    bank,
			    addr,
			    error_local != NULL ? error_local->message : "no details");
		return FALSE;
	}

	error_local = NULL;
	if (!fu_pxi_tp_common_get_feature(self,
					  REPORT_ID_SINGLE,
					  resp,
					  sizeof(resp),
					  &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "register read response failed: bank=0x%02x addr=0x%02x: %s",
			    bank,
			    addr,
			    error_local != NULL ? error_local->message : "no details");
		return FALSE;
	}

	*out_val = resp[3];
	return TRUE;
}

/* --- User Register --- */
gboolean
fu_pxi_tp_register_user_write(FuPxiTpDevice *self,
			      guint8 bank,
			      guint8 addr,
			      guint8 val,
			      GError **error)
{
	guint8 buf[4] = {REPORT_ID_USER, addr, bank, val};
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PXI_TP_DEVICE(self), FALSE);

	if (!fu_pxi_tp_common_send_feature(self, buf, sizeof(buf), &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "user register write failed: bank=0x%02x addr=0x%02x val=0x%02x: %s",
			    bank,
			    addr,
			    val,
			    error_local != NULL ? error_local->message : "no details");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_pxi_tp_register_user_read(FuPxiTpDevice *self,
			     guint8 bank,
			     guint8 addr,
			     guint8 *out_val,
			     GError **error)
{
	guint8 cmd[4] = {REPORT_ID_USER, addr, (guint8)(bank | OP_READ), 0x00};
	guint8 resp[4] = {REPORT_ID_USER};
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PXI_TP_DEVICE(self), FALSE);
	g_return_val_if_fail(out_val != NULL, FALSE);

	if (!fu_pxi_tp_common_send_feature(self, cmd, sizeof(cmd), &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "user register read command failed: bank=0x%02x addr=0x%02x: %s",
			    bank,
			    addr,
			    error_local != NULL ? error_local->message : "no details");
		return FALSE;
	}

	error_local = NULL;
	if (!fu_pxi_tp_common_get_feature(self, REPORT_ID_USER, resp, sizeof(resp), &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "user register read response failed: bank=0x%02x addr=0x%02x: %s",
			    bank,
			    addr,
			    error_local != NULL ? error_local->message : "no details");
		return FALSE;
	}

	*out_val = resp[3];
	return TRUE;
}

/* --- Burst --- */
gboolean
fu_pxi_tp_register_burst_write(FuPxiTpDevice *self, const guint8 *buf, gsize bufsz, GError **error)
{
	guint8 payload[257] = {REPORT_ID_BURST};
	gboolean ok;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PXI_TP_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);

	if (bufsz > 256) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "burst write size too big: %" G_GSIZE_FORMAT " (max 256)",
			    bufsz);
		return FALSE;
	}

	ok = fu_memcpy_safe(payload,
			    sizeof(payload),
			    1, /* dst offset: payload[1..] */
			    buf,
			    bufsz,
			    0, /* src offset: buf[0..] */
			    bufsz,
			    &error_local);
	if (!ok) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "burst write memcpy failed: %s",
			    error_local != NULL ? error_local->message : "no details");
		return FALSE;
	}

	if (!fu_pxi_tp_common_send_feature(self, payload, sizeof(payload), &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "burst write feature report failed: %s",
			    error_local != NULL ? error_local->message : "no details");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_pxi_tp_register_burst_read(FuPxiTpDevice *self, guint8 *buf, gsize bufsz, GError **error)
{
	guint8 payload[257] = {REPORT_ID_BURST};
	gsize copy_len;
	gboolean ok;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PXI_TP_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);

	if (!fu_pxi_tp_common_get_feature(self,
					  REPORT_ID_BURST,
					  payload,
					  sizeof(payload),
					  &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "burst read feature report failed: %s",
			    error_local != NULL ? error_local->message : "no details");
		return FALSE;
	}

	/* skip report id */
	copy_len = MIN(bufsz, (gsize)(sizeof(payload) - 1));

	error_local = NULL;
	ok = fu_memcpy_safe(buf,
			    bufsz,
			    0, /* dst offset */
			    payload,
			    sizeof(payload),
			    1, /* src offset: skip report id */
			    copy_len,
			    &error_local);
	if (!ok) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "burst read memcpy failed: %s",
			    error_local != NULL ? error_local->message : "no details");
		return FALSE;
	}

	return TRUE;
}