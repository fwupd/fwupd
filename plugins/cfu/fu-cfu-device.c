/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-cfu-device.h"
#include "fu-cfu-module.h"

struct _FuCfuDevice {
	FuHidDevice parent_instance;
	guint8 protocol_version;
};

G_DEFINE_TYPE(FuCfuDevice, fu_cfu_device, FU_TYPE_HID_DEVICE)

#define FU_CFU_DEVICE_TIMEOUT 5000 /* ms */
#define FU_CFU_FEATURE_SIZE   60   /* bytes */

#define FU_CFU_CMD_GET_FIRMWARE_VERSION 0x00
#define FU_CFU_CMD_SEND_OFFER		0x00 // TODO

static void
fu_cfu_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCfuDevice *self = FU_CFU_DEVICE(device);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS(fu_cfu_device_parent_class)->to_string(device, idt, str);

	fu_string_append_kx(str, idt, "ProtocolVersion", self->protocol_version);
}

static gboolean
fu_cfu_device_write_offer(FuCfuDevice *self,
			  FuFirmware *firmware,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	guint8 buf2[FU_CFU_FEATURE_SIZE] = {0};
	g_autofree guint8 *buf_tmp = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* generate a offer blob */
	if (flags & FWUPD_INSTALL_FLAG_FORCE)
		fu_cfu_offer_set_force_ignore_version(FU_CFU_OFFER(firmware), TRUE);
	blob = fu_firmware_write(firmware, error);
	if (blob == NULL)
		return FALSE;

	/* send it to the hardware */
	buf = g_bytes_get_data(blob, &bufsz);
	buf_tmp = fu_memdup_safe(buf, bufsz, error);
	if (buf_tmp == NULL)
		return FALSE;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      FU_CFU_CMD_SEND_OFFER,
				      buf_tmp,
				      bufsz,
				      FU_CFU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		g_prefix_error(error, "failed to send offer: ");
		return FALSE;
	}
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      FU_CFU_CMD_SEND_OFFER,
				      buf2,
				      sizeof(buf2),
				      FU_CFU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		return FALSE;
	}
	g_debug("status:%s reject:%s",
		fu_cfu_device_offer_to_string(buf2[13]),
		fu_cfu_device_reject_to_string(buf2[9]));
	if (buf2[13] != FU_CFU_DEVICE_OFFER_ACCEPT) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "not supported: %s",
			    fu_cfu_device_offer_to_string(buf2[13]));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cfu_device_write_payload(FuCfuDevice *self,
			    FuFirmware *firmware,
			    FuProgress *progress,
			    GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	/* write each chunk */
	chunks = fu_firmware_get_chunks(firmware, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 databuf[60] = {0};
		guint8 buf2[60] = {0};

		/* flags */
		if (i == 0)
			databuf[0] = FU_CFU_DEVICE_FLAG_FIRST_BLOCK;
		else if (i == chunks->len - 1)
			databuf[0] = FU_CFU_DEVICE_FLAG_LAST_BLOCK;

		/* length */
		databuf[1] = fu_chunk_get_data_sz(chk);

		/* sequence number */
		if (!fu_memwrite_uint16_safe(databuf,
					     sizeof(databuf),
					     0x2,
					     i + 1,
					     G_LITTLE_ENDIAN,
					     error))
			return FALSE;

		/* address */
		if (!fu_memwrite_uint32_safe(databuf,
					     sizeof(databuf),
					     0x4,
					     fu_chunk_get_address(chk),
					     G_LITTLE_ENDIAN,
					     error))
			return FALSE;

		/* data */
		if (!fu_memcpy_safe(databuf,
				    sizeof(databuf),
				    0x8, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error)) {
			g_prefix_error(error, "memory copy for payload fail: ");
			return FALSE;
		}
		// send
		// revc
		if (buf2[5] != FU_CFU_DEVICE_STATUS_SUCCESS) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "failed to send chunk %u: %s",
				    i + 1,
				    fu_cfu_device_status_to_string(buf2[5]));
			return FALSE;
		}

		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cfu_device_write_firmware(FuDevice *device,
			     FuFirmware *firmware,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuCfuDevice *self = FU_CFU_DEVICE(device);
	g_autoptr(FuFirmware) fw_offer = NULL;
	g_autoptr(FuFirmware) fw_payload = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "offer");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "payload");

	/* send offer */
	fw_offer = fu_firmware_get_image_by_id(firmware, FU_FIRMWARE_ID_HEADER, error);
	if (fw_offer == NULL)
		return FALSE;
	if (!fu_cfu_device_write_offer(self,
				       fw_offer,
				       fu_progress_get_child(progress),
				       flags,
				       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send payload */
	fw_payload = fu_firmware_get_image_by_id(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (fw_payload == NULL)
		return FALSE;
	if (!fu_cfu_device_write_payload(self, fw_payload, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_cfu_device_setup(FuDevice *device, GError **error)
{
	FuCfuDevice *self = FU_CFU_DEVICE(device);
	guint8 buf[FU_CFU_FEATURE_SIZE] = {0};
	guint8 component_cnt = 0;
	guint8 tmp = 0;
	gsize offset = 0;
	g_autoptr(GHashTable) modules_by_cid = NULL;

	/* FuHidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_cfu_device_parent_class)->setup(device, error))
		return FALSE;

	/* get version */
	if (!fu_hid_device_get_report(FU_HID_DEVICE(device),
				      FU_CFU_CMD_GET_FIRMWARE_VERSION,
				      buf,
				      sizeof(buf),
				      FU_CFU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		return FALSE;
	}
	if (!fu_memread_uint8_safe(buf, sizeof(buf), 0x0, &component_cnt, error))
		return FALSE;
	if (!fu_memread_uint8_safe(buf, sizeof(buf), 0x3, &tmp, error))
		return FALSE;
	self->protocol_version = tmp & 0b1111;

	/* keep track of all modules so we can work out which are dual bank */
	modules_by_cid = g_hash_table_new(g_int_hash, g_int_equal);

	/* read each component module version */
	offset += 4;
	for (guint i = 0; i < component_cnt; i++) {
		g_autoptr(FuCfuModule) module = fu_cfu_module_new(device);
		FuCfuModule *module_tmp;

		if (!fu_cfu_module_setup(module, buf, sizeof(buf), offset, error))
			return FALSE;
		fu_device_add_child(device, FU_DEVICE(module));

		/* same module already exists, so mark both as being dual bank */
		module_tmp =
		    g_hash_table_lookup(modules_by_cid,
					GINT_TO_POINTER(fu_cfu_module_get_component_id(module)));
		if (module_tmp != NULL) {
			fu_device_add_flag(FU_DEVICE(module), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
			fu_device_add_flag(FU_DEVICE(module_tmp), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
		} else {
			g_hash_table_insert(modules_by_cid,
					    GINT_TO_POINTER(fu_cfu_module_get_component_id(module)),
					    module);
		}

		/* done */
		offset += 0x8;
	}

	/* success */
	return TRUE;
}

static void
fu_cfu_device_init(FuCfuDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
}

static void
fu_cfu_device_class_init(FuCfuDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_cfu_device_setup;
	klass_device->to_string = fu_cfu_device_to_string;
	klass_device->write_firmware = fu_cfu_device_write_firmware;
}
