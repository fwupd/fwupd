/*
 * Copyright 2024 Huddly
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-huddly-usb-common.h"
#include "fu-huddly-usb-device.h"
#include "fu-huddly-usb-struct.h"

enum { EP_OUT, EP_IN, EP_LAST };
#define HUDDLY_USB_RECEIVE_BUFFER_SIZE 1024

#if !GLIB_CHECK_VERSION(2, 74, 0)
#define G_REGEX_DEFAULT	      0
#define G_REGEX_MATCH_DEFAULT 0
#endif

struct _FuHuddlyUsbDevice {
	FuUsbDevice parent_instance;
	guint bulk_ep[EP_LAST];
	gboolean interfaces_claimed;
	gboolean pending_verify;
	GInputStream *input_stream;
	gchar *product_state;
	gboolean need_reboot;
};

G_DEFINE_TYPE(FuHuddlyUsbDevice, fu_huddly_usb_device, FU_TYPE_USB_DEVICE)

static void
fu_huddly_usb_device_set_state(FuHuddlyUsbDevice *self, const gchar *product_state)
{
	g_free(self->product_state);
	self->product_state = g_strdup(product_state);
}

static gboolean
fu_huddly_usb_device_find_interface(FuHuddlyUsbDevice *self, GError **error)
{
	g_autoptr(GPtrArray) intfs = NULL;

	intfs = fu_usb_device_get_interfaces(FU_USB_DEVICE(self), error);
	if (intfs == NULL) {
		g_prefix_error(error, "could not find interface");
		return FALSE;
	}
	for (guint i = 0; i < intfs->len; i++) {
		FuUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (fu_usb_interface_get_class(intf) == FU_USB_CLASS_VENDOR_SPECIFIC) {
			g_autoptr(GPtrArray) endpoints = fu_usb_interface_get_endpoints(intf);
			for (guint j = 0; j < endpoints->len; j++) {
				FuUsbEndpoint *ep = g_ptr_array_index(endpoints, j);
				if (fu_usb_endpoint_get_direction(ep) ==
				    FU_USB_DIRECTION_HOST_TO_DEVICE) {
					self->bulk_ep[EP_OUT] = fu_usb_endpoint_get_address(ep);
				} else {
					self->bulk_ep[EP_IN] = fu_usb_endpoint_get_address(ep);
				}
			}
		}
	}
	if (self->bulk_ep[EP_OUT] == 0 || self->bulk_ep[EP_IN] == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "could not find usb endpoints");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_huddly_usb_device_bulk_write(FuHuddlyUsbDevice *self,
				GByteArray *src,
				FuProgress *progress,
				GError **error)
{
	gsize offset = 0;
	const gsize max_chunk_size = 16 * 1024;

	if (progress != NULL)
		fu_progress_set_id(progress, G_STRLOC);
	do {
		gsize transmitted = 0;
		gsize remaining = src->len - offset;
		gsize chunk_size = (remaining > max_chunk_size) ? max_chunk_size : remaining;

		if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
						 self->bulk_ep[EP_OUT],
						 src->data + offset,
						 chunk_size,
						 &transmitted,
						 2000,
						 NULL,
						 error)) {
			return FALSE;
		}
		offset += transmitted;
		if (progress != NULL)
			fu_progress_set_percentage_full(progress, offset, src->len);
	} while (offset < src->len);
	return TRUE;
}

static gboolean
fu_huddly_usb_device_bulk_read(FuHuddlyUsbDevice *self,
			       GByteArray *buf,
			       gsize *received_length,
			       GError **error)
{
	return fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					   self->bulk_ep[EP_IN],
					   buf->data,
					   buf->len,
					   received_length,
					   20000,
					   NULL,
					   error);
}

static gboolean
fu_huddly_usb_device_hlink_send(FuHuddlyUsbDevice *self, FuHuddlyUsbHLinkMsg *msg, GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	buf = fu_huddly_usb_hlink_msg_write(msg, error);
	if (buf == NULL)
		return FALSE;
	return fu_huddly_usb_device_bulk_write(self, buf, NULL, error);
}

static FuHuddlyUsbHLinkMsg *
fu_huddly_usb_device_hlink_receive(FuHuddlyUsbDevice *self, GError **error)
{
	gsize received_length = 0;
	g_autoptr(GByteArray) msg_res = g_byte_array_new();
	g_autoptr(FuHuddlyUsbHLinkMsg) msg = NULL;

	fu_byte_array_set_size(msg_res, HUDDLY_USB_RECEIVE_BUFFER_SIZE, 0u);
	if (!fu_huddly_usb_device_bulk_read(self, msg_res, &received_length, error)) {
		g_prefix_error(error, "HLink receive failed: ");
		return NULL;
	}
	msg = fu_huddly_usb_hlink_msg_parse(msg_res->data, received_length, error);
	if (msg == NULL) {
		g_prefix_error(error, "HLink receive failed: ");
		return NULL;
	}
	return g_steal_pointer(&msg);
}

static gboolean
fu_huddly_usb_device_hlink_subscribe(FuHuddlyUsbDevice *self,
				     const gchar *subscription,
				     GError **error)
{
	g_autoptr(FuHuddlyUsbHLinkMsg) msg =
	    fu_huddly_usb_hlink_msg_new_string("hlink-mb-subscribe", subscription);
	g_debug("subscribe %s", subscription);
	return fu_huddly_usb_device_hlink_send(self, msg, error);
}

static gboolean
fu_huddly_usb_device_hlink_unsubscribe(FuHuddlyUsbDevice *self,
				       const gchar *subscription,
				       GError **error)
{
	g_autoptr(FuHuddlyUsbHLinkMsg) msg =
	    fu_huddly_usb_hlink_msg_new_string("hlink-mb-unsubscribe", subscription);
	g_debug("unsubscribe %s", subscription);
	return fu_huddly_usb_device_hlink_send(self, msg, error);
}

/* send an empty packet to reset hlink communications */
static gboolean
fu_huddly_usb_device_send_reset(FuHuddlyUsbDevice *self, GError **error)
{
	g_autoptr(GByteArray) packet = g_byte_array_new();
	if (!fu_huddly_usb_device_bulk_write(self, packet, NULL, error)) {
		g_prefix_error(error, "reset device failed: ");
		return FALSE;
	}
	return TRUE;
}

