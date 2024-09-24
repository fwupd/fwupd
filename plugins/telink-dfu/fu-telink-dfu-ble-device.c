/*
 * Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-telink-dfu-archive.h"
#include "fu-telink-dfu-ble-device.h"
#include "fu-telink-dfu-common.h"
#include "fu-telink-dfu-struct.h"

struct _FuTelinkDfuBleDevice {
	FuBluezDevice parent_instance;
};

G_DEFINE_TYPE(FuTelinkDfuBleDevice, fu_telink_dfu_ble_device, FU_TYPE_BLUEZ_DEVICE)

#define FU_TELINK_DFU_HID_DEVICE_START_ADDR 0x5000

#define FU_TELINK_DFU_BLE_DEVICE_UUID_OTA "00010203-0405-0607-0809-0a0b0c0d2b12"

static FuStructTelinkDfuBlePkt *
fu_telink_dfu_ble_device_create_packet(guint16 preamble, const guint8 *buf, gsize bufsz)
{
	FuStructTelinkDfuBlePkt *pkt = fu_struct_telink_dfu_ble_pkt_new();
	fu_struct_telink_dfu_ble_pkt_set_preamble(pkt, preamble);
	if (buf != NULL)
		fu_struct_telink_dfu_ble_pkt_set_payload(pkt, buf, bufsz, NULL);
	fu_struct_telink_dfu_ble_pkt_set_crc(
	    pkt,
	    ~fu_crc16(FU_CRC_KIND_B16_USB, pkt->data, pkt->len - 2));
	return pkt;
}

static gboolean
fu_telink_dfu_ble_device_write_blocks(FuTelinkDfuBleDevice *self,
				      FuChunkArray *chunks,
				      FuProgress *progress,
				      GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(FuStructTelinkDfuBlePkt) pkt = NULL;

		/* send chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		pkt = fu_telink_dfu_ble_device_create_packet((guint16)i,
							     fu_chunk_get_data(chk),
							     fu_chunk_get_data_sz(chk));
		if (!fu_bluez_device_write(FU_BLUEZ_DEVICE(self),
					   FU_TELINK_DFU_BLE_DEVICE_UUID_OTA,
					   pkt,
					   error))
			return FALSE;
		fu_device_sleep(FU_DEVICE(self), 5);

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	fu_device_sleep(FU_DEVICE(self), 5);
	return TRUE;
}

static gboolean
fu_telink_dfu_ble_device_ota_start(FuTelinkDfuBleDevice *self, GError **error)
{
	g_autoptr(FuStructTelinkDfuBlePkt) pkt = NULL;

	pkt = fu_telink_dfu_ble_device_create_packet(FU_TELINK_DFU_CMD_OTA_START, NULL, 0);
	if (!fu_bluez_device_write(FU_BLUEZ_DEVICE(self),
				   FU_TELINK_DFU_BLE_DEVICE_UUID_OTA,
				   pkt,
				   error))
		return FALSE;

	/* success */
	fu_device_sleep(FU_DEVICE(self), 5);
	return TRUE;
}

static gboolean
fu_telink_dfu_ble_device_ota_stop(FuTelinkDfuBleDevice *self, guint number_chunks, GError **error)
{
	guint8 pkt_stop_data[4] = {0};
	g_autoptr(FuStructTelinkDfuBlePkt) pkt = NULL;

	/* last data packet index */
	fu_memwrite_uint16(pkt_stop_data, (number_chunks >> 4) - 1, G_LITTLE_ENDIAN);
	pkt_stop_data[2] = ~pkt_stop_data[0];
	pkt_stop_data[3] = ~pkt_stop_data[1];
	pkt = fu_telink_dfu_ble_device_create_packet(FU_TELINK_DFU_CMD_OTA_END,
						     pkt_stop_data,
						     sizeof(pkt_stop_data));
	if (!fu_bluez_device_write(FU_BLUEZ_DEVICE(self),
				   FU_TELINK_DFU_BLE_DEVICE_UUID_OTA,
				   pkt,
				   error))
		return FALSE;

	/* success */
	fu_device_sleep(FU_DEVICE(self), 20000);
	return TRUE;
}

static gboolean
fu_telink_dfu_ble_device_write_blob(FuTelinkDfuBleDevice *self,
				    GBytes *blob,
				    FuProgress *progress,
				    GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuStructTelinkDfuBlePkt) pkt = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "ota-start");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 70, "ota-data");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 29, "ota-stop");

	/* ensure we can get the current version */
	pkt = fu_telink_dfu_ble_device_create_packet(FU_TELINK_DFU_CMD_OTA_FW_VERSION, NULL, 0);
	if (!fu_bluez_device_write(FU_BLUEZ_DEVICE(self),
				   FU_TELINK_DFU_BLE_DEVICE_UUID_OTA,
				   pkt,
				   error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), 5);

	/* OTA start command */
	if (!fu_telink_dfu_ble_device_ota_start(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* OTA firmware data */
	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_TELINK_DFU_HID_DEVICE_START_ADDR,
					       FU_STRUCT_TELINK_DFU_BLE_PKT_SIZE_PAYLOAD);
	if (!fu_telink_dfu_ble_device_write_blocks(self,
						   chunks,
						   fu_progress_get_child(progress),
						   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* OTA stop command */
	if (!fu_telink_dfu_ble_device_ota_stop(self, fu_chunk_array_length(chunks), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_telink_dfu_ble_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuTelinkDfuBleDevice *self = FU_TELINK_DFU_BLE_DEVICE(device);
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	archive = fu_archive_new_stream(stream, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;
	blob = fu_archive_lookup_by_fn(archive, "firmware.bin", error);
	if (blob == NULL)
		return FALSE;
	return fu_telink_dfu_ble_device_write_blob(self, blob, progress, error);
}

static void
fu_telink_dfu_ble_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_telink_dfu_ble_device_init(FuTelinkDfuBleDevice *self)
{
	fu_device_set_vendor(FU_DEVICE(self), "Telink");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_remove_delay(FU_DEVICE(self), 10000); /* ms */
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_TELINK_DFU_ARCHIVE);
	fu_device_add_protocol(FU_DEVICE(self), "com.telink.dfu");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
}

static void
fu_telink_dfu_ble_device_class_init(FuTelinkDfuBleDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_telink_dfu_ble_device_write_firmware;
	device_class->set_progress = fu_telink_dfu_ble_device_set_progress;
}
