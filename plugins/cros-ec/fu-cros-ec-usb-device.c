/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-cros-ec-common.h"
#include "fu-cros-ec-firmware.h"
#include "fu-cros-ec-hammer-touchpad.h"
#include "fu-cros-ec-struct.h"
#include "fu-cros-ec-usb-device.h"

#define FU_CROS_EC_USB_SUBCLASS_GOOGLE_UPDATE 0x53
#define FU_CROS_EC_USB_PROTOCOL_GOOGLE_UPDATE 0xff

#define FU_CROS_EC_SETUP_RETRY_CNT	   5
#define FU_CROS_EC_MAX_BLOCK_XFER_RETRIES  10
#define FU_CROS_EC_FLUSH_TIMEOUT_MS	   10
#define FU_CROS_EC_BULK_SEND_TIMEOUT	   2000 /* ms */
#define FU_CROS_EC_BULK_RECV_TIMEOUT	   5000 /* ms */
#define FU_CROS_EC_USB_DEVICE_REMOVE_DELAY 20000

#define FU_CROS_EC_REQUEST_UPDATE_DONE	    0xB007AB1E
#define FU_CROS_EC_REQUEST_UPDATE_EXTRA_CMD 0xB007AB1F

#define FU_CROS_EC_DEVICE_FLAG_HAS_TOUCHPAD "has-touchpad"

struct _FuCrosEcUsbDevice {
	FuUsbDevice parent_instance;
	guint8 iface_idx;  /* bInterfaceNumber */
	guint8 ep_num;	   /* bEndpointAddress */
	guint16 chunk_len; /* wMaxPacketSize */
	gchar *raw_version;
	guint32 maximum_pdu_size;
	guint32 flash_protection;
	guint32 writeable_offset;
	guint16 protocol_version;
	gchar configuration[FU_STRUCT_CROS_EC_FIRST_RESPONSE_PDU_SIZE_VERSION];
	gboolean in_bootloader;
};

G_DEFINE_TYPE(FuCrosEcUsbDevice, fu_cros_ec_usb_device, FU_TYPE_USB_DEVICE)

typedef struct {
	FuChunk *block;
	FuProgress *progress;
} FuCrosEcUsbBlockHelper;

#define FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN	   "ro-written"
#define FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN	   "rw-written"
#define FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO "rebooting-to-ro"
#define FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL	   "special"

