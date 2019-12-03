/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-synaprom-common.h"
#include "fu-synaprom-config.h"
#include "fu-synaprom-device.h"
#include "fu-synaprom-firmware.h"

struct _FuSynapromDevice {
	FuUsbDevice		 parent_instance;
	guint8			 vmajor;
	guint8			 vminor;
};

/* vendor-specific USB control requets to write DFT word (Hayes) */
#define FU_SYNAPROM_USB_CTRLREQUEST_VENDOR_WRITEDFT	21

/* endpoint addresses for command and fingerprint data */
#define FU_SYNAPROM_USB_REQUEST_EP			0x01
#define FU_SYNAPROM_USB_REPLY_EP			0x81
#define FU_SYNAPROM_USB_FINGERPRINT_EP			0x82
#define FU_SYNAPROM_USB_INTERRUPT_EP			0x83

/* le */
typedef struct __attribute__((packed)) {
	guint16		 status;
} FuSynapromReplyGeneric;

/* le */
typedef struct __attribute__((packed)) {
	guint16		 status;
	guint32		 buildtime;	/* Unix-style build time */
	guint32		 buildnum;	/* build number */
	guint8		 vmajor;	/* major version */
	guint8		 vminor;	/* minor version */
	guint8		 target;	/* target, e.g. VCSFW_TARGET_ROM */
	guint8		 product;	/* product, e.g.  VCSFW_PRODUCT_FALCON */
	guint8		 siliconrev;	/* silicon revision */
	guint8		 formalrel;	/* boolean: non-zero -> formal release */
	guint8		 platform;	/* Platform (PCB) revision */
	guint8		 patch;		/* patch level */
	guint8		 serial_number[6]; /* 48-bit Serial Number */
	guint8		 security[2];	/* bytes 0 and 1 of OTP */
	guint32		 patchsig;	/* opaque patch signature */
	guint8		 iface;		/* interface type, see below */
	guint8		 otpsig[3];	/* OTP Patch Signature */
	guint16		 otpspare1;	/* spare space */
	guint8		 reserved;	/* reserved byte */
	guint8		 device_type;	/* device type */
} FuSynapromReplyGetVersion;

/* the following bits describe security options in
** FuSynapromReplyGetVersion::security[1] bit-field */
#define FU_SYNAPROM_SECURITY1_PROD_SENSOR		(1 << 5)

