/*
 * Copyright 2021 Realtek Corporation
 * Copyright 2021 Ricky Wu <ricky_wu@realtek.com> <spring1527@gmail.com>
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-rts54hub-device.h"
#include "fu-rts54hub-rtd21xx-mergeinfo.h"

struct _FuRts54hubRtd21xxMergeinfo {
	FuRts54hubRtd21xxDevice parent_instance;
};

G_DEFINE_TYPE(FuRts54hubRtd21xxMergeinfo,
	      fu_rts54hub_rtd21xx_mergeinfo,
	      FU_TYPE_RTS54HUB_RTD21XX_DEVICE)

#define DEBUG_SLAVE_ADDR   0x6A
#define DDCCI_SLAVE_ADDR   0x6E
#define CHANGE_TO_DDCCI_MODE_OPCODE 0x71
#define VERSION_NUMBER_COUNT 4

static guint8 g_ack_slave_addr = 0x00;

static gboolean 
fu_rts54hub_rtd21xx_mergeinfo_ddcci_mode(FuRts54hubRtd21xxMergeinfo *self,
				    GError **error) {
	guint8 temp[] = {0};

	if (!fu_rts54hub_rtd21xx_device_i2c_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						 DEBUG_SLAVE_ADDR,
						 0x23,
						 temp,
						 sizeof(temp),
						 error)) {

		if (!fu_rts54hub_rtd21xx_device_i2c_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
							DDCCI_SLAVE_ADDR,
							0x23,
							temp,
							sizeof(temp),
							error)) {
			g_prefix_error(error, "failed to check merge info slave addr: ");
			return FALSE;
		}
		else {
			g_ack_slave_addr = DDCCI_SLAVE_ADDR;
		}
	}
	else {
		g_ack_slave_addr = DEBUG_SLAVE_ADDR;
	}

	if(g_ack_slave_addr == DEBUG_SLAVE_ADDR)
	{
		/* change debug mode to ddcci mode */
		temp[0] = 0x01;
		if (!fu_rts54hub_rtd21xx_device_i2c_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
							DEBUG_SLAVE_ADDR,
							CHANGE_TO_DDCCI_MODE_OPCODE,
							temp,
							sizeof(temp),
							error)) {
			g_prefix_error(error, "failed to change debug mode to ddcci mode: ");
			return FALSE;
		}

		/* wait for device ready */
		fu_device_sleep(FU_DEVICE(self), 300); /* ms */

		if (!fu_rts54hub_rtd21xx_device_i2c_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
							DDCCI_SLAVE_ADDR,
							0x23,
							temp,
							sizeof(temp),
							error)) {
			g_prefix_error(error, "failed to change debug mode to ddcci mode: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean 
fu_rts54hub_rtd21xx_mergeinfo_check_ddcci(FuRts54hubRtd21xxMergeinfo *self,
				    GError **error) {
	guint8 buf_reply[16] = {0x00};
	guint8 buf_request[2] = {0x00};

	/* check ddcci commmunication */ 
	buf_request[0] = 0x77;
	buf_request[1] = 0x11;
	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						DDCCI_SLAVE_ADDR,
						0x71,
						buf_request,
						sizeof(buf_request),
						error)) {
		g_prefix_error(error, "failed to ddcci communication with fw: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	if (!fu_rts54hub_rtd21xx_device_ddcci_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						DDCCI_SLAVE_ADDR,
						0x71,
						buf_reply,
						6,
						error)) {
		g_prefix_error(error, "failed to ddcci communication with fw: ");
		return FALSE;
	}

	if(buf_reply[4] != 0x90)
	{
		g_prefix_error(error, "failed to ddcci communication with fw: ");
		return FALSE;		
	}

	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_read_version(FuRts54hubRtd21xxMergeinfo *self,
					guint8 *buf_version, 
					GError **error)
{
	guint8 buf_reply[16] = {0x00};
	guint8 buf_request[2] = {0x00};
	guint32 index = 0;

	/* read merge version */
	buf_request[0] = 0x77;
	buf_request[1] = 0x99;
	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						DDCCI_SLAVE_ADDR,
						0x71,
						buf_request,
						sizeof(buf_request),
						error)) {
		g_prefix_error(error, "failed to ddcci communication with fw: ");
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	if (!fu_rts54hub_rtd21xx_device_ddcci_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
						DDCCI_SLAVE_ADDR,
						0x71,
						buf_reply,
						sizeof(buf_reply),
						error)) {
		g_prefix_error(error, "failed to ddcci communication with fw: ");
		return FALSE;
	}

	for(index = 0; index < VERSION_NUMBER_COUNT; index++)
	{
		buf_version[index] = buf_reply[4 + index];
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

	if(buf_size!= VERSION_NUMBER_COUNT) {
		g_prefix_error(error, "failed to check version buffer size: ");		
		return FALSE;
	}

	/* write merge version */ 
	buf_request[0] = 0x77;
	buf_request[1] = 0xBB;
	memcpy(buf_request + 2, buf_version, buf_size);

	if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
						DDCCI_SLAVE_ADDR,
						0x71,
						buf_request,
						sizeof(buf_request),
						error)) {
		g_prefix_error(error, "failed to write merge fw version: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_restore_state(FuRts54hubRtd21xxMergeinfo *self,
				    GError **error)
{
	guint8 buf_request[2] = {0x00};
	guint8 temp[] = {0};

	if(g_ack_slave_addr == DEBUG_SLAVE_ADDR)
	{
		buf_request[0] = 0x77;
		buf_request[1] = 0x55;
		if (!fu_rts54hub_rtd21xx_device_ddcci_write(FU_RTS54HUB_RTD21XX_DEVICE(self),
							DDCCI_SLAVE_ADDR,
							0x71,
							buf_request,
							sizeof(buf_request),
							error)) {
			g_prefix_error(error, "failed to ddcci communication with fw: ");
			return FALSE;
		}

		/* wait for device ready */
		fu_device_sleep(FU_DEVICE(self), 500); /* ms */

		if (!fu_rts54hub_rtd21xx_device_i2c_read(FU_RTS54HUB_RTD21XX_DEVICE(self),
					 DEBUG_SLAVE_ADDR,
					 0x23,
					 temp,
					 sizeof(temp),
					 error)) {
			g_prefix_error(error, "failed to change to debug slave: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_ensure_version_unlocked(FuRts54hubRtd21xxMergeinfo *self,
						       GError **error)
{
	guint8 buf_version[VERSION_NUMBER_COUNT] = {0x00};
	g_autofree gchar *version = NULL;

	if(!fu_rts54hub_rtd21xx_mergeinfo_read_version(self, buf_version, error)) {
		//g_prefix_error(error, "failed to read merge version: ");
		return FALSE;
	}

	/* set merge version */
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	version = g_strdup_printf("%u.%u.%u.%u", buf_version[0], buf_version[1], buf_version[2], buf_version[3]);
	fu_device_set_version(FU_DEVICE(self), version);	

	return TRUE;
}


static gboolean
fu_rts54hub_rtd21xx_mergeinfo_detach_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRts54hubRtd21xxMergeinfo *self = FU_RTS54HUB_RTD21XX_MERGEINFO(device);
	guint8 status = 0xfe;

	if(!fu_rts54hub_rtd21xx_mergeinfo_ddcci_mode(self, error)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "change to ddcci mode fail 0x%02x",
			    status);
		return FALSE;
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 300); /* ms */

	if (!fu_rts54hub_rtd21xx_mergeinfo_check_ddcci(self, error)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "check ddcci mode fail 0x%02x",
			    status);
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
	FuRts54HubDevice *parent = FU_RTS54HUB_DEVICE(fu_device_get_parent(device));
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_retry(device, fu_rts54hub_rtd21xx_mergeinfo_detach_cb, 10, NULL, error);
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRts54hubRtd21xxMergeinfo *self = FU_RTS54HUB_RTD21XX_MERGEINFO(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	if(!fu_rts54hub_rtd21xx_mergeinfo_restore_state(self, error)) {
		g_prefix_error(error, "failed to restore state in attach: ");
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
	FuRts54HubDevice *parent = FU_RTS54HUB_DEVICE(fu_device_get_parent(device));
	FuRts54hubRtd21xxMergeinfo *self = FU_RTS54HUB_RTD21XX_MERGEINFO(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	if(!fu_rts54hub_rtd21xx_mergeinfo_restore_state(self, error)) {
		g_prefix_error(error, "failed to restore state in attach: ");
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
	if (!fu_rts54hub_rtd21xx_mergeinfo_ensure_version_unlocked(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_rts54hub_rtd21xx_mergeinfo_reload(FuDevice *device, GError **error)
{
	FuRts54HubDevice *parent = FU_RTS54HUB_DEVICE(fu_device_get_parent(device));
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open parent device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;
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
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GInputStream) stream = NULL;	

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 40, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 50, "read");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "finish");

	/* open device */
	locker = fu_device_locker_new(self, error);
	if (locker == NULL)
		return FALSE;

	/* simple image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* get merge version */
	if (!fu_input_stream_read_safe(stream,
				       merge_version,
				       sizeof(merge_version),
				       0, /* dst */
				       0, /* src */
				       VERSION_NUMBER_COUNT,
				       error)) {
		g_prefix_error(error, "failed to get merge version info: ");
		return FALSE;
	}

	/* write version*/
	if(!fu_rts54hub_rtd21xx_mergeinfo_write_version(self, merge_version, VERSION_NUMBER_COUNT, error)){
		g_prefix_error(error, "failed to write merge version: ");
	}

	/* wait for device ready */
	fu_device_sleep(FU_DEVICE(self), 1000); /* ms */

	fu_progress_step_done(progress);	

	if(!fu_rts54hub_rtd21xx_mergeinfo_read_version(self, read_buf, error)) {
		g_prefix_error(error, "failed to read merge version: ");
		return FALSE;
	}

	fu_progress_step_done(progress);

	if(memcmp(read_buf, merge_version, VERSION_NUMBER_COUNT)!= 0) {
		g_prefix_error(error, "failed to compare merge version: ");
		return FALSE;
	}

	fu_progress_step_done(progress);

	/* success */
	return TRUE;	
}

static void
fu_rts54hub_rtd21xx_mergeinfo_init(FuRts54hubRtd21xxMergeinfo *self)
{
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