static gboolean
fu_cros_ec_usb_device_get_configuration(FuCrosEcUsbDevice *self, GError **error)
{
	guint8 index;
	g_autofree gchar *configuration = NULL;

	index = fu_usb_device_get_configuration_index(FU_USB_DEVICE(self), error);
	if (index == 0x0)
		return FALSE;
	configuration = fu_usb_device_get_string_descriptor(FU_USB_DEVICE(self), index, error);
	if (configuration == NULL)
		return FALSE;
	g_debug("%s(%s): raw configuration read: %s",
		fu_device_get_id(FU_DEVICE(self)),
		fu_device_get_name(FU_DEVICE(self)),
		configuration);

	if (g_strlcpy(self->configuration,
		      configuration,
		      FU_STRUCT_CROS_EC_FIRST_RESPONSE_PDU_SIZE_VERSION) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "empty iConfiguration");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_find_interface(FuUsbDevice *device, GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE(device);
	g_autoptr(GPtrArray) intfs = NULL;

	/* based on usb_updater2's find_interfacei() and find_endpoint() */
	intfs = fu_usb_device_get_interfaces(device, error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		FuUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (fu_usb_interface_get_class(intf) == 255 &&
		    fu_usb_interface_get_subclass(intf) == FU_CROS_EC_USB_SUBCLASS_GOOGLE_UPDATE &&
		    fu_usb_interface_get_protocol(intf) == FU_CROS_EC_USB_PROTOCOL_GOOGLE_UPDATE) {
			FuUsbEndpoint *ep;
			g_autoptr(GPtrArray) endpoints = fu_usb_interface_get_endpoints(intf);
			if (NULL == endpoints || endpoints->len == 0)
				continue;
			ep = g_ptr_array_index(endpoints, 0);
			self->iface_idx = fu_usb_interface_get_number(intf);
			self->ep_num = fu_usb_endpoint_get_address(ep) & 0x7f;
			self->chunk_len = fu_usb_endpoint_get_maximum_packet_size(ep);
			return TRUE;
		}
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no update interface found");
	return FALSE;
}

static gboolean
fu_cros_ec_usb_device_probe(FuDevice *device, GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE(device);

	/* very much like usb_updater2's usb_findit() */
	if (!fu_cros_ec_usb_device_find_interface(FU_USB_DEVICE(device), error)) {
		g_prefix_error(error, "failed to find update interface: ");
		return FALSE;
	}
	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->iface_idx);

	if (self->chunk_len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wMaxPacketSize isn't valid: %" G_GUINT16_FORMAT,
			    self->chunk_len);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_do_xfer(FuCrosEcUsbDevice *self,
			      const guint8 *outbuf,
			      gsize outlen,
			      guint8 *inbuf,
			      gsize inlen,
			      gboolean allow_less,
			      gsize *rxed_count,
			      GError **error)
{
	gsize actual = 0;

	/* send data out */
	if (outbuf != NULL && outlen > 0) {
		g_autofree guint8 *outbuf_tmp = NULL;

		/* make mutable */
		outbuf_tmp = fu_memdup_safe(outbuf, outlen, error);
		if (outbuf_tmp == NULL)
			return FALSE;

		if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
						 self->ep_num,
						 outbuf_tmp,
						 outlen,
						 &actual,
						 FU_CROS_EC_BULK_SEND_TIMEOUT,
						 NULL,
						 error))
			return FALSE;
		if (actual != outlen) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "only sent %" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT " bytes",
				    actual,
				    outlen);
			return FALSE;
		}
	}

	/* read reply back */
	if (inbuf != NULL && inlen > 0) {
		actual = 0;
		if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
						 self->ep_num | 0x80,
						 inbuf,
						 inlen,
						 &actual,
						 FU_CROS_EC_BULK_RECV_TIMEOUT,
						 NULL,
						 error)) {
			fu_error_convert(error);
			return FALSE;
		}
		if (actual != inlen && !allow_less) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "only received %" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT " bytes",
				    actual,
				    inlen);
			return FALSE;
		}
	}

	if (rxed_count != NULL)
		*rxed_count = actual;

	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_flush(FuDevice *device, gpointer user_data, GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE(device);
	gsize actual = 0;
	g_autofree guint8 *inbuf = g_malloc0(self->chunk_len);

	/* bulk transfer expected to fail normally (ie, no stale data)
	 * but if bulk transfer succeeds, indicates stale bytes on the device
	 * so this will retry until they're emptied */
	if (fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					self->ep_num | 0x80,
					inbuf,
					self->chunk_len,
					&actual,
					FU_CROS_EC_FLUSH_TIMEOUT_MS,
					NULL,
					NULL)) {
		g_debug("flushing %" G_GSIZE_FORMAT " bytes", actual);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "flushing %" G_GSIZE_FORMAT " bytes",
			    actual);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_recovery(FuCrosEcUsbDevice *self, GError **error)
{
	/* flush all data from endpoint to recover in case of error */
	if (!fu_device_retry(FU_DEVICE(self),
			     fu_cros_ec_usb_device_flush,
			     FU_CROS_EC_SETUP_RETRY_CNT,
			     NULL,
			     error)) {
		g_prefix_error(error, "failed to flush device to idle state: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/*
 * Channel TPM extension/vendor command over USB. The payload of the USB frame
 * in this case consists of the 2 byte subcommand code concatenated with the
 * command body. The caller needs to indicate if a response is expected, and
 * if it is - of what maximum size.
 */
static gboolean
fu_cros_ec_usb_device_ext_cmd(FuCrosEcUsbDevice *self,
			      guint16 subcommand,
			      guint8 *cmd_body,
			      gsize body_size,
			      guint8 *resp,
			      gsize *resp_size,
			      gboolean allow_less,
			      GError **error)
{
	gsize usb_msg_size =
	    FU_STRUCT_CROS_EC_UPDATE_FRAME_HEADER_SIZE + sizeof(subcommand) + body_size;
	g_autoptr(FuStructCrosEcUpdateFrameHeader) ufh =
	    fu_struct_cros_ec_update_frame_header_new();
	fu_struct_cros_ec_update_frame_header_set_block_size(ufh, usb_msg_size);
	fu_struct_cros_ec_update_frame_header_set_cmd_block_base(
	    ufh,
	    FU_CROS_EC_REQUEST_UPDATE_EXTRA_CMD);
	fu_byte_array_append_uint16(ufh, subcommand, G_BIG_ENDIAN);
	if (body_size > 0)
		g_byte_array_append(ufh, cmd_body, body_size);
	return fu_cros_ec_usb_device_do_xfer(self,
					     ufh->data,
					     ufh->len,
					     (guint8 *)resp,
					     resp_size != NULL ? *resp_size : 0,
					     TRUE,
					     NULL,
					     error);
}

static gboolean
fu_cros_ec_usb_device_start_request_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE(device);
	FuStructCrosEcFirstResponsePdu *st_rpdu = (FuStructCrosEcFirstResponsePdu *)user_data;
	gsize rxed_size = 0;
	g_autoptr(FuStructCrosEcUpdateFrameHeader) ufh =
	    fu_struct_cros_ec_update_frame_header_new();

	fu_struct_cros_ec_update_frame_header_set_block_size(ufh, ufh->len);
	if (!fu_cros_ec_usb_device_do_xfer(self,
					   ufh->data,
					   ufh->len,
					   st_rpdu->data,
					   st_rpdu->len,
					   TRUE,
					   &rxed_size,
					   error))
		return FALSE;

	/* we got something, so check for errors in response */
	if (rxed_size < 8) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "unexpected response size %" G_GSIZE_FORMAT,
			    rxed_size);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_setup(FuDevice *device, GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE(device);
	guint32 error_code;
	g_auto(GStrv) config_split = NULL;
	g_autoptr(FuStructCrosEcFirstResponsePdu) st_rpdu =
	    fu_struct_cros_ec_first_response_pdu_new();
	g_autoptr(FuCrosEcVersion) active_version = NULL;
	g_autoptr(FuCrosEcVersion) version = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuCrosEcHammerTouchpad) touchpad = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_cros_ec_usb_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_cros_ec_usb_device_recovery(self, error))
		return FALSE;

	/* send start request */
	if (!fu_device_retry(device,
			     fu_cros_ec_usb_device_start_request_cb,
			     FU_CROS_EC_SETUP_RETRY_CNT,
			     st_rpdu,
			     error)) {
		g_prefix_error(error, "failed to send start request: ");
		return FALSE;
	}

	self->protocol_version = fu_struct_cros_ec_first_response_pdu_get_protocol_version(st_rpdu);
	if (self->protocol_version < 5 || self->protocol_version > 6) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported protocol version %d",
			    self->protocol_version);
		return FALSE;
	}

	error_code = fu_struct_cros_ec_first_response_pdu_get_return_value(st_rpdu);
	if (error_code != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "target reporting error %u",
			    error_code);
		return FALSE;
	}

	self->writeable_offset = fu_struct_cros_ec_first_response_pdu_get_offset(st_rpdu);
	g_free(self->raw_version);
	self->raw_version = fu_struct_cros_ec_first_response_pdu_get_version(st_rpdu);
	self->maximum_pdu_size = fu_struct_cros_ec_first_response_pdu_get_maximum_pdu_size(st_rpdu);
	self->flash_protection = fu_struct_cros_ec_first_response_pdu_get_flash_protection(st_rpdu);

	/* get active version string and running region from iConfiguration */
	if (!fu_cros_ec_usb_device_get_configuration(self, error))
		return FALSE;
	config_split = g_strsplit(self->configuration, ":", 2);
	if (g_strv_length(config_split) < 2) {
		/* no prefix found so fall back to offset */
		self->in_bootloader = self->writeable_offset != 0x0;
		active_version = fu_cros_ec_version_parse(self->configuration, error);
		if (active_version == NULL) {
			g_prefix_error(error,
				       "failed parsing device's version: %32s: ",
				       self->configuration);
			return FALSE;
		}
	} else {
		self->in_bootloader = g_strcmp0("RO", config_split[0]) == 0;
		active_version = fu_cros_ec_version_parse(config_split[1], error);
		if (active_version == NULL) {
			g_prefix_error(error,
				       "failed parsing device's version: %32s: ",
				       config_split[1]);
			return FALSE;
		}
	}

	/* get the other region's version string from targ */
	version = fu_cros_ec_version_parse(self->raw_version, &error_local);
	if (version == NULL) {
		if (!self->in_bootloader) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed parsing device's version: %32s: ",
						   self->raw_version);
			return FALSE;
		}
		/* if unable to parse version, copy from the active_version.
		 * This allows to restore devices failed on write due different reasons. */
		version = g_new0(FuCrosEcVersion, 1);
		version->boardname = g_strndup(active_version->boardname,
					       FU_STRUCT_CROS_EC_FIRST_RESPONSE_PDU_SIZE_VERSION);
		version->triplet = g_strndup(active_version->triplet,
					     FU_STRUCT_CROS_EC_FIRST_RESPONSE_PDU_SIZE_VERSION);
		version->sha1 = g_strndup(active_version->sha1,
					  FU_STRUCT_CROS_EC_FIRST_RESPONSE_PDU_SIZE_VERSION);
		version->dirty = active_version->dirty;
	}

	if (self->in_bootloader) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		fu_device_set_version(FU_DEVICE(device), version->triplet);
		fu_device_set_version_bootloader(FU_DEVICE(device), active_version->triplet);
	} else {
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		fu_device_set_version(FU_DEVICE(device), active_version->triplet);
		fu_device_set_version_bootloader(FU_DEVICE(device), version->triplet);
	}

	/* one extra instance ID */
	fu_device_add_instance_str(FU_DEVICE(device), "BOARDNAME", version->boardname);
	if (!fu_device_build_instance_id(FU_DEVICE(device),
					 error,
					 "USB",
					 "VID",
					 "PID",
					 "BOARDNAME",
					 NULL))
		return FALSE;

	if (fu_device_has_private_flag(device, FU_CROS_EC_DEVICE_FLAG_HAS_TOUCHPAD)) {
		touchpad = fu_cros_ec_hammer_touchpad_new(FU_DEVICE(device));
		if (!fu_device_setup(FU_DEVICE(touchpad), &error_local))
			return FALSE;
		fu_device_add_child(FU_DEVICE(device), FU_DEVICE(touchpad));
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_reload(FuDevice *device, GError **error)
{
	if (fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN) &&
	    fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO))
		return TRUE;

	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_transfer_block_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE(device);
	FuCrosEcUsbBlockHelper *helper = (FuCrosEcUsbBlockHelper *)user_data;
	gsize transfer_size = 0;
	guint32 reply = 0;
	g_autoptr(FuStructCrosEcUpdateFrameHeader) ufh =
	    fu_struct_cros_ec_update_frame_header_new();
	g_autoptr(GPtrArray) chunks = NULL;

	/* first send the header */
	fu_struct_cros_ec_update_frame_header_set_block_size(
	    ufh,
	    ufh->len + fu_chunk_get_data_sz(helper->block));
	fu_struct_cros_ec_update_frame_header_set_cmd_block_base(
	    ufh,
	    fu_chunk_get_address(helper->block));
	if (!fu_cros_ec_usb_device_do_xfer(self,
					   ufh->data,
					   ufh->len,
					   NULL,
					   0,
					   FALSE,
					   NULL,
					   error)) {
		g_autoptr(GError) error_flush = NULL;
		/* flush all data from endpoint to recover in case of error */
		if (!fu_cros_ec_usb_device_recovery(self, &error_flush))
			g_debug("failed to flush to idle: %s", error_flush->message);
		g_prefix_error(error, "failed at sending header: ");
		return FALSE;
	}

	/* we're in a retry handler */
	fu_progress_reset(helper->progress);

	/* send the block, chunk by chunk */
	chunks = fu_chunk_array_new(fu_chunk_get_data(helper->block),
				    fu_chunk_get_data_sz(helper->block),
				    0x00,
				    0x00,
				    self->chunk_len);
	fu_progress_set_id(helper->progress, G_STRLOC);
	fu_progress_set_steps(helper->progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!fu_cros_ec_usb_device_do_xfer(self,
						   fu_chunk_get_data(chk),
						   fu_chunk_get_data_sz(chk),
						   NULL,
						   0,
						   FALSE,
						   NULL,
						   error)) {
			g_autoptr(GError) error_flush = NULL;
			g_prefix_error(error, "failed sending chunk 0x%x: ", i);

			/* flush all data from endpoint to recover in case of error */
			if (!fu_cros_ec_usb_device_recovery(self, &error_flush))
				g_debug("failed to flush to idle: %s", error_flush->message);
			return FALSE;
		}
		fu_progress_step_done(helper->progress);
	}

	/* get the reply */
	if (!fu_cros_ec_usb_device_do_xfer(self,
					   NULL,
					   0,
					   (guint8 *)&reply,
					   sizeof(reply),
					   TRUE,
					   &transfer_size,
					   error)) {
		g_autoptr(GError) error_flush = NULL;
		g_prefix_error(error, "failed at reply: ");
		/* flush all data from endpoint to recover in case of error */
		if (!fu_cros_ec_usb_device_recovery(self, &error_flush))
			g_debug("failed to flush to idle: %s", error_flush->message);
		return FALSE;
	}
	if (transfer_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "zero bytes received for block reply");
		return FALSE;
	}
	if (reply != 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "error: status 0x%#x", reply);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_transfer_section(FuCrosEcUsbDevice *self,
				       FuFirmware *firmware,
				       FuCrosEcFirmwareSection *section,
				       FuProgress *progress,
				       GError **error)
{
	const guint8 *data_ptr = NULL;
	gsize data_len = 0;
	g_autoptr(GBytes) img_bytes = NULL;
	g_autoptr(GPtrArray) blocks = NULL;

	g_return_val_if_fail(section != NULL, FALSE);

	img_bytes = fu_firmware_get_image_by_idx_bytes(firmware, section->image_idx, error);
	if (img_bytes == NULL) {
		g_prefix_error(error, "failed to find section image: ");
		return FALSE;
	}

	data_ptr = (const guint8 *)g_bytes_get_data(img_bytes, &data_len);
	if (data_ptr == NULL || data_len != section->size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "image and section sizes do not match: image = %" G_GSIZE_FORMAT
			    " bytes vs section size = %" G_GSIZE_FORMAT " bytes",
			    data_len,
			    section->size);
		return FALSE;
	}

	/* smart update: trim trailing bytes */
	while (data_len > 1 && (data_ptr[data_len - 1] == 0xff))
		data_len--;
	g_debug("trimmed %" G_GSIZE_FORMAT " trailing bytes", section->size - data_len);
	g_debug("sending 0x%x bytes to 0x%x", (guint)data_len, section->offset);

	/* send in chunks of PDU size */
	blocks =
	    fu_chunk_array_new(data_ptr, data_len, section->offset, 0x0, self->maximum_pdu_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, blocks->len);
	for (guint i = 0; i < blocks->len; i++) {
		FuCrosEcUsbBlockHelper helper = {
		    .block = g_ptr_array_index(blocks, i),
		    .progress = fu_progress_get_child(progress),
		};
		if (!fu_device_retry(FU_DEVICE(self),
				     fu_cros_ec_usb_device_transfer_block_cb,
				     FU_CROS_EC_MAX_BLOCK_XFER_RETRIES,
				     &helper,
				     error)) {
			g_prefix_error(error, "failed to transfer block 0x%x: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static void
fu_cros_ec_usb_device_send_done(FuCrosEcUsbDevice *self)
{
	guint8 buf[1] = {0x0};
	g_autoptr(FuStructCrosEcUpdateDone) st = fu_struct_cros_ec_update_done_new();
	g_autoptr(GError) error_local = NULL;

	/* send stop request, ignoring reply */
	if (!fu_cros_ec_usb_device_do_xfer(self,
					   st->data,
					   st->len,
					   buf,
					   sizeof(buf),
					   FALSE,
					   NULL,
					   &error_local)) {
		g_debug("error on transfer of done: %s", error_local->message);
	}
}

static gboolean
fu_cros_ec_usb_device_send_subcommand(FuCrosEcUsbDevice *self,
				      guint16 subcommand,
				      guint8 *cmd_body,
				      gsize body_size,
				      guint8 *resp,
				      gsize *resp_size,
				      gboolean allow_less,
				      GError **error)
{
	fu_cros_ec_usb_device_send_done(self);

	if (!fu_cros_ec_usb_device_ext_cmd(self,
					   subcommand,
					   cmd_body,
					   body_size,
					   resp,
					   resp_size,
					   FALSE,
					   error)) {
		g_prefix_error(error,
			       "failed to send subcommand %" G_GUINT16_FORMAT ": ",
			       subcommand);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_cros_ec_usb_device_reset_to_ro(FuCrosEcUsbDevice *self)
{
	guint8 response = 0x0;
	guint16 subcommand = FU_CROS_EC_UPDATE_EXTRA_CMD_IMMEDIATE_RESET;
	guint8 command_body[2] = {0x0}; /* max command body size */
	gsize command_body_size = 0;
	gsize response_size = 1;
	g_autoptr(GError) error_local = NULL;

	if (!fu_cros_ec_usb_device_send_subcommand(self,
						   subcommand,
						   command_body,
						   command_body_size,
						   &response,
						   &response_size,
						   FALSE,
						   &error_local)) {
		/* failure here is ok */
		g_debug("ignoring failure: reset: %s", error_local->message);
	}
}

static gboolean
fu_cros_ec_usb_device_jump_to_rw(FuCrosEcUsbDevice *self)
{
	guint8 response = 0x0;
	guint16 subcommand = FU_CROS_EC_UPDATE_EXTRA_CMD_JUMP_TO_RW;
	guint8 command_body[2] = {0x0}; /* max command body size */
	gsize command_body_size = 0;
	gsize response_size = 1;
	g_autoptr(GError) error_local = NULL;

	if (!fu_cros_ec_usb_device_send_subcommand(self,
						   subcommand,
						   command_body,
						   command_body_size,
						   &response,
						   &response_size,
						   FALSE,
						   &error_local)) {
		/* bail out early here if subcommand failed, which is normal */
		g_debug("ignoring failure: jump to rw: %s", error_local->message);
		return TRUE;
	}

	/* Jump to rw may not work, so if we've reached here, initiate a
	 * full reset using immediate reset */
	fu_cros_ec_usb_device_reset_to_ro(self);

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_stay_in_ro(FuCrosEcUsbDevice *self, GError **error)
{
	gsize response_size = 1;
	guint8 response = 0x0;
	guint16 subcommand = FU_CROS_EC_UPDATE_EXTRA_CMD_STAY_IN_RO;
	guint8 command_body[2] = {0x0}; /* max command body size */
	gsize command_body_size = 0;

	if (!fu_cros_ec_usb_device_send_subcommand(self,
						   subcommand,
						   command_body,
						   command_body_size,
						   &response,
						   &response_size,
						   FALSE,
						   error)) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE(device);
	g_autoptr(GPtrArray) sections = NULL;
	FuCrosEcFirmware *cros_ec_firmware = FU_CROS_EC_FIRMWARE(firmware);

	fu_device_remove_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL);

	if (fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO)) {
		g_autoptr(FuStructCrosEcFirstResponsePdu) st_rpdu =
		    fu_struct_cros_ec_first_response_pdu_new();

		fu_device_remove_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO);
		if (!fu_cros_ec_usb_device_stay_in_ro(self, error)) {
			g_prefix_error(error, "failed to send stay-in-ro subcommand: ");
			return FALSE;
		}

		/* flush all data from endpoint to recover in case of error */
		if (!fu_cros_ec_usb_device_recovery(self, error)) {
			g_prefix_error(error, "failed to flush device to idle state: ");
			return FALSE;
		}

		/* send start request */
		if (!fu_device_retry(device,
				     fu_cros_ec_usb_device_start_request_cb,
				     FU_CROS_EC_SETUP_RETRY_CNT,
				     st_rpdu,
				     error)) {
			g_prefix_error(error, "failed to send start request: ");
			return FALSE;
		}
	}

	if (fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN) &&
	    self->in_bootloader) {
		/*
		 * We had previously written to the rw region (while we were
		 * booted from ro region), but somehow landed in ro again after
		 * a reboot. Since we wrote rw already, we wanted to jump
		 * to the new rw so we could evaluate ro.
		 *
		 * This is a transitory state due to the fact that we have to
		 * boot through RO to get to RW. Set another write required to
		 * allow the RO region to auto-jump to RW.
		 *
		 * Special flow: write phase skips actual write -> attach skips
		 * send of reset command, just sets wait for replug, and
		 * device restart status.
		 */
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
		return TRUE;
	}

	sections = fu_cros_ec_firmware_get_needed_sections(cros_ec_firmware, error);
	if (sections == NULL)
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, sections->len);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < sections->len; i++) {
		FuCrosEcFirmwareSection *section = g_ptr_array_index(sections, i);
		g_autoptr(GError) error_local = NULL;

		if (!fu_cros_ec_usb_device_transfer_section(self,
							    firmware,
							    section,
							    fu_progress_get_child(progress),
							    &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ)) {
				g_debug("failed to transfer section, trying another write, "
					"ignoring error: %s",
					error_local->message);
				fu_device_add_flag(device,
						   FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
				fu_progress_finished(progress);
				return TRUE;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}

		if (self->in_bootloader) {
			fu_device_set_version(device, section->version.triplet);
		} else {
			fu_device_set_version_bootloader(device, section->version.triplet);
		}

		fu_progress_step_done(progress);
	}

	/* send done */
	fu_cros_ec_usb_device_send_done(self);

	if (self->in_bootloader)
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN);
	else
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN);

	/* logical XOR */
	if (fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN) !=
	    fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN))
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_cros_ec_usb_device_prepare_firmware(FuDevice *device,
				       GInputStream *stream,
				       FuProgress *progress,
				       FuFirmwareParseFlags flags,
				       GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_cros_ec_firmware_new();

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	if (!fu_cros_ec_firmware_ensure_version(FU_CROS_EC_FIRMWARE(firmware), error))
		return NULL;

	/* pick sections */
	if (!fu_cros_ec_firmware_pick_sections(FU_CROS_EC_FIRMWARE(firmware),
					       self->writeable_offset,
					       error)) {
		g_prefix_error(error, "failed to pick sections: ");
		return NULL;
	}
	return g_steal_pointer(&firmware);
}

