/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-synaprom-common.h"
#include "fu-synaprom-config.h"
#include "fu-synaprom-device.h"
#include "fu-synaprom-firmware.h"

struct _FuSynapromDevice {
	FuUsbDevice parent_instance;
	guint8 vmajor;
	guint8 vminor;
	guint32 product_type;
};

/* vendor-specific USB control requests to write DFT word (Hayes) */
#define FU_SYNAPROM_USB_CTRLREQUEST_VENDOR_WRITEDFT 21

/* endpoint addresses for command and fingerprint data */
#define FU_SYNAPROM_USB_REQUEST_EP     0x01
#define FU_SYNAPROM_USB_REPLY_EP       0x81
#define FU_SYNAPROM_USB_FINGERPRINT_EP 0x82
#define FU_SYNAPROM_USB_INTERRUPT_EP   0x83

/* le */
typedef struct __attribute__((packed)) { /* nocheck:blocked */
	guint16 status;
} FuSynapromReplyGeneric;

/* le */
typedef struct __attribute__((packed)) { /* nocheck:blocked */
	guint16 status;
	guint32 buildtime;	 /* Unix-style build time */
	guint32 buildnum;	 /* build number */
	guint8 vmajor;		 /* major version */
	guint8 vminor;		 /* minor version */
	guint8 target;		 /* target, e.g. VCSFW_TARGET_ROM */
	guint8 product;		 /* product, e.g.  VCSFW_PRODUCT_FALCON */
	guint8 siliconrev;	 /* silicon revision */
	guint8 formalrel;	 /* boolean: non-zero -> formal release */
	guint8 platform;	 /* Platform (PCB) revision */
	guint8 patch;		 /* patch level */
	guint8 serial_number[6]; /* 48-bit Serial Number */
	guint8 security[2];	 /* bytes 0 and 1 of OTP */
	guint32 patchsig;	 /* opaque patch signature */
	guint8 iface;		 /* interface type, see below */
	guint8 otpsig[3];	 /* OTP Patch Signature */
	guint16 otpspare1;	 /* spare space */
	guint8 reserved;	 /* reserved byte */
	guint8 device_type;	 /* device type */
} FuSynapromReplyGetVersion;

/* the following bits describe security options in
** FuSynapromReplyGetVersion::security[1] bit-field */
#define FU_SYNAPROM_SECURITY1_PROD_SENSOR (1 << 5)

G_DEFINE_TYPE(FuSynapromDevice, fu_synaprom_device, FU_TYPE_USB_DEVICE)

gboolean
fu_synaprom_device_cmd_send(FuSynapromDevice *self,
			    GByteArray *request,
			    GByteArray *reply,
			    FuProgress *progress,
			    guint timeout_ms,
			    GError **error)
{
	gsize actual_len = 0;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 25, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 75, NULL);

	fu_dump_full(G_LOG_DOMAIN,
		     "REQST",
		     request->data,
		     request->len,
		     16,
		     FU_DUMP_FLAGS_SHOW_ADDRESSES);
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 FU_SYNAPROM_USB_REQUEST_EP,
					 request->data,
					 request->len,
					 &actual_len,
					 timeout_ms,
					 NULL,
					 error)) {
		g_prefix_error(error, "failed to request: ");
		return FALSE;
	}
	if (actual_len < request->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "only sent 0x%04x of 0x%04x",
			    (guint)actual_len,
			    request->len);
		return FALSE;
	}
	fu_progress_step_done(progress);

	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 FU_SYNAPROM_USB_REPLY_EP,
					 reply->data,
					 reply->len,
					 NULL, /* allowed to return short read */
					 timeout_ms,
					 NULL,
					 error)) {
		g_prefix_error(error, "failed to reply: ");
		return FALSE;
	}
	fu_dump_full(G_LOG_DOMAIN,
		     "REPLY",
		     reply->data,
		     actual_len,
		     16,
		     FU_DUMP_FLAGS_SHOW_ADDRESSES);
	fu_progress_step_done(progress);

	/* parse as FuSynapromReplyGeneric */
	if (reply->len >= sizeof(FuSynapromReplyGeneric)) {
		FuSynapromReplyGeneric *hdr = (FuSynapromReplyGeneric *)reply->data;
		return fu_synaprom_error_from_status(GUINT16_FROM_LE(hdr->status), error);
	}

	/* success */
	return TRUE;
}