/* send a hlink salute and receive a response from the device */
static gboolean
fu_huddly_usb_device_salute(FuHuddlyUsbDevice *self, GError **error)
{
	gsize received_length = 0;
	g_autoptr(GByteArray) salutation = g_byte_array_new();
	g_autoptr(GByteArray) response = g_byte_array_new();
	g_autofree gchar *str = NULL;

	g_debug("send salute...");
	fu_byte_array_append_uint8(salutation, 0x00);

	if (!fu_huddly_usb_device_bulk_write(self, salutation, NULL, error)) {
		g_prefix_error(error, "send salute send message failed: ");
		return FALSE;
	}

	fu_byte_array_set_size(response, 100, 0x0);
	if (!fu_huddly_usb_device_bulk_read(self, response, &received_length, error)) {
		g_prefix_error(error, "send salute read response failed: ");
		return FALSE;
	}
	str = fu_strsafe((const gchar *)response->data, received_length);
	g_debug("received response %s", str);
	return TRUE;
}

static gboolean
fu_huddly_usb_device_ensure_product_info(FuHuddlyUsbDevice *self, GError **error)
{
	g_auto(GStrv) version_split = NULL;
	g_autoptr(FuHuddlyUsbHLinkMsg) msg_req = NULL;
	g_autoptr(FuHuddlyUsbHLinkMsg) msg_res = NULL;
	g_autoptr(FuMsgpackItem) item_state = NULL;
	g_autoptr(FuMsgpackItem) item_version = NULL;
	g_autoptr(GPtrArray) items = NULL;

	if (!fu_huddly_usb_device_hlink_subscribe(self, "prodinfo/get_msgpack_reply", error)) {
		g_prefix_error(error, "failed to read product info: ");
		return FALSE;
	}
	msg_req = fu_huddly_usb_hlink_msg_new("prodinfo/get_msgpack", NULL);
	if (!fu_huddly_usb_device_hlink_send(self, msg_req, error)) {
		g_prefix_error(error, "failed to read product info: ");
		return FALSE;
	}
	msg_res = fu_huddly_usb_device_hlink_receive(self, error);
	if (msg_res == NULL) {
		g_prefix_error(error, "failed to read product info: ");
		return FALSE;
	}
	g_debug("receive data %s", msg_res->msg_name);
	items = fu_msgpack_parse(msg_res->payload, error);
	if (items == NULL)
		return FALSE;

	/* version */
	item_version = fu_msgpack_map_lookup(items, 0, "app_version", error);
	if (item_version == NULL) {
		g_prefix_error(error, "failed to read product info: ");
		return FALSE;
	}
	version_split = g_regex_split_simple("[-+]",
					     fu_msgpack_item_get_string(item_version)->str,
					     G_REGEX_DEFAULT,
					     G_REGEX_MATCH_DEFAULT);
	fu_device_set_version(FU_DEVICE(self), version_split[0]);

	/* state */
	item_state = fu_msgpack_map_lookup(items, 0, "state", error);
	if (item_state == NULL) {
		g_prefix_error(error, "failed to read product info: ");
		return FALSE;
	}
	fu_huddly_usb_device_set_state(self, fu_msgpack_item_get_string(item_state)->str);
	return TRUE;
}