G_DEFINE_TYPE (FuSynapromDevice, fu_synaprom_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_synaprom_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	if (!g_usb_device_claim_interface (usb_device, 0x0,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_synaprom_device_cmd_send (FuSynapromDevice *device,
			     GByteArray *request,
			     GByteArray *reply,
			     guint timeout_ms,
			     GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	gboolean ret;
	gsize actual_len = 0;

	if (g_getenv ("FWUPD_SYNAPROM_VERBOSE") != NULL) {
		fu_common_dump_full (G_LOG_DOMAIN, "REQST",
				     request->data, request->len, 16,
				     FU_DUMP_FLAGS_SHOW_ADDRESSES);
	}
	ret = g_usb_device_bulk_transfer (usb_device,
					  FU_SYNAPROM_USB_REQUEST_EP,
					  request->data,
					  request->len,
					  &actual_len,
					  timeout_ms, NULL, error);
	if (!ret) {
		g_prefix_error (error, "failed to request: ");
		return FALSE;
	}
	if (actual_len < request->len) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "only sent 0x%04x of 0x%04x",
			     (guint) actual_len, request->len);
		return FALSE;
	}

	ret = g_usb_device_bulk_transfer (usb_device,
					  FU_SYNAPROM_USB_REPLY_EP,
					  reply->data,
					  reply->len,
					  NULL, /* allowed to return short read */
					  timeout_ms, NULL, error);
	if (!ret) {
		g_prefix_error (error, "failed to reply: ");
		return FALSE;
	}
	if (g_getenv ("FWUPD_SYNAPROM_VERBOSE") != NULL) {
		fu_common_dump_full (G_LOG_DOMAIN, "REPLY",
				     reply->data, actual_len, 16,
				     FU_DUMP_FLAGS_SHOW_ADDRESSES);
	}

	/* parse as FuSynapromReplyGeneric */
	if (reply->len >= sizeof(FuSynapromReplyGeneric)) {
		FuSynapromReplyGeneric *hdr = (FuSynapromReplyGeneric *) reply->data;
		return fu_synaprom_error_from_status (GUINT16_FROM_LE(hdr->status), error);
	}

	/* success */
	return TRUE;
}

void
fu_synaprom_device_set_version (FuSynapromDevice *self,
				guint8 vmajor, guint8 vminor, guint32 buildnum)
{
	g_autofree gchar *str = NULL;

	/* set display version */
	str = g_strdup_printf ("%02u.%02u.%u", vmajor, vminor, buildnum);
	fu_device_set_version (FU_DEVICE (self), str, FWUPD_VERSION_FORMAT_TRIPLET);

	/* we need this for checking the firmware compatibility later */
	self->vmajor = vmajor;
	self->vminor = vminor;
}

static void
fu_synaprom_device_set_serial_number (FuSynapromDevice *self, guint64 serial_number)
{
	g_autofree gchar *str = NULL;
	str = g_strdup_printf ("%" G_GUINT64_FORMAT, serial_number);
	fu_device_set_serial (FU_DEVICE (self), str);
}

static gboolean
fu_synaprom_device_setup (FuDevice *device, GError **error)
{
	FuSynapromDevice *self = FU_SYNAPROM_DEVICE (device);
	FuSynapromReplyGetVersion pkt;
	guint32 product;
	guint64 serial_number = 0;
	g_autoptr(GByteArray) request = NULL;
	g_autoptr(GByteArray) reply = NULL;

	/* get version */
	request = fu_synaprom_request_new (FU_SYNAPROM_CMD_GET_VERSION, NULL, 0);
	reply = fu_synaprom_reply_new (sizeof(FuSynapromReplyGetVersion));
	if (!fu_synaprom_device_cmd_send (self, request, reply, 250, error)) {
		g_prefix_error (error, "failed to get version: ");
		return FALSE;
	}
	memcpy (&pkt, reply->data, sizeof(pkt));
	product = GUINT32_FROM_LE(pkt.product);
	g_debug ("product ID is %u, version=%u.%u, buildnum=%u prod=%i",
		 product, pkt.vmajor, pkt.vminor, GUINT32_FROM_LE(pkt.buildnum),
		 pkt.security[1] & FU_SYNAPROM_SECURITY1_PROD_SENSOR);
	fu_synaprom_device_set_version (self, pkt.vmajor, pkt.vminor,
					GUINT32_FROM_LE(pkt.buildnum));

	/* get serial number */
	memcpy (&serial_number, pkt.serial_number, sizeof(pkt.serial_number));
	fu_synaprom_device_set_serial_number (self, serial_number);

	/* check device type */
	if (product == FU_SYNAPROM_PRODUCT_PROMETHEUS) {
		fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else if (product == FU_SYNAPROM_PRODUCT_PROMETHEUSPBL ||
		   product == FU_SYNAPROM_PRODUCT_PROMETHEUSMSBL) {
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "device %u is not supported by this plugin",
			     product);
		return FALSE;
	}

	/* add updatable config child, if this is a production sensor */
	if (fu_device_get_children (device)->len == 0 &&
	    !fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER) &&
	    pkt.security[1] & FU_SYNAPROM_SECURITY1_PROD_SENSOR) {
		g_autoptr(FuSynapromConfig) cfg = fu_synaprom_config_new (self);
		if (!fu_device_setup (FU_DEVICE (cfg), error)) {
			g_prefix_error (error, "failed to get config version: ");
			return FALSE;
		}
		fu_device_add_child (FU_DEVICE (device), FU_DEVICE (cfg));
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaprom_device_cmd_download_chunk (FuSynapromDevice *device,
				       const GByteArray *chunk,
				       GError **error)
{
	g_autoptr(GByteArray) request = NULL;
	g_autoptr(GByteArray) reply = NULL;
	request = fu_synaprom_request_new (FU_SYNAPROM_CMD_BOOTLDR_PATCH,
					   chunk->data, chunk->len);
	reply = fu_synaprom_reply_new (sizeof(FuSynapromReplyGeneric));
	return fu_synaprom_device_cmd_send (device, request, reply, 20000, error);
}

FuFirmware *
fu_synaprom_device_prepare_fw (FuDevice *device,
			       GBytes *fw,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuSynapromFirmwareMfwHeader hdr;
	guint32 product;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuFirmware) firmware = fu_synaprom_firmware_new ();

	/* parse the firmware */
	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;

	/* check the update header product and version */
	blob = fu_firmware_get_image_by_id_bytes (firmware, "mfw-update-header", error);
	if (blob == NULL)
		return NULL;
	if (g_bytes_get_size (blob) != sizeof(hdr)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "MFW metadata is invalid");
		return NULL;
	}
	memcpy (&hdr, g_bytes_get_data (blob, NULL), sizeof(hdr));
	product = GUINT32_FROM_LE(hdr.product);
	if (product != FU_SYNAPROM_PRODUCT_PROMETHEUS) {
		if (flags & FWUPD_INSTALL_FLAG_FORCE) {
			g_warning ("MFW metadata not compatible, "
				   "got 0x%02x expected 0x%02x",
				   product, (guint) FU_SYNAPROM_PRODUCT_PROMETHEUS);
		} else {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "MFW metadata not compatible, "
				     "got 0x%02x expected 0x%02x",
				     product, (guint) FU_SYNAPROM_PRODUCT_PROMETHEUS);
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer (&firmware);
}

gboolean
fu_synaprom_device_write_fw (FuSynapromDevice *self, GBytes *fw, GError **error)
{
	const guint8 *buf;
	gsize sz = 0;

	/* write chunks */
	fu_device_set_progress (FU_DEVICE (self), 10);
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
	buf = g_bytes_get_data (fw, &sz);
	while (sz != 0) {
		guint32 chunksz;
		g_autoptr(GByteArray) chunk = g_byte_array_new ();

		/* get chunk size */
		if (sz < sizeof(guint32)) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_SUPPORTED,
					     "No enough data for patch len");
			return FALSE;
		}
		memcpy (&chunksz, buf, sizeof(guint32));
		buf += sizeof(guint32);
		sz -= sizeof(guint32);
		if (sz < chunksz) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_SUPPORTED,
					     "No enough data for patch chunk");
			return FALSE;
		}

		/* download chunk */
		g_byte_array_append (chunk, buf, chunksz);
		if (!fu_synaprom_device_cmd_download_chunk (self, chunk, error))
			return FALSE;

		/* next chunk */
		buf += chunksz;
		sz -= chunksz;
	}

	/* success! */
	fu_device_set_progress (FU_DEVICE (self), 100);
	return TRUE;
}