guint32
fu_synaprom_device_get_product_type(FuSynapromDevice *self)
{
	return self->product_type;
}

void
fu_synaprom_device_set_version(FuSynapromDevice *self,
			       guint8 vmajor,
			       guint8 vminor,
			       guint32 buildnum)
{
	g_autofree gchar *str = NULL;

	/* We decide to skip 10.02.xxxxxx firmware, so we force the minor version from 0x02
	** to 0x01 to make the devices with 0x02 minor version firmware allow to be updated
	** back to minor version 0x01. */
	if (vmajor == 0x0a && vminor == 0x02) {
		g_debug("quirking vminor from %02x to 01", vminor);
		vminor = 0x01;
	}

	/* set display version */
	str = g_strdup_printf("%02u.%02u.%u", vmajor, vminor, buildnum);
	fu_device_set_version(FU_DEVICE(self), str);

	/* we need this for checking the firmware compatibility later */
	self->vmajor = vmajor;
	self->vminor = vminor;
}

static void
fu_synaprom_device_set_serial_number(FuSynapromDevice *self, guint64 serial_number)
{
	g_autofree gchar *str = NULL;
	str = g_strdup_printf("%" G_GUINT64_FORMAT, serial_number);
	fu_device_set_serial(FU_DEVICE(self), str);
}

static gboolean
fu_synaprom_device_setup(FuDevice *device, GError **error)
{
	FuSynapromDevice *self = FU_SYNAPROM_DEVICE(device);
	FuSynapromReplyGetVersion pkt;
	guint32 product;
	guint64 serial_number = 0;
	g_autoptr(GByteArray) request = NULL;
	g_autoptr(GByteArray) reply = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_synaprom_device_parent_class)->setup(device, error))
		return FALSE;

	/* get version */
	request = fu_synaprom_request_new(FU_SYNAPROM_CMD_GET_VERSION, NULL, 0);
	reply = fu_synaprom_reply_new(sizeof(FuSynapromReplyGetVersion));
	if (!fu_synaprom_device_cmd_send(self, request, reply, progress, 250, error)) {
		g_prefix_error(error, "failed to get version: ");
		return FALSE;
	}
	memcpy(&pkt, reply->data, sizeof(pkt)); /* nocheck:blocked */
	product = GUINT32_FROM_LE(pkt.product);
	g_info("product ID is %u, version=%u.%u, buildnum=%u prod=%i",
	       product,
	       pkt.vmajor,
	       pkt.vminor,
	       GUINT32_FROM_LE(pkt.buildnum),
	       pkt.security[1] & FU_SYNAPROM_SECURITY1_PROD_SENSOR);
	fu_synaprom_device_set_version(self, pkt.vmajor, pkt.vminor, GUINT32_FROM_LE(pkt.buildnum));

	/* get serial number */
	memcpy(&serial_number, pkt.serial_number, sizeof(pkt.serial_number)); /* nocheck:blocked */
	fu_synaprom_device_set_serial_number(self, serial_number);

	/* check device type */
	if (product == FU_SYNAPROM_PRODUCT_PROMETHEUS || product == FU_SYNAPROM_PRODUCT_TRITON) {
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else if (product == FU_SYNAPROM_PRODUCT_PROMETHEUSPBL ||
		   product == FU_SYNAPROM_PRODUCT_PROMETHEUSMSBL ||
		   product == FU_SYNAPROM_PRODUCT_TRITONPBL ||
		   product == FU_SYNAPROM_PRODUCT_TRITONMSBL) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device %u is not supported by this plugin",
			    product);
		return FALSE;
	}

	if (product == FU_SYNAPROM_PRODUCT_TRITON || product == FU_SYNAPROM_PRODUCT_TRITONPBL ||
	    product == FU_SYNAPROM_PRODUCT_TRITONMSBL)
		self->product_type = FU_SYNAPROM_PRODUCT_TYPE_TRITON;
	else
		self->product_type = FU_SYNAPROM_PRODUCT_TYPE_PROMETHEUS;

	/* add updatable config child, if this is a production sensor */
	if (fu_device_get_children(device)->len == 0 &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER) &&
	    pkt.security[1] & FU_SYNAPROM_SECURITY1_PROD_SENSOR) {
		g_autoptr(FuSynapromConfig) cfg = fu_synaprom_config_new(self);
		fu_device_add_child(FU_DEVICE(device), FU_DEVICE(cfg));
	}

	/* success */
	return TRUE;
}

