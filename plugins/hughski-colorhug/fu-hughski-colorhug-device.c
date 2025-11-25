/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-hughski-colorhug-device.h"
#include "fu-hughski-colorhug-struct.h"

#define FU_HUGHSKI_COLORHUG_DEVICE_FLAG_HALFSIZE "halfsize"

struct _FuHughskiColorhugDevice {
	FuUsbDevice parent_instance;
	guint16 start_addr;
};

G_DEFINE_TYPE(FuHughskiColorhugDevice, fu_hughski_colorhug_device, FU_TYPE_USB_DEVICE)

#define CH_CMD_GET_FIRMWARE_VERSION 0x07
#define CH_CMD_RESET		    0x24
#define CH_CMD_READ_FLASH	    0x25
#define CH_CMD_WRITE_FLASH	    0x26
#define CH_CMD_BOOT_FLASH	    0x27
#define CH_CMD_SET_FLASH_SUCCESS    0x28
#define CH_CMD_ERASE_FLASH	    0x29

#define CH_USB_HID_EP		   0x0001
#define CH_USB_HID_EP_IN	   (CH_USB_HID_EP | 0x80)
#define CH_USB_HID_EP_OUT	   (CH_USB_HID_EP | 0x00)
#define CH_USB_HID_EP_SIZE	   64
#define CH_USB_CONFIG		   0x0001
#define CH_USB_INTERFACE	   0x0000
#define CH_EEPROM_ADDR_RUNCODE	   0x4000
#define CH_EEPROM_ADDR_RUNCODE_ALS 0x2000

#define CH_DEVICE_USB_TIMEOUT	     5000  /* ms */
#define CH_FLASH_TRANSFER_BLOCK_SIZE 0x020 /* 32 */