static gboolean
fu_cros_ec_usb_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE(device);

	if (self->in_bootloader &&
	    fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL)) {
		/*
		 * attach after the SPECIAL flag was set. The EC will auto-jump
		 * from ro -> rw, so we do not need to send an explicit
		 * reset_to_ro. We just need to set for another wait for replug
		 * as a detach + reenumeration is expected as we jump from
		 * ro -> rw.
		 */
		fu_device_remove_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		return TRUE;
	}

	if (fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN) &&
	    !fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN)) {
		fu_device_add_private_flag(FU_DEVICE(self),
					   FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO);
		fu_cros_ec_usb_device_reset_to_ro(self);
	} else {
		fu_cros_ec_usb_device_jump_to_rw(self);
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE(device);

	if (fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN) &&
	    !fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN))
		return TRUE;

	if (self->in_bootloader) {
		/* If EC just rebooted - prevent jumping to RW during the update */
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO);
		g_debug("skipping immediate reboot in case of already in bootloader");
		/* in RO so skip reboot */
		return TRUE;
	}

	if (self->flash_protection != 0x0) {
		/* in RW, and RO region is write protected, so jump to RO */
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN);
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO);
		fu_cros_ec_usb_device_reset_to_ro(self);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	}

	/* success */
	return TRUE;
}

