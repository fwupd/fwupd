/*
 * Copyright (C) 2023 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-algoltek-usb-common.h"
#include "fu-algoltek-usb-device.h"
#include "fu-algoltek-usb-firmware.h"
// #include "fu-algoltek-usb-struct.h"

struct _FuAlgoltekUsbDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuAlgoltekUsbDevice, fu_algoltek_usb_device, FU_TYPE_USB_DEVICE)

static void
fu_algoltek_usb_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FU_DEVICE_CLASS(fu_algoltek_usb_device_parent_class)->to_string(device, idt, str);
}

static guint8
checkSum(GByteArray *byteArray)
{
	guint8 result = 0;
	for (guint32 i = 0; i < byteArray->len; i++) {
		result += (guint8)(byteArray->data[i] & 0xff);
	}
	result = (guint8)(~result + 1);
	return result;
}

static GByteArray *
fu_algoltek_device_CMD02(FuAlgoltekUsbDevice *self, int address, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* first byte is length */
	fu_byte_array_set_size(buf, 11, 0x0);
	buf->data[0] = 5;
	buf->data[1] = ALGOLTEK_USB_REQUEST_CMD02;
	buf->data[2] = ((address >> 8) & 0xFF);
	buf->data[3] = (address & 0xFF);
	buf->data[10] = checkSum(buf);
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   ALGOLTEK_USB_REQUEST_CMD02,
					   address, /* value */
					   0xFFFF,  /* index */
					   buf->data,
					   buf->len,
					   NULL,
					   ALGOLTEK_DEVICE_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "failed to contact device: ");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&buf);
}

static GByteArray *
fu_algoltek_device_CMD01(FuAlgoltekUsbDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) versionData = g_byte_array_new();
	gsize actual_length = 0;

	/* first byte is length */
	fu_byte_array_set_size(buf, 64, 0x0);
	buf->data[0] = 3;
	buf->data[1] = ALGOLTEK_USB_REQUEST_CMD01;
	buf->data[63] = checkSum(buf);
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   ALGOLTEK_USB_REQUEST_CMD01,
					   0xFFFF, /* value */
					   0xFFFF, /* index */
					   buf->data,
					   buf->len,
					   &actual_length,
					   ALGOLTEK_DEVICE_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "failed to contact device: ");
		return NULL;
	}

	if (actual_length != buf->len) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "got 0x%x but requested 0x%x",
			    (guint)actual_length,
			    (guint)buf->len);
		return NULL;
	}

	/* Remove cmd and length */
	for (guint32 i = 2; i < buf->len; i++) {
		if (buf->data[i] < 128)
			fu_byte_array_append_uint8(versionData, buf->data[i]);
	}
	/* success */
	return g_steal_pointer(&versionData);
}

static guint
fu_algoltek_readout_info_int(GByteArray *backData)
{
	int backDataAddress = (backData->data[2] << 8) | backData->data[3];
	int backDataValue;
	if (backDataAddress == 0x860C)
		backDataValue = backData->data[0];
	else
		backDataValue = (backData->data[4] << 8) | backData->data[5];
	return backDataValue;
}

static gboolean
fu_algoltek_device_CMD03(FuAlgoltekUsbDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_set_size(buf, 11, 0x0);
	buf->data[0] = 3;
	buf->data[1] = ALGOLTEK_USB_REQUEST_CMD03;
	buf->data[10] = checkSum(buf);
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   ALGOLTEK_USB_REQUEST_CMD03,
					   0, /* value */
					   0, /* index */
					   buf->data,
					   buf->data[0],
					   NULL,
					   ALGOLTEK_DEVICE_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "failed to contact device: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_algoltek_device_CMD04(FuAlgoltekUsbDevice *self, guint8 number, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GByteArray) buf = g_byte_array_new();
	fu_byte_array_set_size(buf, 11, 0x0);
	buf->data[0] = 4;
	buf->data[1] = ALGOLTEK_USB_REQUEST_CMD04;
	buf->data[2] = number;
	buf->data[10] = checkSum(buf);
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   ALGOLTEK_USB_REQUEST_CMD04,
					   0, /* value */
					   0, /* index */
					   buf->data,
					   buf->data[0],
					   NULL,
					   ALGOLTEK_DEVICE_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "failed to contact device: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_algoltek_device_CMD05(FuAlgoltekUsbDevice *self, int address, int inputValue, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GByteArray) buf = g_byte_array_new();
	fu_byte_array_set_size(buf, 11, 0x0);
	buf->data[0] = 7;
	buf->data[1] = ALGOLTEK_USB_REQUEST_CMD05;
	buf->data[2] = ((address >> 8) & 0xFF);
	buf->data[3] = (address & 0xFF);
	buf->data[4] = ((inputValue >> 8) & 0xFF);
	buf->data[5] = (inputValue & 0xFF);
	buf->data[10] = checkSum(buf);
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   ALGOLTEK_USB_REQUEST_CMD05,
					   0, /* value */
					   0, /* index */
					   buf->data,
					   buf->data[0],
					   NULL,
					   ALGOLTEK_DEVICE_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "failed to contact device: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_algoltek_device_CMD06(FuAlgoltekUsbDevice *self,
			 GByteArray *ISPData,
			 int address,
			 GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GByteArray) partIspData = g_byte_array_new();
	guint restTransferDataSize = ISPData->len;
	guint dataAddress = 0;
	guint startIndex = 0;
	guint transferDataSize = 0;
	guint basicDataSize = 5;
	guint maxPktSize = 64;

	while (restTransferDataSize > 0) {
		dataAddress = address + startIndex;
		if (restTransferDataSize > (maxPktSize - basicDataSize)) {
			transferDataSize = maxPktSize - basicDataSize;
		} else {
			transferDataSize = restTransferDataSize;
		}
		fu_byte_array_set_size(partIspData, transferDataSize + basicDataSize, 0);
		partIspData->data[0] = (guint8)(transferDataSize + basicDataSize);
		partIspData->data[1] = ALGOLTEK_USB_REQUEST_CMD06;
		partIspData->data[2] = (guint8)((dataAddress >> 8) & 0xFF);
		partIspData->data[3] = (guint8)(dataAddress & 0xFF);

		for (guint i = 0; i < transferDataSize; i++) {
			partIspData->data[4 + i] = ISPData->data[i + startIndex];
		}

		partIspData->data[partIspData->len - 1] = checkSum(partIspData);

		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_INTERFACE,
						   ALGOLTEK_USB_REQUEST_CMD06,
						   0, /* value */
						   0, /* index */
						   partIspData->data,
						   partIspData->data[0],
						   NULL,
						   ALGOLTEK_DEVICE_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error, "failed to contact device: ");
			return FALSE;
		}

		startIndex += transferDataSize;
		restTransferDataSize -= transferDataSize;
	}
	return TRUE;
}