static gboolean
fu_synaprom_device_write_firmware (FuDevice *device,
				   FuFirmware *firmware,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuSynapromDevice *self = FU_SYNAPROM_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;

	/* get default image */
	fw = fu_firmware_get_image_by_id_bytes (firmware, "mfw-update-payload", error);
	if (fw == NULL)
		return FALSE;

	return fu_synaprom_device_write_fw (self, fw, error);
}

static gboolean
fu_synaprom_device_attach (FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	gboolean ret;
	gsize actual_len = 0;
	guint8 data[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	ret = g_usb_device_control_transfer (usb_device,
					     G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					     G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					     G_USB_DEVICE_RECIPIENT_DEVICE,
					     FU_SYNAPROM_USB_CTRLREQUEST_VENDOR_WRITEDFT,
					     0x0000, 0x0000,
					     data, sizeof(data), &actual_len,
					     2000, NULL, error);
	if (!ret)
		return FALSE;
	if (actual_len != sizeof(data)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "only sent 0x%04x of 0x%04x",
			     (guint) actual_len, (guint) sizeof(data));
		return FALSE;
	}
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!g_usb_device_reset (usb_device, error)) {
		g_prefix_error (error, "failed to force-reset device: ");
		return FALSE;
	}
	fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_synaprom_device_detach (FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	gboolean ret;
	gsize actual_len = 0;
	guint8 data[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };

	ret = g_usb_device_control_transfer (usb_device,
					     G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					     G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					     G_USB_DEVICE_RECIPIENT_DEVICE,
					     FU_SYNAPROM_USB_CTRLREQUEST_VENDOR_WRITEDFT,
					     0x0000, 0x0000,
					     data, sizeof(data), &actual_len,
					     2000, NULL, error);
	if (!ret)
		return FALSE;
	if (actual_len != sizeof(data)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "only sent 0x%04x of 0x%04x",
			     (guint) actual_len, (guint) sizeof(data));
		return FALSE;
	}
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!g_usb_device_reset (usb_device, error)) {
		g_prefix_error (error, "failed to force-reset device: ");
		return FALSE;
	}
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static void
fu_synaprom_device_init (FuSynapromDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY);
	fu_device_set_protocol (FU_DEVICE (self), "com.synaptics.prometheus");
	fu_device_set_remove_delay (FU_DEVICE (self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_name (FU_DEVICE (self), "Prometheus");
	fu_device_set_summary (FU_DEVICE (self), "Fingerprint reader");
	fu_device_set_vendor (FU_DEVICE (self), "Synaptics");
	fu_device_add_icon (FU_DEVICE (self), "touchpad-disabled");
}

static void
fu_synaprom_device_class_init (FuSynapromDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_synaprom_device_write_firmware;
	klass_device->prepare_firmware = fu_synaprom_device_prepare_fw;
	klass_device->setup = fu_synaprom_device_setup;
	klass_device->reload = fu_synaprom_device_setup;
	klass_device->attach = fu_synaprom_device_attach;
	klass_device->detach = fu_synaprom_device_detach;
	klass_usb_device->open = fu_synaprom_device_open;
}

FuSynapromDevice *
fu_synaprom_device_new (FuUsbDevice *device)
{
	FuSynapromDevice *self;
	self = g_object_new (FU_TYPE_SYNAPROM_DEVICE, NULL);
	if (device != NULL)
		fu_device_incorporate (FU_DEVICE (self), FU_DEVICE (device));
	return FU_SYNAPROM_DEVICE (self);
}