static gboolean
fu_huddly_usb_device_reboot(FuHuddlyUsbDevice *self, GError **error)
{
	g_autoptr(FuHuddlyUsbHLinkMsg) msg = fu_huddly_usb_hlink_msg_new("camctrl/reboot", NULL);
	return fu_huddly_usb_device_hlink_send(self, msg, error);
}

static gboolean
fu_huddly_usb_device_hcp_write_file(FuHuddlyUsbDevice *self,
				    const gchar *filename,
				    GInputStream *stream,
				    FuProgress *progress,
				    GError **error)
{
	g_autoptr(FuHuddlyUsbHLinkMsg) msg_req = NULL;
	g_autoptr(FuHuddlyUsbHLinkMsg) msg_res = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GByteArray) payload_msgpack = NULL;
	g_autoptr(GPtrArray) rcv_items = NULL;
	g_autoptr(GPtrArray) msgpack_items =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(FuMsgpackItem) item_status = NULL;
	guint8 status_code;

	g_ptr_array_add(msgpack_items, fu_msgpack_item_new_map(2));
	g_ptr_array_add(msgpack_items, fu_msgpack_item_new_string("name"));
	g_ptr_array_add(msgpack_items, fu_msgpack_item_new_string(filename));
	g_ptr_array_add(msgpack_items, fu_msgpack_item_new_string("file_data"));
	g_ptr_array_add(msgpack_items, fu_msgpack_item_new_binary_stream(stream));
	payload_msgpack = fu_msgpack_write(msgpack_items, error);
	msg_req = fu_huddly_usb_hlink_msg_new("hcp/write", payload_msgpack);
	buf = fu_huddly_usb_hlink_msg_write(msg_req, error);
	if (buf == NULL)
		return FALSE;

	if (!fu_huddly_usb_device_hlink_subscribe(self, "hcp/write_reply", error))
		return FALSE;
	if (!fu_huddly_usb_device_bulk_write(self, buf, progress, error))
		return FALSE;

	/* read reply and check status */
	msg_res = fu_huddly_usb_device_hlink_receive(self, error);
	if (msg_res == NULL)
		return FALSE;

	rcv_items = fu_msgpack_parse(msg_res->payload, error);
	if (rcv_items == NULL)
		return FALSE;
	item_status = fu_msgpack_map_lookup(rcv_items, 0, "status", error);
	if (item_status == NULL)
		return FALSE;
	status_code = fu_msgpack_item_get_integer(item_status);
	if (status_code != 0) {
		g_autoptr(FuMsgpackItem) item_errstr = NULL;
		item_errstr = fu_msgpack_map_lookup(rcv_items, 0, "string", NULL);
		if (item_errstr != NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to write file to target: %s (%u)",
				    fu_msgpack_item_get_string(item_errstr)->str,
				    status_code);
			return FALSE;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to write file to target: %u",
			    status_code);
		return FALSE;
	}

	return fu_huddly_usb_device_hlink_unsubscribe(self, "hcp/write_reply", error);
}