static gboolean
fu_algoltek_device_CMD07(FuAlgoltekUsbDevice *self, int address, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_set_size(buf, 11, 0x0);
	buf->data[0] = 5;
	buf->data[1] = ALGOLTEK_USB_REQUEST_CMD07;
	buf->data[2] = ((address >> 8) & 0xFF);
	buf->data[3] = (address & 0xFF);
	buf->data[10] = checkSum(buf);
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   ALGOLTEK_USB_REQUEST_CMD07,
					   0, /* value */
					   0, /* index */
					   buf->data,
					   buf->data[0],
					   NULL,
					   ALGOLTEK_DEVICE_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "failed to contact device: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_algoltek_device_CMD08(FuAlgoltekUsbDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_set_size(buf, 2, 0x0);
	buf->data[0] = 0;
	buf->data[1] = 0;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_INTERFACE,
					   ALGOLTEK_USB_REQUEST_CMD08,
					   0, /* value */
					   0, /* index */
					   buf->data,
					   buf->len,
					   NULL,
					   ALGOLTEK_DEVICE_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "failed to contact device: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_algoltek_device_status_check(FuAlgoltekUsbDevice *self, GError **error)
{
	g_autoptr(GByteArray) readoutCheck = g_byte_array_new();
	guint retryTimes = 0;
	guint checkResult;
Retry:
	readoutCheck = fu_algoltek_device_CMD02(self, 0x860C, error);
	if (readoutCheck == NULL)
		return FALSE;
	checkResult = fu_algoltek_readout_info_int(readoutCheck);
	switch (checkResult) {
	case Result_PASS:
		break;
	case Result_ERROR:
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "Check flashwrite status return failed...");
		return FALSE;
	default:
		retryTimes++;
		if (retryTimes < 10)
			goto Retry;
		else
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_algoltek_device_CMD09(FuAlgoltekUsbDevice *self,
			 GByteArray *firmwareData,
			 FuProgress *progress,
			 GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GByteArray) transferParameter = g_byte_array_new();
	g_autoptr(GByteArray) partFirmware = g_byte_array_new();
	guint restTransferDataSize = firmwareData->len;
	guint dataAddress = 0;
	guint startIndex = 0;
	guint maxPktSize = 64;
	guint transferDataSize = 0;
	guint countCheck = 0;
	guint value;
	guint index;
	gboolean result;

	while (restTransferDataSize > 0) {
		countCheck++;
		dataAddress = startIndex;
		if (restTransferDataSize > maxPktSize)
			transferDataSize = maxPktSize;
		else
			transferDataSize = restTransferDataSize;

		fu_byte_array_set_size(transferParameter, 4, 0);

		if (countCheck == 256 / maxPktSize)
			transferParameter->data[0] = 1;
		else
			transferParameter->data[0] = 0;

		transferParameter->data[1] = (guint8)((dataAddress >> 16) & 0xFF);
		transferParameter->data[2] = (guint8)((dataAddress >> 8) & 0xFF);
		transferParameter->data[3] = (guint8)(dataAddress & 0xFF);

		value = (transferParameter->data[0] << 8) | (transferParameter->data[1]);
		index = (transferParameter->data[2] << 8) | (transferParameter->data[3]);

		fu_byte_array_set_size(partFirmware, transferDataSize, 0);

		for (guint i = 0; i < transferDataSize; i++)
			partFirmware->data[i] = firmwareData->data[i + startIndex];

		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_INTERFACE,
						   ALGOLTEK_USB_REQUEST_CMD09,
						   value, /* value */
						   index, /* index */
						   partFirmware->data,
						   partFirmware->len,
						   NULL,
						   ALGOLTEK_DEVICE_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error, "failed to contact device: ");
			return FALSE;
		}

		startIndex += transferDataSize;
		restTransferDataSize -= transferDataSize;
		if (countCheck == (256 / maxPktSize) || restTransferDataSize == 0) {
			countCheck = 0;
			result = fu_algoltek_device_status_check(self, error);
			if (!result)
				return FALSE;
		}
		fu_progress_set_percentage_full(progress, startIndex, firmwareData->len);
	}
	return TRUE;
}

