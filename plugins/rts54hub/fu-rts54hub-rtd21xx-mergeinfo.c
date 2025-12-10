/*
 * Copyright 2025 Realtek Corporation
 * Copyright 2025 Shadow Zhang <shadow_zhang@realsil.com.cn>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <stdio.h>

#include "fu-rts54hub-device.h"
#include "fu-rts54hub-rtd21xx-mergeinfo.h"
#include "fu-rts54hub-struct.h"

struct _FuRts54hubRtd21xxMergeinfo {
	FuRts54hubRtd21xxDevice parent_instance;
};

G_DEFINE_TYPE(FuRts54hubRtd21xxMergeinfo,
	      fu_rts54hub_rtd21xx_mergeinfo,
	      FU_TYPE_RTS54HUB_RTD21XX_DEVICE)

#define FU_RTS54HUB_MERGEINFO_ADDR_DEBUG_TARGET 0x6A
#define FU_RTS54HUB_MERGEINFO_ADDR_DDCCI_TARGET 0x6E

#define FU_RTS54HUB_MERGEINFO_OPCODE_CHANGE_TO_DDCCI_MODE 0x71

#define FU_RTS54HUB_MERGEINFO_SUB_ADDR_DDCCI_COMM 0x71
#define FU_RTS54HUB_MERGEINFO_SUB_ADDR_CHECK_ACK  0x23

#define FU_RTS54HUB_MERGEINFO_VERSION_BUFSZ 4

#define FU_RTS54HUB_MERGEINFO_DDCCI_CHECK_TARGET_VALUE 0x90

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_ddcci_mode(FuRts54hubRtd21xxMergeinfo *self, GError **error)
{
	guint8 temp = 0x01;

	/* change debug mode to DDC/CI mode */
	if (!fu_rts54hub_rtd21xx_device_i2c_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						  FU_RTS54HUB_MERGEINFO_ADDR_DEBUG_TARGET,
						  FU_RTS54HUB_MERGEINFO_OPCODE_CHANGE_TO_DDCCI_MODE,
						  &temp,
						  sizeof(temp),
						  error)) {
		g_prefix_error_literal(error, "failed to change debug mode to DDC/CI mode: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	if (!fu_rts54hub_rtd21xx_device_i2c_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						 FU_RTS54HUB_MERGEINFO_ADDR_DDCCI_TARGET,
						 FU_RTS54HUB_MERGEINFO_SUB_ADDR_CHECK_ACK,
						 &temp,
						 sizeof(temp),
						 error)) {
		g_prefix_error_literal(error, "failed to change debug mode to DDC/CI mode: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_check_ddcci(FuRts54hubRtd21xxMergeinfo *self, GError **error)
{
	guint8 buf_reply[16] = {0x00};
	g_autoptr(FuStructRts54HubDdcPkt) st = fu_struct_rts54_hub_ddc_pkt_new();

	/* check DDC/CI communication */
	fu_struct_rts54_hub_ddc_pkt_set_second_opcode(
	    st,
	    FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_COMMUNICATION);

	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						    FU_RTS54HUB_MERGEINFO_ADDR_DDCCI_TARGET,
						    FU_RTS54HUB_MERGEINFO_SUB_ADDR_DDCCI_COMM,
						    st->buf->data,
						    st->buf->len,
						    error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	if (!fu_rts54hub_rtd21xx_device_ddcci_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						   FU_RTS54HUB_MERGEINFO_ADDR_DDCCI_TARGET,
						   FU_RTS54HUB_MERGEINFO_SUB_ADDR_DDCCI_COMM,
						   buf_reply,
						   sizeof(buf_reply),
						   error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	if (buf_reply[4] != FU_RTS54HUB_MERGEINFO_DDCCI_CHECK_TARGET_VALUE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_ensure_version(FuRts54hubRtd21xxMergeinfo *self, GError **error)
{
	guint8 buf_reply[16] = {0x00};
	guint32 version_raw = 0;
	g_autoptr(FuStructRts54HubDdcPkt) st = fu_struct_rts54_hub_ddc_pkt_new();

	/* read merge version */
	fu_struct_rts54_hub_ddc_pkt_set_second_opcode(
	    st,
	    FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_GET_VERSION);

	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						    FU_RTS54HUB_MERGEINFO_ADDR_DDCCI_TARGET,
						    FU_RTS54HUB_MERGEINFO_SUB_ADDR_DDCCI_COMM,
						    st->buf->data,
						    st->buf->len,
						    error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	if (!fu_rts54hub_rtd21xx_device_ddcci_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						   FU_RTS54HUB_MERGEINFO_ADDR_DDCCI_TARGET,
						   FU_RTS54HUB_MERGEINFO_SUB_ADDR_DDCCI_COMM,
						   buf_reply,
						   sizeof(buf_reply),
						   error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	if (!fu_memread_uint32_safe(buf_reply,
				    sizeof(buf_reply),
				    4,
				    &version_raw,
				    G_BIG_ENDIAN,
				    error)) {
		g_prefix_error_literal(error, "converting version to uint32 failed: ");
		return FALSE;
	}
	fu_device_set_version_raw(FU_DEVICE(self), version_raw);

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_read_version(FuRts54hubRtd21xxMergeinfo *self,
					   guint8 *buf_version,
					   gsize buf_version_sz,
					   GError **error)
{
	guint8 buf_reply[16] = {0x00};
	g_autoptr(FuStructRts54HubDdcPkt) st = fu_struct_rts54_hub_ddc_pkt_new();

	/* read merge version */
	fu_struct_rts54_hub_ddc_pkt_set_second_opcode(
	    st,
	    FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_GET_VERSION);

	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						    FU_RTS54HUB_MERGEINFO_ADDR_DDCCI_TARGET,
						    FU_RTS54HUB_MERGEINFO_SUB_ADDR_DDCCI_COMM,
						    st->buf->data,
						    st->buf->len,
						    error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	if (!fu_rts54hub_rtd21xx_device_ddcci_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						   FU_RTS54HUB_MERGEINFO_ADDR_DDCCI_TARGET,
						   FU_RTS54HUB_MERGEINFO_SUB_ADDR_DDCCI_COMM,
						   buf_reply,
						   sizeof(buf_reply),
						   error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	if (!fu_memcpy_safe(buf_version,
			    buf_version_sz,
			    0x0, /* dst */
			    buf_reply,
			    16,
			    4, /* src */
			    buf_version_sz,
			    error)) {
		g_prefix_error_literal(error, "memcpy merge version fail: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_write_version(FuRts54hubRtd21xxMergeinfo *self,
					    const guint8 *buf_version,
					    gsize buf_version_sz,
					    GError **error)
{
	g_autoptr(FuStructRts54HubDdcWriteMergeInfoPkt) st =
	    fu_struct_rts54_hub_ddc_write_merge_info_pkt_new();

	if (buf_version_sz != FU_RTS54HUB_MERGEINFO_VERSION_BUFSZ) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to check version buffer size: ");
		return FALSE;
	}

	/* write merge version */
	fu_struct_rts54_hub_ddc_write_merge_info_pkt_set_second_opcode(
	    st,
	    FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_SET_VERSION);
	fu_struct_rts54_hub_ddc_write_merge_info_pkt_set_major_version(st, buf_version[0]);
	fu_struct_rts54_hub_ddc_write_merge_info_pkt_set_minor_version(st, buf_version[1]);
	fu_struct_rts54_hub_ddc_write_merge_info_pkt_set_patch_version(st, buf_version[2]);
	fu_struct_rts54_hub_ddc_write_merge_info_pkt_set_build_version(st, buf_version[3]);

	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						    FU_RTS54HUB_MERGEINFO_ADDR_DDCCI_TARGET,
						    FU_RTS54HUB_MERGEINFO_SUB_ADDR_DDCCI_COMM,
						    st->buf->data,
						    st->buf->len,
						    error)) {
		g_prefix_error_literal(error, "failed to write merge fw version: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_restore_state(FuRts54hubRtd21xxMergeinfo *self, GError **error)
{
	guint8 temp = 0;
	g_autoptr(FuStructRts54HubDdcPkt) st = fu_struct_rts54_hub_ddc_pkt_new();

	fu_struct_rts54_hub_ddc_pkt_set_second_opcode(
	    st,
	    FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_DDCCI_TO_DEBUG);

	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						    FU_RTS54HUB_MERGEINFO_ADDR_DDCCI_TARGET,
						    FU_RTS54HUB_MERGEINFO_SUB_ADDR_DDCCI_COMM,
						    st->buf->data,
						    st->buf->len,
						    error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 500); /* ms */

	if (!fu_rts54hub_rtd21xx_device_i2c_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						 FU_RTS54HUB_MERGEINFO_ADDR_DEBUG_TARGET,
						 FU_RTS54HUB_MERGEINFO_SUB_ADDR_CHECK_ACK,
						 &temp,
						 1,
						 error)) {
		g_prefix_error_literal(error, "failed to change to debug target addr: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_detach_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRts54hubRtd21xxMergeinfo *self = FU_RTS54HUB_RTD21XX_MERGEINFO(device);

	/* wait for device ready */
	if (!fu_rts54hub_rtd21xx_mergeinfo_ddcci_mode(self, error)) {
		g_prefix_error_literal(error, "change to DDC/CI mode fail: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */
	if (!fu_rts54hub_rtd21xx_mergeinfo_check_ddcci(self, error)) {
		g_prefix_error_literal(error, "check DDC/CI mode fail: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	return fu_device_retry_full(device,
				    fu_rts54hub_rtd21xx_mergeinfo_detach_cb,
				    10,
				    300,
				    NULL,
				    error);
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRts54hubRtd21xxMergeinfo *self = FU_RTS54HUB_RTD21XX_MERGEINFO(device);

	if (!fu_rts54hub_rtd21xx_mergeinfo_restore_state(self, error)) {
		g_prefix_error_literal(error, "failed to restore state in attach: ");
		return FALSE;
	}
	/* the device needs some time to restart with the new firmware before
	 * it can be queried again */
	fu_device_sleep_full(device, 1000, progress); /* ms */

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_exit_cb(FuDevice *device, GError **error)
{
	FuRts54hubRtd21xxMergeinfo *self = FU_RTS54HUB_RTD21XX_MERGEINFO(device);

	if (!fu_rts54hub_rtd21xx_mergeinfo_restore_state(self, error)) {
		g_prefix_error_literal(error, "failed to restore state in attach: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_setup(FuDevice *device, GError **error)
{
	FuRts54hubRtd21xxMergeinfo *self = FU_RTS54HUB_RTD21XX_MERGEINFO(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* get version */
	locker =
	    fu_device_locker_new_full(device,
				      (FuDeviceLockerFunc)fu_device_detach,
				      (FuDeviceLockerFunc)fu_rts54hub_rtd21xx_mergeinfo_exit_cb,
				      error);
	if (locker == NULL)
		return FALSE;
	if (!fu_rts54hub_rtd21xx_mergeinfo_ensure_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_reload(FuDevice *device, GError **error)
{
	return fu_rts54hub_rtd21xx_mergeinfo_setup(device, error);
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_write_firmware(FuDevice *device,
					     FuFirmware *firmware,
					     FuProgress *progress,
					     FwupdInstallFlags flags,
					     GError **error)
{
	FuRts54hubRtd21xxMergeinfo *self = FU_RTS54HUB_RTD21XX_MERGEINFO(device);
	guint8 buf[FU_RTS54HUB_MERGEINFO_VERSION_BUFSZ] = {0x0};
	guint8 buf_version[FU_RTS54HUB_MERGEINFO_VERSION_BUFSZ] = {0x00};
	const gchar *version_str = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 50, "read");

	/* get version x.x.x.x */
	version_str = fu_firmware_get_version(firmware);
	if (version_str == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "get version in write firmware fail: ");
		return FALSE;
	}

	/* convert x.x.x.x to buf_version */
	if (fu_device_get_version_format(FU_DEVICE(self)) == FWUPD_VERSION_FORMAT_QUAD) {
		if (sscanf(version_str,
			   "%hhu.%hhu.%hhu.%hhu",
			   &buf_version[0],
			   &buf_version[1],
			   &buf_version[2],
			   &buf_version[3]) != 4) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "failed to parse version str: ");
			return FALSE;
		}
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to get version format: ");
		return FALSE;
	}

	/* write version*/
	if (!fu_rts54hub_rtd21xx_mergeinfo_write_version(self,
							 buf_version,
							 sizeof(buf_version),
							 error)) {
		g_prefix_error_literal(error, "failed to write merge version: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 1000); /* ms */
	fu_progress_step_done(progress);

	if (!fu_rts54hub_rtd21xx_mergeinfo_read_version(self, buf, sizeof(buf), error)) {
		g_prefix_error_literal(error, "failed to read merge version: ");
		return FALSE;
	}
	if (!fu_memcmp_safe(buf,
			    sizeof(buf),
			    0x0,
			    buf_version,
			    sizeof(buf_version),
			    0x0,
			    FU_RTS54HUB_MERGEINFO_VERSION_BUFSZ,
			    error)) {
		g_prefix_error_literal(error, "failed to compare merge version: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gchar *
fu_rts54hub_rtd21xx_mergeinfo_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_rts54hub_rtd21xx_mergeinfo_init(FuRts54hubRtd21xxMergeinfo *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
}

static void
fu_rts54hub_rtd21xx_mergeinfo_class_init(FuRts54hubRtd21xxMergeinfoClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_rts54hub_rtd21xx_mergeinfo_setup;
	device_class->reload = fu_rts54hub_rtd21xx_mergeinfo_reload;
	device_class->attach = fu_rts54hub_rtd21xx_mergeinfo_attach;
	device_class->detach = fu_rts54hub_rtd21xx_mergeinfo_detach;
	device_class->write_firmware = fu_rts54hub_rtd21xx_mergeinfo_write_firmware;
	device_class->convert_version = fu_rts54hub_rtd21xx_mergeinfo_convert_version;
}