static gboolean
fu_huddly_usb_device_hpk_done_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuHuddlyUsbDevice *self = FU_HUDDLY_USB_DEVICE(device);
	GString *operation;
	guint8 err;
	g_autoptr(FuHuddlyUsbHLinkMsg) msg_res = NULL;
	g_autoptr(FuMsgpackItem) item_operation = NULL;
	g_autoptr(FuMsgpackItem) item_error = NULL;
	g_autoptr(FuMsgpackItem) item_reboot = NULL;
	g_autoptr(GPtrArray) items = NULL;

	msg_res = fu_huddly_usb_device_hlink_receive(self, error);
	if (msg_res == NULL)
		return FALSE;
	items = fu_msgpack_parse(msg_res->payload, error);
	if (items == NULL)
		return FALSE;
	item_operation = fu_msgpack_map_lookup(items, 0, "operation", error);
	if (item_operation == NULL)
		return FALSE;
	operation = fu_msgpack_item_get_string(item_operation);
	g_debug("operation %s", operation->str);

	/* get error */
	item_error = fu_msgpack_map_lookup(items, 0, "error", error);
	if (item_error == NULL)
		return FALSE;
	err = fu_msgpack_item_get_integer(item_error);
	if (err != 0) {
		g_prefix_error(error, "received error %s", operation->str);
		return FALSE;
	}

	item_reboot = fu_msgpack_map_lookup(items, 0, "reboot", error);
	if (item_reboot == NULL)
		return FALSE;
	self->need_reboot = fu_msgpack_item_get_boolean(item_reboot);

	/* are we done? */
	if (g_strcmp0(operation->str, "done") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "operation was %s",
			    operation->str);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_huddly_usb_device_hpk_run(FuHuddlyUsbDevice *self, const gchar *filename, GError **error)
{
	g_autoptr(GByteArray) pack_buffer = NULL;
	g_autoptr(GPtrArray) items = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(FuHuddlyUsbHLinkMsg) msg = NULL;

	g_ptr_array_add(items, fu_msgpack_item_new_map(1));
	g_ptr_array_add(items, fu_msgpack_item_new_string("filename"));
	g_ptr_array_add(items, fu_msgpack_item_new_string(filename));

	if (!fu_huddly_usb_device_hlink_subscribe(self, "upgrader/status", error))
		return FALSE;
	pack_buffer = fu_msgpack_write(items, error);
	if (pack_buffer == NULL)
		return FALSE;
	msg = fu_huddly_usb_hlink_msg_new("hpk/run", pack_buffer);
	if (msg == NULL)
		return FALSE;
	if (!fu_huddly_usb_device_hlink_send(self, msg, error))
		return FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_huddly_usb_device_hpk_done_cb,
				  100,
				  500, /* ms */
				  NULL,
				  error))
		return FALSE;

	return fu_huddly_usb_device_hlink_unsubscribe(self, "upgrader/status", error);
}

static void
fu_huddly_usb_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuHuddlyUsbDevice *self = FU_HUDDLY_USB_DEVICE(device);
	fwupd_codec_string_append(str, idt, "ProductState", self->product_state);
	fwupd_codec_string_append_bool(str, idt, "PendingVerify", self->pending_verify);
	fwupd_codec_string_append_bool(str, idt, "NeedReboot", self->need_reboot);
}

static gboolean
fu_huddly_usb_device_verify(FuHuddlyUsbDevice *self, FuProgress *progress, GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 20, NULL);

	/* write the firmware image to the device for verification */
	if (!fu_huddly_usb_device_hcp_write_file(self,
						 "firmware.hpk",
						 self->input_stream,
						 fu_progress_get_child(progress),
						 error)) {
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* tell the device to execute the upgrade script in the transmitted hpk. This will verify
	 * the written software */
	if (!fu_huddly_usb_device_hpk_run(self, "firmware.hpk", error))
		return FALSE;
	fu_progress_step_done(progress);

	self->pending_verify = FALSE;
	return TRUE;
}

