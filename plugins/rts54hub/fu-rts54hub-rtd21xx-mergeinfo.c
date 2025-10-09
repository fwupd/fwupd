/*
 * Copyright 2021 Realtek Corporation
 * Copyright 2021 Ricky Wu <ricky_wu@realtek.com> <spring1527@gmail.com>
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
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

#define DEBUG_TARGET_ADDR	    0x6A
#define DDCCI_TARGET_ADDR	    0x6E
#define CHANGE_TO_DDCCI_MODE_OPCODE 0x71
#define DDCCI_COMM_SUB_ADDR	    0x71
#define CHECK_ACK_SUB_ADDR	    0x23
#define VERSION_NUMBER_COUNT	    4
#define DDCCI_CHECK_TARGET_VALUE    0x90

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_ddcci_mode(FuRts54hubRtd21xxMergeinfo *self, GError **error)
{
	guint8 temp = 0x01;

	/* change debug mode to DDC/CI mode */
	if (!fu_rts54hub_rtd21xx_device_i2c_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						  DEBUG_TARGET_ADDR,
						  CHANGE_TO_DDCCI_MODE_OPCODE,
						  &temp,
						  sizeof(temp),
						  error)) {
		g_prefix_error_literal(error, "failed to change debug mode to DDC/CI mode: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	if (!fu_rts54hub_rtd21xx_device_i2c_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						 DDCCI_TARGET_ADDR,
						 CHECK_ACK_SUB_ADDR,
						 &temp,
						 sizeof(temp),
						 error)) {
		g_prefix_error_literal(error, "failed to change debug mode to DDC/CI mode: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_check_ddcci(FuRts54hubRtd21xxMergeinfo *self, GError **error)
{
	guint8 buf_reply[16] = {0x00};
	guint8 buf_request[2] = {0x00};

	/* check DDC/CI communication */
	buf_request[0] = FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_FIRST;
	buf_request[1] = FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_COMMUNICATION;
	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						    DDCCI_TARGET_ADDR,
						    DDCCI_COMM_SUB_ADDR,
						    buf_request,
						    sizeof(buf_request),
						    error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	if (!fu_rts54hub_rtd21xx_device_ddcci_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						   DDCCI_TARGET_ADDR,
						   DDCCI_COMM_SUB_ADDR,
						   buf_reply,
						   6,
						   error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	if (buf_reply[4] != DDCCI_CHECK_TARGET_VALUE) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_ensure_version(FuRts54hubRtd21xxMergeinfo *self, GError **error)
{
	guint8 buf_reply[16] = {0x00};
	guint8 buf_request[2] = {0x00};
	g_autofree gchar *version = NULL;

	/* read merge version */
	buf_request[0] = FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_FIRST;
	buf_request[1] = FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_GET_VERSION;
	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						    DDCCI_TARGET_ADDR,
						    DDCCI_COMM_SUB_ADDR,
						    buf_request,
						    sizeof(buf_request),
						    error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	if (!fu_rts54hub_rtd21xx_device_ddcci_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						   DDCCI_TARGET_ADDR,
						   DDCCI_COMM_SUB_ADDR,
						   buf_reply,
						   sizeof(buf_reply),
						   error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	version =
	    g_strdup_printf("%u.%u.%u.%u", buf_reply[4], buf_reply[5], buf_reply[6], buf_reply[7]);
	fu_device_set_version(FU_DEVICE(self), version);

	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_read_version(FuRts54hubRtd21xxMergeinfo *self,
					   guint8 *buf_version,
					   GError **error)
{
	guint8 buf_reply[16] = {0x00};
	guint8 buf_request[2] = {0x00};

	/* read merge version */
	buf_request[0] = FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_FIRST;
	buf_request[1] = FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_GET_VERSION;
	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						    DDCCI_TARGET_ADDR,
						    DDCCI_COMM_SUB_ADDR,
						    buf_request,
						    sizeof(buf_request),
						    error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	if (!fu_rts54hub_rtd21xx_device_ddcci_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						   DDCCI_TARGET_ADDR,
						   DDCCI_COMM_SUB_ADDR,
						   buf_reply,
						   sizeof(buf_reply),
						   error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	if (!fu_memcpy_safe(buf_version,
			    VERSION_NUMBER_COUNT,
			    0x0, /* dst */
			    (const guint8 *)buf_reply,
			    16,
			    4, /* src */
			    VERSION_NUMBER_COUNT,
			    error)) {
		g_prefix_error_literal(error, "memcpy merge version fail: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_write_version(FuRts54hubRtd21xxMergeinfo *self,
					    const guint8 *buf_version,
					    gsize buf_size,
					    GError **error)
{
	guint8 buf_request[6] = {0x00};

	if (buf_size != VERSION_NUMBER_COUNT) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to check version buffer size: ");
		return FALSE;
	}

	/* write merge version */
	buf_request[0] = FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_FIRST;
	buf_request[1] = FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_SET_VERSION;

	if (!fu_memcpy_safe(buf_request,
			    6,
			    0x2, /* dst */
			    (const guint8 *)buf_version,
			    buf_size,
			    0, /* src */
			    buf_size,
			    error)) {
		g_prefix_error_literal(error, "memcpy merge version fail: ");
		return FALSE;
	}

	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						    DDCCI_TARGET_ADDR,
						    DDCCI_COMM_SUB_ADDR,
						    buf_request,
						    sizeof(buf_request),
						    error)) {
		g_prefix_error_literal(error, "failed to write merge fw version: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_restore_state(FuRts54hubRtd21xxMergeinfo *self, GError **error)
{
	guint8 buf_request[2] = {0x00};
	guint8 temp = 0;

	buf_request[0] = FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_FIRST;
	buf_request[1] = FU_RTS54_HUB_MERGE_INFO_DDCCI_OPCODE_DDCCI_TO_DEBUG;
	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						    DDCCI_TARGET_ADDR,
						    DDCCI_COMM_SUB_ADDR,
						    buf_request,
						    sizeof(buf_request),
						    error)) {
		g_prefix_error_literal(error, "failed to DDC/CI communication with fw: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 500); /* ms */

	if (!fu_rts54hub_rtd21xx_device_i2c_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						 DEBUG_TARGET_ADDR,
						 CHECK_ACK_SUB_ADDR,
						 &temp,
						 1,
						 error)) {
		g_prefix_error_literal(error, "failed to change to debug target addr: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_detach_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRts54hubRtd21xxMergeinfo *self = FU_RTS54HUB_RTD21XX_MERGEINFO(device);

	if (!fu_rts54hub_rtd21xx_mergeinfo_ddcci_mode(self, error)) {
		g_prefix_error_literal(error, "change to DDC/CI mode fail: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	if (!fu_rts54hub_rtd21xx_mergeinfo_check_ddcci(self, error)) {
		g_prefix_error_literal(error, "check DDC/CI mode fail: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRts54hubDevice *parent = FU_RTS54HUB_DEVICE(fu_device_get_parent(device));
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new(FU_DEVICE(parent), error);
	if (locker == NULL)
		return FALSE;
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
fu_rts54hub_rtd21xx_mergeinfo_exit(FuDevice *device, GError **error)
{
	FuRts54hubDevice *parent = FU_RTS54HUB_DEVICE(fu_device_get_parent(device));
	FuRts54hubRtd21xxMergeinfo *self = FU_RTS54HUB_RTD21XX_MERGEINFO(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new(FU_DEVICE(parent), error);
	if (locker == NULL)
		return FALSE;

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
	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_device_detach,
					   (FuDeviceLockerFunc)fu_rts54hub_rtd21xx_mergeinfo_exit,
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
	guint8 read_buf[VERSION_NUMBER_COUNT] = {0x0};
	guint8 merge_version[VERSION_NUMBER_COUNT] = {0x00};
	const gchar *version_str = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 40, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 50, "read");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "finish");

	/* open device */
	locker = fu_device_locker_new(FU_DEVICE(self), error);
	if (locker == NULL)
		return FALSE;

	// get version x.x.x.x
	version_str = fu_firmware_get_version(firmware);

	// convert x.x.x.x to merge_version
	if (version_str != NULL) {
		if (fu_device_get_version_format(FU_DEVICE(self)) == FWUPD_VERSION_FORMAT_QUAD) {
			if (sscanf(version_str,
				   "%hhu.%hhu.%hhu.%hhu",
				   &merge_version[0],
				   &merge_version[1],
				   &merge_version[2],
				   &merge_version[3]) != 4) {
				g_prefix_error_literal(error, /* nocheck:error */
						       "failed to parse version str: ");
				return FALSE;
			};
		} else {
			g_prefix_error_literal(error, /* nocheck:error */
					       "failed to get version format: ");
			return FALSE;
		}
	} else {
		g_prefix_error_literal(error, /* nocheck:error */
				       "get version in write firmware fail: ");
		return FALSE;
	}

	/* write version*/
	if (!fu_rts54hub_rtd21xx_mergeinfo_write_version(self,
							 merge_version,
							 sizeof(merge_version),
							 error)) {
		g_prefix_error_literal(error, "failed to write merge version: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 1000); /* ms */

	fu_progress_step_done(progress);

	if (!fu_rts54hub_rtd21xx_mergeinfo_read_version(self, read_buf, error)) {
		g_prefix_error_literal(error, "failed to read merge version: ");
		return FALSE;
	}

	fu_progress_step_done(progress);

	if (!fu_memcmp_safe(read_buf,
			    VERSION_NUMBER_COUNT,
			    0x0,
			    merge_version,
			    sizeof(merge_version),
			    0x0,
			    VERSION_NUMBER_COUNT,
			    error)) {
		g_prefix_error_literal(error, "failed to compare merge version: ");
		return FALSE;
	}

	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_rts54hub_rtd21xx_mergeinfo_init(FuRts54hubRtd21xxMergeinfo *self)
{
	/* set merge version format*/
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
}