static void
fu_cros_ec_usb_device_replace(FuDevice *device, FuDevice *donor)
{
	if (fu_device_has_private_flag(donor, FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN))
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN);
	if (fu_device_has_private_flag(donor, FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN))
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN);
	if (fu_device_has_private_flag(donor, FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO))
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO);
	if (fu_device_has_private_flag(donor, FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL))
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL);
}

static gboolean
fu_cros_ec_usb_device_cleanup(FuDevice *device,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	fu_device_remove_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN);
	fu_device_remove_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN);
	fu_device_remove_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO);
	fu_device_remove_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL);

	/* success */
	return TRUE;
}

static void
fu_cros_ec_usb_device_init(FuCrosEcUsbDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.google.usb.crosec");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_DETACH_PREPARE_FIRMWARE);
	fu_device_set_acquiesce_delay(FU_DEVICE(self), 7500); /* ms */
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_CROS_EC_USB_DEVICE_REMOVE_DELAY);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_CROS_EC_FIRMWARE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_register_private_flag(FU_DEVICE(self), FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN);
	fu_device_register_private_flag(FU_DEVICE(self), FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO);
	fu_device_register_private_flag(FU_DEVICE(self), FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL);
	fu_device_register_private_flag(FU_DEVICE(self), FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL);
	fu_device_register_private_flag(FU_DEVICE(self), FU_CROS_EC_DEVICE_FLAG_HAS_TOUCHPAD);
}