static gboolean
fu_algoltek_usb_device_setup(FuDevice *device, GError **error)
{
	FuAlgoltekUsbDevice *self = FU_ALGOLTEK_USB_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	g_autoptr(GByteArray) versionData = NULL;
	g_autofree gchar *version_str = NULL;

	/* UsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_algoltek_usb_device_parent_class)->setup(device, error))
		return FALSE;

	versionData = fu_algoltek_device_CMD01(self, error);
	version_str = g_strdup_printf("%s", versionData->data);

	g_assert(self != NULL);
	g_assert(usb_device != NULL);
	fu_device_set_version(device, version_str);

	/* success */
	return TRUE;
}

static gboolean
fu_algoltek_usb_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuAlgoltekUsbDevice *self = FU_ALGOLTEK_USB_DEVICE(device);
	g_autoptr(GBytes) blob_ISP = NULL;
	g_autoptr(GBytes) blob_payload = NULL;
	g_autoptr(GByteArray) fw_ISP = g_byte_array_new();
	g_autoptr(GByteArray) fw_payload = g_byte_array_new();
	gboolean result;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 15, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, NULL);

	result = fu_algoltek_device_CMD03(self, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x09 : ");
		return FALSE;
	}

	result = fu_algoltek_device_CMD04(self, 2, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x20_2 : ");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), 900);

	result = fu_algoltek_device_CMD05(self, 0x80AD, 0, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x07_80AD : ");
		return FALSE;
	}

	result = fu_algoltek_device_CMD05(self, 0x80C0, 0, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x07_80C0 : ");
		return FALSE;
	}

	result = fu_algoltek_device_CMD05(self, 0x80C9, 0, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x07_80C9 : ");
		return FALSE;
	}

	result = fu_algoltek_device_CMD05(self, 0x80D1, 0, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x07_80D1 : ");
		return FALSE;
	}

	result = fu_algoltek_device_CMD05(self, 0x80D9, 0, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x07_80D9 : ");
		return FALSE;
	}

	result = fu_algoltek_device_CMD05(self, 0x80E1, 0, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x07_80E1 : ");
		return FALSE;
	}

	result = fu_algoltek_device_CMD05(self, 0x80E9, 0, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x07_80E9 : ");
		return FALSE;
	}

	result = fu_algoltek_device_CMD04(self, 0, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x20_0 : ");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), 500);

	/* get ISP image */
	blob_ISP = fu_firmware_get_image_by_id_bytes(firmware, "ISP", error);
	if (blob_ISP == NULL)
		return FALSE;
	fu_byte_array_append_bytes(fw_ISP, blob_ISP);
	result = fu_algoltek_device_CMD06(self, fw_ISP, 0x2000, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x13 : ");
		return FALSE;
	}

	result = fu_algoltek_device_CMD07(self, 0x2000, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x1D_2000 : ");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), 1000);

	result = fu_algoltek_device_CMD08(self, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x19 : ");
		return FALSE;
	}

	fu_device_sleep(FU_DEVICE(self), 500);

	/* get payload image */
	blob_payload = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (blob_payload == NULL)
		return FALSE;
	fu_byte_array_append_bytes(fw_payload, blob_payload);
	result = fu_algoltek_device_CMD09(self, fw_payload, progress, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x10 : ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	result = fu_algoltek_device_CMD04(self, 1, error);
	if (!result) {
		g_prefix_error(error, "Error Code 0x20_1 : ");
		return FALSE;
	}
	/* success! */
	return TRUE;
}

static void
fu_algoltek_usb_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_algoltek_usb_device_init(FuAlgoltekUsbDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.algoltek.usb");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ALGOLTEK_USB_FIRMWARE);
}

static void
fu_algoltek_usb_device_class_init(FuAlgoltekUsbDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_algoltek_usb_device_to_string;
	klass_device->setup = fu_algoltek_usb_device_setup;
	klass_device->write_firmware = fu_algoltek_usb_device_write_firmware;
	klass_device->set_progress = fu_algoltek_usb_device_set_progress;
}