FuFirmware *
fu_synaprom_device_prepare_firmware(FuDevice *device,
				    GInputStream *stream,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	guint32 product_id;
	g_autoptr(FuFirmware) firmware = fu_synaprom_firmware_new();
	FuSynapromDevice *self = FU_SYNAPROM_DEVICE(device);

	if (self->product_type == FU_SYNAPROM_PRODUCT_TYPE_TRITON) {
		if (!fu_synaprom_firmware_set_signature_size(FU_SYNAPROM_FIRMWARE(firmware),
							     FU_SYNAPROM_FIRMWARE_TRITON_SIGSIZE))
			return NULL;
	}

	/* check the update header product and version */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	product_id = fu_synaprom_firmware_get_product_id(FU_SYNAPROM_FIRMWARE(firmware));
	if (product_id != FU_SYNAPROM_PRODUCT_PROMETHEUS &&
	    product_id != FU_SYNAPROM_PRODUCT_TRITON) {
		if (flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) {
			g_warning("MFW metadata not compatible, "
				  "got 0x%02x expected 0x%02x or 0x%02x",
				  product_id,
				  (guint)FU_SYNAPROM_PRODUCT_PROMETHEUS,
				  (guint)FU_SYNAPROM_PRODUCT_TRITON);
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "MFW metadata not compatible, "
				    "got 0x%02x expected 0x%02x or 0x%02x",
				    product_id,
				    (guint)FU_SYNAPROM_PRODUCT_PROMETHEUS,
				    (guint)FU_SYNAPROM_PRODUCT_TRITON);
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_synaprom_device_write_chunks(FuSynapromDevice *self,
				GPtrArray *chunks,
				FuProgress *progress,
				GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		GByteArray *chunk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) request = NULL;
		g_autoptr(GByteArray) reply = NULL;

		/* patch */
		request =
		    fu_synaprom_request_new(FU_SYNAPROM_CMD_BOOTLDR_PATCH, chunk->data, chunk->len);
		reply = fu_synaprom_reply_new(sizeof(FuSynapromReplyGeneric));
		if (!fu_synaprom_device_cmd_send(self,
						 request,
						 reply,
						 fu_progress_get_child(progress),
						 20000,
						 error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

gboolean
fu_synaprom_device_write_fw(FuSynapromDevice *self,
			    GBytes *fw,
			    FuProgress *progress,
			    GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	gsize offset = 0;
	g_autoptr(GPtrArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, NULL);

	/* collect chunks */
	buf = g_bytes_get_data(fw, &bufsz);
	chunks = g_ptr_array_new_with_free_func((GDestroyNotify)g_byte_array_unref);
	while (offset != bufsz) {
		guint32 chunksz = 0;
		g_autofree guint8 *chunkbuf = NULL;
		g_autoptr(GByteArray) chunk = g_byte_array_new();

		/* get chunk size */
		if (!fu_memread_uint32_safe(buf, bufsz, offset, &chunksz, G_LITTLE_ENDIAN, error))
			return FALSE;
		offset += sizeof(guint32);

		/* read out chunk */
		chunkbuf = g_malloc0(chunksz);
		if (!fu_memcpy_safe(chunkbuf,
				    chunksz,
				    0x0, /* dst */
				    buf,
				    bufsz,
				    offset, /* src */
				    chunksz,
				    error))
			return FALSE;
		offset += chunksz;

		/* add chunk */
		g_byte_array_append(chunk, chunkbuf, chunksz);
		g_ptr_array_add(chunks, g_steal_pointer(&chunk));
	}
	fu_progress_step_done(progress);

	/* write chunks */
	if (!fu_synaprom_device_write_chunks(self, chunks, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_synaprom_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuSynapromDevice *self = FU_SYNAPROM_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;

	/* get default image */
	fw = fu_firmware_get_image_by_id_bytes(firmware, "mfw-update-payload", error);
	if (fw == NULL)
		return FALSE;

	return fu_synaprom_device_write_fw(self, fw, progress, error);
}

static gboolean
fu_synaprom_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	gboolean ret;
	gsize actual_len = 0;
	guint8 data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	ret = fu_usb_device_control_transfer(FU_USB_DEVICE(device),
					     FU_USB_DIRECTION_HOST_TO_DEVICE,
					     FU_USB_REQUEST_TYPE_VENDOR,
					     FU_USB_RECIPIENT_DEVICE,
					     FU_SYNAPROM_USB_CTRLREQUEST_VENDOR_WRITEDFT,
					     0x0000,
					     0x0000,
					     data,
					     sizeof(data),
					     &actual_len,
					     2000,
					     NULL,
					     error);
	if (!ret)
		return FALSE;
	if (actual_len != sizeof(data)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "only sent 0x%04x of 0x%04x",
			    (guint)actual_len,
			    (guint)sizeof(data));
		return FALSE;
	}
	if (!fu_usb_device_reset(FU_USB_DEVICE(device), error)) {
		g_prefix_error(error, "failed to force-reset device: ");
		return FALSE;
	}
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_synaprom_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	gsize actual_len = 0;
	guint8 data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00};

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}

	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(device),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_VENDOR,
					    FU_USB_RECIPIENT_DEVICE,
					    FU_SYNAPROM_USB_CTRLREQUEST_VENDOR_WRITEDFT,
					    0x0000,
					    0x0000,
					    data,
					    sizeof(data),
					    &actual_len,
					    2000,
					    NULL,
					    error))
		return FALSE;
	if (actual_len != sizeof(data)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "only sent 0x%04x of 0x%04x",
			    (guint)actual_len,
			    (guint)sizeof(data));
		return FALSE;
	}
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_usb_device_reset(FU_USB_DEVICE(device), error)) {
		g_prefix_error(error, "failed to force-reset device: ");
		return FALSE;
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static void
fu_synaprom_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 96, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_synaprom_device_init(FuSynapromDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_protocol(FU_DEVICE(self), "com.synaptics.prometheus");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_name(FU_DEVICE(self), "Prometheus");
	fu_device_set_summary(FU_DEVICE(self), "Fingerprint reader");
	fu_device_set_vendor(FU_DEVICE(self), "Synaptics");
	fu_device_add_icon(FU_DEVICE(self), "auth-fingerprint");
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x0);
}

static void
fu_synaprom_device_class_init(FuSynapromDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_synaprom_device_write_firmware;
	device_class->prepare_firmware = fu_synaprom_device_prepare_firmware;
	device_class->setup = fu_synaprom_device_setup;
	device_class->reload = fu_synaprom_device_setup;
	device_class->attach = fu_synaprom_device_attach;
	device_class->detach = fu_synaprom_device_detach;
	device_class->set_progress = fu_synaprom_device_set_progress;
}

FuSynapromDevice *
fu_synaprom_device_new(FuUsbDevice *device)
{
	FuSynapromDevice *self;
	self = g_object_new(FU_TYPE_SYNAPROM_DEVICE, NULL);
	if (device != NULL) {
		fu_device_incorporate(FU_DEVICE(self),
				      FU_DEVICE(device),
				      FU_DEVICE_INCORPORATE_FLAG_ALL);
	}
	return FU_SYNAPROM_DEVICE(self);
}