static void
fu_cros_ec_usb_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE(device);
	fwupd_codec_string_append_int(str, idt, "ProtocolVersion", self->protocol_version);
	fwupd_codec_string_append_int(str, idt, "MaxPduSize", self->maximum_pdu_size);
	fwupd_codec_string_append_hex(str, idt, "FlashProtection", self->flash_protection);
	fwupd_codec_string_append(str, idt, "RawVersion", self->raw_version);
	fwupd_codec_string_append_hex(str, idt, "WriteableOffset", self->writeable_offset);
}

static void
fu_cros_ec_usb_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 76, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 22, "reload");
}

static void
fu_cros_ec_usb_device_finalize(GObject *object)
{
	FuCrosEcUsbDevice *self = FU_CROS_EC_USB_DEVICE(object);
	g_free(self->raw_version);
	G_OBJECT_CLASS(fu_cros_ec_usb_device_parent_class)->finalize(object);
}

static void
fu_cros_ec_usb_device_class_init(FuCrosEcUsbDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_cros_ec_usb_device_finalize;
	device_class->attach = fu_cros_ec_usb_device_attach;
	device_class->detach = fu_cros_ec_usb_device_detach;
	device_class->prepare_firmware = fu_cros_ec_usb_device_prepare_firmware;
	device_class->setup = fu_cros_ec_usb_device_setup;
	device_class->to_string = fu_cros_ec_usb_device_to_string;
	device_class->write_firmware = fu_cros_ec_usb_device_write_firmware;
	device_class->probe = fu_cros_ec_usb_device_probe;
	device_class->set_progress = fu_cros_ec_usb_device_set_progress;
	device_class->reload = fu_cros_ec_usb_device_reload;
	device_class->replace = fu_cros_ec_usb_device_replace;
	device_class->cleanup = fu_cros_ec_usb_device_cleanup;
}