static gboolean
fu_hughski_colorhug_device_msg(FuHughskiColorhugDevice *self,
			       guint8 cmd,
			       guint8 *ibuf,
			       gsize ibufsz,
			       guint8 *obuf,
			       gsize obufsz,
			       GError **error)
{
	guint8 buf[] = {[0] = cmd, [1 ... CH_USB_HID_EP_SIZE - 1] = 0x00};
	gsize actual_length = 0;
	g_autoptr(GError) error_local = NULL;

	/* check size */
	if (ibufsz > sizeof(buf) - 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "cannot process chunk of size %" G_GSIZE_FORMAT,
			    ibufsz);
		return FALSE;
	}
	if (obufsz > sizeof(buf) - 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "cannot process chunk of size %" G_GSIZE_FORMAT,
			    ibufsz);
		return FALSE;
	}

	/* optionally copy in data */
	if (ibuf != NULL) {
		if (!fu_memcpy_safe(buf,
				    sizeof(buf),
				    0x1, /* dst */
				    ibuf,
				    ibufsz,
				    0x0, /* src */
				    ibufsz,
				    error))
			return FALSE;
	}

	/* request */
	fu_dump_raw(G_LOG_DOMAIN, "REQ", buf, ibufsz + 1);
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      CH_USB_HID_EP_OUT,
					      buf,
					      sizeof(buf),
					      &actual_length,
					      CH_DEVICE_USB_TIMEOUT,
					      NULL, /* cancellable */
					      &error_local)) {
		if (cmd == CH_CMD_RESET &&
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("ignoring '%s' on reset", error_local->message);
			return TRUE;
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to send request: ");
		return FALSE;
	}
	if (actual_length != CH_USB_HID_EP_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "request not all sent, got %" G_GSIZE_FORMAT,
			    actual_length);
		return FALSE;
	}

	/* read reply */
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      CH_USB_HID_EP_IN,
					      buf,
					      sizeof(buf),
					      &actual_length,
					      CH_DEVICE_USB_TIMEOUT,
					      NULL, /* cancellable */
					      &error_local)) {
		if (cmd == CH_CMD_RESET &&
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("ignoring '%s' on reset", error_local->message);
			return TRUE;
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to get reply: ");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "RES", buf, actual_length);

	/* old bootloaders do not return the full block */
	if (actual_length != CH_USB_HID_EP_SIZE && actual_length != 2 &&
	    actual_length != obufsz + 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "request not all received, got %" G_GSIZE_FORMAT,
			    actual_length);
		return FALSE;
	}

	/* check error code */
	if (buf[0] != FU_HUGHSKI_COLORHUG_ERROR_NONE) {
		const gchar *msg = fu_hughski_colorhug_error_to_string(buf[0]);
		if (msg == NULL)
			msg = "unknown error";
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, msg);
		return FALSE;
	}

	/* check cmd matches */
	if (buf[1] != cmd) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "cmd incorrect, expected %u, got %u",
			    cmd,
			    buf[1]);
		return FALSE;
	}

	/* copy back optional buf */
	if (obuf != NULL) {
		if (!fu_memcpy_safe(obuf,
				    obufsz,
				    0x0, /* dst */
				    buf,
				    sizeof(buf),
				    0x2, /* src */
				    obufsz,
				    error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_hughski_colorhug_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuHughskiColorhugDevice *self = FU_HUGHSKI_COLORHUG_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}
	if (!fu_hughski_colorhug_device_msg(self,
					    CH_CMD_RESET,
					    NULL,
					    0, /* in */
					    NULL,
					    0, /* out */
					    &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to reset device: %s",
			    error_local->message);
		return FALSE;
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_hughski_colorhug_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuHughskiColorhugDevice *self = FU_HUGHSKI_COLORHUG_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}
	if (!fu_hughski_colorhug_device_msg(self,
					    CH_CMD_BOOT_FLASH,
					    NULL,
					    0, /* in */
					    NULL,
					    0, /* out */
					    &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to boot to runtime: %s",
			    error_local->message);
		return FALSE;
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_hughski_colorhug_device_set_flash_success(FuHughskiColorhugDevice *self,
					     gboolean val,
					     GError **error)
{
	guint8 buf[] = {[0] = val ? 0x01 : 0x00};
	g_autoptr(GError) error_local = NULL;

	g_debug("setting flash success %s", val ? "true" : "false");
	if (!fu_hughski_colorhug_device_msg(self,
					    CH_CMD_SET_FLASH_SUCCESS,
					    buf,
					    sizeof(buf), /* in */
					    NULL,
					    0, /* out */
					    &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to set flash success: %s",
			    error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_hughski_colorhug_device_reload(FuDevice *device, GError **error)
{
	FuHughskiColorhugDevice *self = FU_HUGHSKI_COLORHUG_DEVICE(device);
	return fu_hughski_colorhug_device_set_flash_success(self, TRUE, error);
}

static gboolean
fu_hughski_colorhug_device_erase(FuHughskiColorhugDevice *self,
				 guint16 addr,
				 gsize sz,
				 GError **error)
{
	guint8 buf[4]; /* nocheck:zero-init */
	g_autoptr(GError) error_local = NULL;

	fu_memwrite_uint16(buf + 0, addr, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(buf + 2, sz, G_LITTLE_ENDIAN);
	if (!fu_hughski_colorhug_device_msg(self,
					    CH_CMD_ERASE_FLASH,
					    buf,
					    sizeof(buf), /* in */
					    NULL,
					    0, /* out */
					    &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to erase device: %s",
			    error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gchar *
fu_hughski_colorhug_device_get_version(FuHughskiColorhugDevice *self, GError **error)
{
	guint8 buf[6] = {0};
	if (!fu_hughski_colorhug_device_msg(self,
					    CH_CMD_GET_FIRMWARE_VERSION,
					    NULL,
					    0, /* in */
					    buf,
					    sizeof(buf), /* out */
					    error)) {
		return NULL;
	}
	return g_strdup_printf("%i.%i.%i",
			       fu_memread_uint16(buf + 0, G_LITTLE_ENDIAN),
			       fu_memread_uint16(buf + 2, G_LITTLE_ENDIAN),
			       fu_memread_uint16(buf + 4, G_LITTLE_ENDIAN));
}

static gboolean
fu_hughski_colorhug_device_probe(FuDevice *device, GError **error)
{
	FuHughskiColorhugDevice *self = FU_HUGHSKI_COLORHUG_DEVICE(device);

	/* compact memory layout */
	if (fu_device_has_private_flag(device, FU_HUGHSKI_COLORHUG_DEVICE_FLAG_HALFSIZE))
		self->start_addr = CH_EEPROM_ADDR_RUNCODE_ALS;

	/* add hardcoded bits */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_hughski_colorhug_device_setup(FuDevice *device, GError **error)
{
	FuHughskiColorhugDevice *self = FU_HUGHSKI_COLORHUG_DEVICE(device);
	guint idx;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_hughski_colorhug_device_parent_class)->setup(device, error))
		return FALSE;

	/* get version number, falling back to the USB device release */
	idx = fu_usb_device_get_custom_index(FU_USB_DEVICE(device),
					     FU_USB_CLASS_VENDOR_SPECIFIC,
					     'F',
					     'W',
					     NULL);
	if (idx != 0x00) {
		g_autofree gchar *tmp = NULL;
		tmp = fu_usb_device_get_string_descriptor(FU_USB_DEVICE(device), idx, NULL);
		/* although guessing is a route to insanity, if the device has
		 * provided the extra data it's because the BCD type was not
		 * suitable -- and INTEL_ME is not relevant here */
		if (tmp != NULL) {
			fu_device_set_version_format(device, fu_version_guess_format(tmp));
			fu_device_set_version(device, tmp);
		}
	}

	/* get GUID from the descriptor if set */
	idx = fu_usb_device_get_custom_index(FU_USB_DEVICE(device),
					     FU_USB_CLASS_VENDOR_SPECIFIC,
					     'G',
					     'U',
					     NULL);
	if (idx != 0x00) {
		g_autofree gchar *tmp = NULL;
		tmp = fu_usb_device_get_string_descriptor(FU_USB_DEVICE(device), idx, NULL);
		fu_device_add_instance_id(device, tmp);
	}

	/* using the USB descriptor and old firmware */
	if (fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_BCD) {
		g_autofree gchar *version = NULL;
		g_autoptr(GError) error_local = NULL;
		version = fu_hughski_colorhug_device_get_version(self, &error_local);
		if (version != NULL) {
			g_debug("obtained fwver using API '%s'", version);
			fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
			fu_device_set_version(device, version);
		} else {
			g_warning("failed to get firmware version: %s", error_local->message);
		}
	}

	/* success */
	return TRUE;
}

static guint8
fu_hughski_colorhug_device_calculate_checksum(const guint8 *data, gsize len)
{
	guint8 checksum = 0xff;
	for (guint32 i = 0; i < len; i++)
		checksum ^= data[i];
	return checksum;
}

static gboolean
fu_hughski_colorhug_device_write_blocks(FuHughskiColorhugDevice *self,
					FuChunkArray *chunks,
					FuProgress *progress,
					GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		guint8 buf[CH_FLASH_TRANSFER_BLOCK_SIZE + 4] = {0};
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GError) error_local = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* set address, length, checksum, data */
		fu_memwrite_uint16(buf + 0, fu_chunk_get_address(chk), G_LITTLE_ENDIAN);
		buf[2] = fu_chunk_get_data_sz(chk);
		buf[3] = fu_hughski_colorhug_device_calculate_checksum(fu_chunk_get_data(chk),
								       fu_chunk_get_data_sz(chk));
		if (!fu_memcpy_safe(buf,
				    sizeof(buf),
				    0x4, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;
		if (!fu_hughski_colorhug_device_msg(self,
						    CH_CMD_WRITE_FLASH,
						    buf,
						    sizeof(buf), /* in */
						    NULL,
						    0, /* out */
						    &error_local)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to write: %s",
				    error_local->message);
			return FALSE;
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hughski_colorhug_device_verify_blocks(FuHughskiColorhugDevice *self,
					 FuChunkArray *chunks,
					 FuProgress *progress,
					 GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		guint8 buf[3] = {0};
		guint8 buf_out[CH_FLASH_TRANSFER_BLOCK_SIZE + 1] = {0};
		g_autoptr(GError) error_local = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* set address */
		fu_memwrite_uint16(buf + 0, fu_chunk_get_address(chk), G_LITTLE_ENDIAN);
		buf[2] = fu_chunk_get_data_sz(chk);
		if (!fu_hughski_colorhug_device_msg(self,
						    CH_CMD_READ_FLASH,
						    buf,
						    sizeof(buf), /* in */
						    buf_out,
						    sizeof(buf_out), /* out */
						    &error_local)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "failed to read: %s",
				    error_local->message);
			return FALSE;
		}

		/* verify */
		if (memcmp(buf_out + 1, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk)) != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "failed to verify firmware for chunk %u, "
				    "address 0x%0x, length 0x%0x",
				    i,
				    (guint)fu_chunk_get_address(chk),
				    (guint)fu_chunk_get_data_sz(chk));
			return FALSE;
		}

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hughski_colorhug_device_write_firmware(FuDevice *device,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuHughskiColorhugDevice *self = FU_HUGHSKI_COLORHUG_DEVICE(device);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 19, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 35, NULL);

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* don't auto-boot firmware */
	if (!fu_hughski_colorhug_device_set_flash_success(self, FALSE, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* erase flash */
	if (!fu_hughski_colorhug_device_erase(self,
					      self->start_addr,
					      fu_firmware_get_size(firmware),
					      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write each block */
	chunks = fu_chunk_array_new_from_stream(stream,
						self->start_addr,
						FU_CHUNK_PAGESZ_NONE,
						CH_FLASH_TRANSFER_BLOCK_SIZE,
						error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_hughski_colorhug_device_write_blocks(self,
						     chunks,
						     fu_progress_get_child(progress),
						     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify each block */
	if (!fu_hughski_colorhug_device_verify_blocks(self,
						      chunks,
						      fu_progress_get_child(progress),
						      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_hughski_colorhug_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static void
fu_hughski_colorhug_device_init(FuHughskiColorhugDevice *self)
{
	/* this is the application code */
	self->start_addr = CH_EEPROM_ADDR_RUNCODE;
	fu_device_add_protocol(FU_DEVICE(self), "com.hughski.colorhug");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_register_private_flag(FU_DEVICE(self), FU_HUGHSKI_COLORHUG_DEVICE_FLAG_HALFSIZE);
	fu_usb_device_set_configuration(FU_USB_DEVICE(self), CH_USB_CONFIG);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), CH_USB_INTERFACE);
}

static void
fu_hughski_colorhug_device_class_init(FuHughskiColorhugDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_hughski_colorhug_device_write_firmware;
	device_class->attach = fu_hughski_colorhug_device_attach;
	device_class->detach = fu_hughski_colorhug_device_detach;
	device_class->reload = fu_hughski_colorhug_device_reload;
	device_class->setup = fu_hughski_colorhug_device_setup;
	device_class->probe = fu_hughski_colorhug_device_probe;
	device_class->set_progress = fu_hughski_colorhug_device_set_progress;
}
