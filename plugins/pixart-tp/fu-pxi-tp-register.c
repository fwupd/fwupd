/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

 #include "config.h"

#include "fu-pxi-tp-register.h"

#define REPORT_ID_SINGLE 0x42
#define REPORT_ID_BURST	 0x41
#define REPORT_ID_USER	 0x43
#define OP_READ		 0x10

// --- System Register ---

gboolean
fu_pxi_tp_register_write(FuPxiTpDevice *self, guint8 bank, guint8 addr, guint8 val, GError **error)
{
	guint8 buf[4] = {REPORT_ID_SINGLE, addr, bank, val};
	return fu_pxi_tp_common_send_feature(self, buf, sizeof(buf), error);
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

	if (!fu_pxi_tp_common_send_feature(self, cmd, sizeof(cmd), error))
		return FALSE;

	if (!fu_pxi_tp_common_get_feature(self, REPORT_ID_SINGLE, resp, sizeof(resp), error))
		return FALSE;

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
	return fu_pxi_tp_common_send_feature(self, buf, sizeof(buf), error);
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

	if (!fu_pxi_tp_common_send_feature(self, cmd, sizeof(cmd), error))
		return FALSE;

	if (!fu_pxi_tp_common_get_feature(self, REPORT_ID_USER, resp, sizeof(resp), error))
		return FALSE;

	*out_val = resp[3];
	return TRUE;
}

/* --- Burst --- */

gboolean
fu_pxi_tp_register_burst_write(FuPxiTpDevice *self, const guint8 *buf, gsize bufsz, GError **error)
{
	guint8 payload[257] = {REPORT_ID_BURST};
	gboolean ok;

	if (bufsz > 256) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_INVALID_FILE,
				      "burst write size too big: %" G_GSIZE_FORMAT " (max 256)",
				      bufsz);
		return FALSE;
	}

	ok = fu_memcpy_safe(payload,
			    sizeof(payload),
			    1, /* dst = payload[1..] */
			    buf,
			    bufsz,
			    0,	   /* src = buf[0..]     */
			    bufsz, /* len */
			    error);
	if (!ok) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_INVALID_FILE,
				      "packing HID payload failed");
		return FALSE;
	}

	return fu_pxi_tp_common_send_feature(self, payload, sizeof(payload), error);
}

gboolean
fu_pxi_tp_register_burst_read(FuPxiTpDevice *self, guint8 *buf, gsize bufsz, GError **error)
{
	guint8 payload[257] = {REPORT_ID_BURST};
	gsize copy_len;
	gboolean ok;

	if (!fu_pxi_tp_common_get_feature(self, REPORT_ID_BURST, payload, sizeof(payload), error))
		return FALSE;

	/* 能拷的長度 = 呼叫者 buffer 大小 與 負載長度(最多 256) 的較小者 */
	copy_len = MIN(bufsz, (gsize)(sizeof(payload) - 1)); /* 排除 report id */

	ok = fu_memcpy_safe(buf,
			    bufsz,
			    0, /* dst */
			    payload,
			    sizeof(payload),
			    1, /* src 跳過 report id */
			    copy_len,
			    error);
	if (!ok) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_INVALID_FILE,
				      "unpacking HID payload failed");
		return FALSE;
	}

	return TRUE;
}