static gboolean
fu_huddly_usb_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuHuddlyUsbDevice *self = FU_HUDDLY_USB_DEVICE(device);

	if (!fu_huddly_usb_device_ensure_product_info(self, error)) {
		g_prefix_error(error, "failed to read product info: ");
		return FALSE;
	}

	/* check that the device is pending verification */
	if (g_strcmp0(self->product_state, "Unverified") == 0) {
		if (!fu_huddly_usb_device_verify(self, progress, error))
			return FALSE;
		/* ensure that the device reports state 'Verified' after the update has completed */
		if (!fu_huddly_usb_device_ensure_product_info(self, error))
			return FALSE;
		if (g_strcmp0(self->product_state, "Verified") != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "expected device state Verified. State %s",
				    self->product_state);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_huddly_usb_device_probe(FuDevice *device, GError **error)
{
	FuHuddlyUsbDevice *self = FU_HUDDLY_USB_DEVICE(device);

	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS(fu_huddly_usb_device_parent_class)->probe(device, error))
		return FALSE;

	return fu_huddly_usb_device_find_interface(self, error);
}

static gboolean
fu_huddly_usb_device_setup(FuDevice *device, GError **error)
{
	FuHuddlyUsbDevice *self = FU_HUDDLY_USB_DEVICE(device);

	/* UsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_huddly_usb_device_parent_class)->setup(device, error))
		return FALSE;

	/* send protocol reset twice in case previous communication has not terminated correctly */
	if (!fu_huddly_usb_device_send_reset(self, error))
		return FALSE;
	if (!fu_huddly_usb_device_send_reset(self, error))
		return FALSE;
	if (!fu_huddly_usb_device_salute(self, error))
		return FALSE;
	if (!fu_huddly_usb_device_ensure_product_info(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_huddly_usb_device_cleanup(FuDevice *device,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuHuddlyUsbDevice *self = FU_HUDDLY_USB_DEVICE(device);

	/* no longer required */
	g_clear_object(&self->input_stream);

	return TRUE;
}

static gboolean
fu_huddly_usb_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuHuddlyUsbDevice *self = FU_HUDDLY_USB_DEVICE(device);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 54, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 45, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, NULL);

	/* get default image */
	self->input_stream = fu_firmware_get_stream(firmware, error);
	if (self->input_stream == NULL)
		return FALSE;

	/* send the image file to the target */
	if (!fu_huddly_usb_device_hcp_write_file(self,
						 "firmware.hpk",
						 self->input_stream,
						 fu_progress_get_child(progress),
						 error)) {
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* tell the device to execute the upgrade script embedded in the hpk */
	if (!fu_huddly_usb_device_hpk_run(self, "firmware.hpk", error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!self->need_reboot) {
		/* The device not requesting reboot could occur if the device was in an unverified
		state due to an aborted previous upgrade attempt, in which case this download will
		complete the upgrade */
		g_warning("expected device to request reboot after download");
		return TRUE;
	}

	/* reboot the device after the upgrade has been written */
	if (!fu_huddly_usb_device_reboot(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	self->pending_verify = TRUE;
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_huddly_usb_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 72, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 26, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_huddly_usb_device_init(FuHuddlyUsbDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_remove_delay(FU_DEVICE(self), 60000); /* 60 second remove delay */
	fu_device_add_protocol(FU_DEVICE(self), "com.huddly.usb");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_WEB_CAMERA);
}

static void
fu_huddly_usb_device_replace(FuDevice *device, FuDevice *donor)
{
	FuHuddlyUsbDevice *self = FU_HUDDLY_USB_DEVICE(device);
	FuHuddlyUsbDevice *self_donor = FU_HUDDLY_USB_DEVICE(donor);
	g_set_object(&self->input_stream, self_donor->input_stream);
}

static void
fu_huddly_usb_device_finalize(GObject *object)
{
	FuHuddlyUsbDevice *self = FU_HUDDLY_USB_DEVICE(object);
	if (self->input_stream != NULL)
		g_object_unref(self->input_stream);
	g_free(self->product_state);
	G_OBJECT_CLASS(fu_huddly_usb_device_parent_class)->finalize(object);
}

static void
fu_huddly_usb_device_class_init(FuHuddlyUsbDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_huddly_usb_device_finalize;
	device_class->to_string = fu_huddly_usb_device_to_string;
	device_class->probe = fu_huddly_usb_device_probe;
	device_class->setup = fu_huddly_usb_device_setup;
	device_class->cleanup = fu_huddly_usb_device_cleanup;
	device_class->attach = fu_huddly_usb_device_attach;
	device_class->write_firmware = fu_huddly_usb_device_write_firmware;
	device_class->set_progress = fu_huddly_usb_device_set_progress;
	device_class->replace = fu_huddly_usb_device_replace;
}
