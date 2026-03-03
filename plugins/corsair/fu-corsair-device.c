/*
 * Copyright 2022 Andrii Dushko <andrii.dushko@developex.net>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-corsair-common.h"
#include "fu-corsair-device.h"
#include "fu-corsair-subdevice.h"

struct _FuCorsairDevice {
	FuUsbDevice parent_instance;
	guint8 vendor_interface;
	guint8 epin;
	guint8 epout;
	guint16 cmd_write_size;
	guint16 cmd_read_size;
};

G_DEFINE_TYPE(FuCorsairDevice, fu_corsair_device, FU_TYPE_USB_DEVICE)

#define CORSAIR_DEFAULT_VENDOR_INTERFACE_ID 1
#define CORSAIR_DEFAULT_CMD_SIZE	    64
#define CORSAIR_ACTIVATION_TIMEOUT	    30000
#define CORSAIR_TRANSACTION_TIMEOUT	    10000
#define CORSAIR_FIRST_CHUNK_HEADER_SIZE	    7

static void
fu_corsair_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "VendorInterface", self->vendor_interface);
	fwupd_codec_string_append_hex(str, idt, "InEndpoint", self->epin);
	fwupd_codec_string_append_hex(str, idt, "OutEndpoint", self->epout);
	fwupd_codec_string_append_hex(str, idt, "CmdWriteSize", self->cmd_write_size);
	fwupd_codec_string_append_hex(str, idt, "CmdReadSize", self->cmd_read_size);
}

static gchar *
fu_corsair_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_corsair_version_from_uint32(version_raw);
}

static gboolean
fu_corsair_device_send(FuCorsairDevice *self, GByteArray *buf, guint timeout, GError **error)
{
	gsize actual_len = 0;
	g_autoptr(GByteArray) buf2 = g_byte_array_new();

	/* sanity check */
	if (self->cmd_write_size == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "cmd size invalid");
		return FALSE;
	}

	g_byte_array_append(buf2, buf->data, buf->len);
	fu_byte_array_set_size(buf2, self->cmd_write_size, 0x0);
	fu_dump_raw(G_LOG_DOMAIN, "request", buf2->data, buf2->len);
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      self->epout,
					      buf2->data,
					      buf2->len,
					      &actual_len,
					      timeout,
					      NULL,
					      error)) {
		g_prefix_error_literal(error, "failed to write command: ");
		return FALSE;
	}
	if (actual_len != buf2->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wrong size written: %" G_GSIZE_FORMAT,
			    actual_len);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_corsair_device_recv(FuCorsairDevice *self, guint timeout, GError **error)
{
	gsize actual_len = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* sanity check */
	if (self->cmd_read_size == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "cmd size invalid");
		return NULL;
	}

	fu_byte_array_set_size(buf, self->cmd_read_size, 0x0);
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      self->epin,
					      buf->data,
					      buf->len,
					      &actual_len,
					      timeout,
					      NULL,
					      error)) {
		g_prefix_error_literal(error, "failed to get command response: ");
		return NULL;
	}
	if (actual_len != buf->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wrong size read: %" G_GSIZE_FORMAT,
			    actual_len);
		return NULL;
	}
	fu_dump_raw(G_LOG_DOMAIN, "response", buf->data, buf->len);

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_corsair_device_cmd_generic(FuCorsairDevice *self, GByteArray *buf, guint timeout, GError **error)
{
	g_autoptr(GByteArray) buf_tmp = NULL;
	g_autoptr(FuStructCorsairGenericRes) st_res = NULL;

	if (!fu_corsair_device_send(self, buf, timeout, error))
		return FALSE;
	buf_tmp = fu_corsair_device_recv(self, timeout, error);
	if (buf_tmp == NULL)
		return FALSE;
	st_res = fu_struct_corsair_generic_res_parse(buf_tmp->data, buf_tmp->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_corsair_device_flush_input_reports(FuCorsairDevice *self)
{
	gsize actual_len;
	g_autofree guint8 *buf = g_malloc0(self->cmd_read_size);

	for (guint i = 0; i < 3; i++) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
						      self->epin,
						      buf,
						      self->cmd_read_size,
						      &actual_len,
						      10, /* ms */
						      NULL,
						      &error_local))
			g_debug("flushing status: %s", error_local->message);
	}
}

static gboolean
fu_corsair_device_write_init(FuCorsairDevice *self,
			     FuCorsairDestination destination,
			     GError **error)
{
	g_autoptr(FuStructCorsairInitReq) st_req = fu_struct_corsair_init_req_new();

	fu_struct_corsair_init_req_set_destination(st_req, destination);
	if (!fu_corsair_device_cmd_generic(self, st_req->buf, CORSAIR_TRANSACTION_TIMEOUT, error)) {
		g_prefix_error_literal(error, "firmware init fail: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_corsair_device_write_chk0(FuCorsairDevice *self,
			     FuCorsairDestination destination,
			     FuChunk *chunk,
			     guint32 firmware_size,
			     GError **error)
{
	g_autoptr(FuStructCorsairWriteFirstReq) st_req = fu_struct_corsair_write_first_req_new();

	fu_struct_corsair_write_first_req_set_destination(st_req, destination);
	fu_struct_corsair_write_first_req_set_size(st_req, firmware_size);
	g_byte_array_append(st_req->buf, fu_chunk_get_data(chunk), fu_chunk_get_data_sz(chunk));
	if (!fu_corsair_device_cmd_generic(self, st_req->buf, CORSAIR_TRANSACTION_TIMEOUT, error)) {
		g_prefix_error_literal(error, "write command fail: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_corsair_device_write_chunk(FuCorsairDevice *self,
			      FuCorsairDestination destination,
			      FuChunk *chunk,
			      GError **error)
{
	g_autoptr(FuStructCorsairWriteNextReq) st_req = fu_struct_corsair_write_next_req_new();

	fu_struct_corsair_write_next_req_set_destination(st_req, destination);
	g_byte_array_append(st_req->buf, fu_chunk_get_data(chunk), fu_chunk_get_data_sz(chunk));
	if (!fu_corsair_device_cmd_generic(self, st_req->buf, CORSAIR_TRANSACTION_TIMEOUT, error)) {
		g_prefix_error_literal(error, "write command fail: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_corsair_device_get_property(FuCorsairDevice *self,
			       FuCorsairDestination destination,
			       FuCorsairDeviceProperty property,
			       guint32 *value,
			       GError **error)
{
	g_autoptr(FuStructCorsairGetPropertyReq) st_req = fu_struct_corsair_get_property_req_new();
	g_autoptr(FuStructCorsairGetPropertyRes) st_res = NULL;
	g_autoptr(GByteArray) buf_tmp = NULL;

	fu_struct_corsair_get_property_req_set_destination(st_req, destination);
	fu_struct_corsair_get_property_req_set_property(st_req, property);
	if (!fu_corsair_device_send(self, st_req->buf, CORSAIR_TRANSACTION_TIMEOUT, error))
		return FALSE;
	buf_tmp = fu_corsair_device_recv(self, CORSAIR_TRANSACTION_TIMEOUT, error);
	if (buf_tmp == NULL)
		return FALSE;
	st_res = fu_struct_corsair_get_property_res_parse(buf_tmp->data, buf_tmp->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/* success */
	*value = fu_struct_corsair_get_property_res_get_value(st_res);
	return TRUE;
}

gboolean
fu_corsair_device_set_mode(FuCorsairDevice *self,
			   FuCorsairDestination destination,
			   FuCorsairDeviceMode mode,
			   GError **error)
{
	g_autoptr(FuStructCorsairSetModeReq) st_req = fu_struct_corsair_set_mode_req_new();

	fu_struct_corsair_set_mode_req_set_destination(st_req, destination);
	fu_struct_corsair_set_mode_req_set_mode(st_req, mode);
	if (!fu_corsair_device_cmd_generic(self, st_req->buf, CORSAIR_TRANSACTION_TIMEOUT, error)) {
		g_prefix_error_literal(error, "set mode command fail: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_corsair_device_write_firmware_chunks(FuCorsairDevice *self,
					FuCorsairDestination destination,
					FuChunk *chk0,
					FuChunkArray *chunks,
					FuProgress *progress,
					guint32 firmware_size,
					GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks) + 1);

	/* first chunk */
	if (!fu_corsair_device_write_chk0(self, destination, chk0, firmware_size, error)) {
		g_prefix_error_literal(error, "cannot write first chunk: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* other chunks */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_corsair_device_write_chunk(self, destination, chk, error)) {
			g_prefix_error(error, "cannot write chunk %u: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_corsair_device_commit_firmware(FuCorsairDevice *self,
				  FuCorsairDestination destination,
				  GError **error)
{
	g_autoptr(FuStructCorsairCommitReq) st_req = fu_struct_corsair_commit_req_new();

	fu_struct_corsair_commit_req_set_destination(st_req, destination);
	if (!fu_corsair_device_cmd_generic(self, st_req->buf, CORSAIR_TRANSACTION_TIMEOUT, error)) {
		g_prefix_error_literal(error, "firmware commit fail: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_corsair_device_activate_firmware(FuCorsairDevice *self,
				    FuCorsairDestination destination,
				    GBytes *blob,
				    GError **error)
{
	g_autoptr(FuStructCorsairActivateReq) st_req = fu_struct_corsair_activate_req_new();
	fu_struct_corsair_activate_req_set_destination(st_req, destination);
	fu_struct_corsair_activate_req_set_crc(st_req, fu_crc32_bytes(FU_CRC_KIND_B32_MPEG2, blob));
	return fu_corsair_device_cmd_generic(self, st_req->buf, CORSAIR_ACTIVATION_TIMEOUT, error);
}

gboolean
fu_corsair_device_write_firmware_full(FuCorsairDevice *self,
				      FuCorsairDestination destination,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuChunk) chk0 = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_rest = NULL;
	guint32 chk0_size = self->cmd_write_size - CORSAIR_FIRST_CHUNK_HEADER_SIZE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, NULL);

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL) {
		g_prefix_error_literal(error, "cannot get firmware data: ");
		return FALSE;
	}

	/* the firmware size should be greater than 1 chunk */
	if (g_bytes_get_size(blob) <= chk0_size) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "update file should be bigger");
		return FALSE;
	}

	chk0 = fu_chunk_new(0, 0, 0, g_bytes_get_data(blob, NULL), chk0_size);
	blob_rest = fu_bytes_new_offset(blob, chk0_size, g_bytes_get_size(blob) - chk0_size, error);
	if (blob_rest == NULL) {
		g_prefix_error_literal(error, "cannot get firmware past first chunk: ");
		return FALSE;
	}
	if (!fu_corsair_device_write_init(self, destination, error)) {
		g_prefix_error_literal(error, "cannot write init: ");
		return FALSE;
	}
	chunks = fu_chunk_array_new_from_bytes(blob_rest,
					       chk0_size,
					       FU_CHUNK_PAGESZ_NONE,
					       self->cmd_write_size -
						   FU_STRUCT_CORSAIR_WRITE_NEXT_REQ_SIZE);
	if (!fu_corsair_device_write_firmware_chunks(self,
						     destination,
						     chk0,
						     chunks,
						     fu_progress_get_child(progress),
						     g_bytes_get_size(blob),
						     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* commit and activate */
	if (!fu_corsair_device_commit_firmware(self, destination, error))
		return FALSE;
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_CORSAIR_DEVICE_FLAG_LEGACY_ATTACH)) {
		if (!fu_corsair_device_activate_firmware(self, destination, blob, error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_corsair_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	return fu_corsair_device_write_firmware_full(self,
						     FU_CORSAIR_DESTINATION_SELF,
						     firmware,
						     progress,
						     error);
}

gboolean
fu_corsair_device_legacy_attach(FuCorsairDevice *self,
				FuCorsairDestination destination,
				GError **error)
{
	g_autoptr(FuStructCorsairAttachReq) st_req = fu_struct_corsair_attach_req_new();
	fu_struct_corsair_attach_req_set_destination(st_req, destination);
	return fu_corsair_device_send(self, st_req->buf, CORSAIR_TRANSACTION_TIMEOUT, error);
}

static gboolean
fu_corsair_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;
	if (fu_device_has_private_flag(device, FU_CORSAIR_DEVICE_FLAG_LEGACY_ATTACH)) {
		if (!fu_corsair_device_legacy_attach(self, FU_CORSAIR_DESTINATION_SELF, error))
			return FALSE;
	} else {
		if (!fu_corsair_device_set_mode(self,
						FU_CORSAIR_DESTINATION_SELF,
						FU_CORSAIR_DEVICE_MODE_APPLICATION,
						error))
			return FALSE;
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_corsair_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* the device sometimes reboots before providing a response */
	if (!fu_corsair_device_set_mode(self,
					FU_CORSAIR_DESTINATION_SELF,
					FU_CORSAIR_DEVICE_MODE_BOOTLOADER,
					&error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ)) {
			g_debug("ignoring: %s", error_local->message);
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	/* success */
	fu_device_sleep(device, 4000);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_corsair_device_probe(FuDevice *device, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	FuUsbInterface *iface = NULL;
	FuUsbEndpoint *ep1 = NULL;
	FuUsbEndpoint *ep2 = NULL;
	g_autoptr(GPtrArray) ifaces = NULL;
	g_autoptr(GPtrArray) endpoints = NULL;

	if (!FU_DEVICE_CLASS(fu_corsair_device_parent_class)->probe(device, error))
		return FALSE;

	ifaces = fu_usb_device_get_interfaces(FU_USB_DEVICE(self), error);
	if (ifaces == NULL || (ifaces->len < (self->vendor_interface + 1u))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "update interface not found");
		return FALSE;
	}

	/* expecting to have two endpoints for communication */
	iface = g_ptr_array_index(ifaces, self->vendor_interface);
	endpoints = fu_usb_interface_get_endpoints(iface);
	if (endpoints == NULL || endpoints->len != 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "update interface endpoints not found");
		return FALSE;
	}

	ep1 = g_ptr_array_index(endpoints, 0);
	ep2 = g_ptr_array_index(endpoints, 1);
	if (fu_usb_endpoint_get_direction(ep1) == FU_USB_DIRECTION_DEVICE_TO_HOST) {
		self->epin = fu_usb_endpoint_get_address(ep1);
		self->epout = fu_usb_endpoint_get_address(ep2);
		self->cmd_read_size = fu_usb_endpoint_get_maximum_packet_size(ep1);
		self->cmd_write_size = fu_usb_endpoint_get_maximum_packet_size(ep2);
	} else {
		self->epin = fu_usb_endpoint_get_address(ep2);
		self->epout = fu_usb_endpoint_get_address(ep1);
		self->cmd_read_size = fu_usb_endpoint_get_maximum_packet_size(ep2);
		self->cmd_write_size = fu_usb_endpoint_get_maximum_packet_size(ep1);
	}
	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->vendor_interface);

	/* sanity check */
	if (self->cmd_write_size <= CORSAIR_FIRST_CHUNK_HEADER_SIZE ||
	    self->cmd_write_size <= FU_STRUCT_CORSAIR_WRITE_NEXT_REQ_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "endpoint packet size too small");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_corsair_device_poll_subdevice(FuCorsairDevice *self, gboolean *subdevice_added, GError **error)
{
	guint32 subdevices = 0;
	g_autoptr(FuCorsairSubdevice) child = NULL;

	if (!fu_corsair_device_get_property(self,
					    FU_CORSAIR_DESTINATION_SELF,
					    FU_CORSAIR_DEVICE_PROPERTY_SUBDEVICES,
					    &subdevices,
					    error)) {
		g_prefix_error_literal(error, "cannot get subdevices: ");
		return FALSE;
	}
	if (subdevices == 0) {
		*subdevice_added = FALSE;
		return TRUE;
	}
	child = fu_corsair_subdevice_new(FU_DEVICE(self));
	if (!fu_device_setup(FU_DEVICE(child), error))
		return FALSE;
	fu_device_add_child(FU_DEVICE(self), FU_DEVICE(child));

	/* success */
	*subdevice_added = TRUE;
	return TRUE;
}

static gboolean
fu_corsair_device_ensure_mode(FuCorsairDevice *self, GError **error)
{
	guint32 mode = 0;
	if (!fu_corsair_device_get_property(self,
					    FU_CORSAIR_DESTINATION_SELF,
					    FU_CORSAIR_DEVICE_PROPERTY_MODE,
					    &mode,
					    error))
		return FALSE;
	if (mode == FU_CORSAIR_DEVICE_MODE_BOOTLOADER)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_corsair_device_ensure_version(FuCorsairDevice *self, GError **error)
{
	guint32 version_raw = 0;

	if (!fu_corsair_device_get_property(self,
					    FU_CORSAIR_DESTINATION_SELF,
					    FU_CORSAIR_DEVICE_PROPERTY_VERSION,
					    &version_raw,
					    error)) {
		g_prefix_error_literal(error, "cannot get version: ");
		return FALSE;
	}

	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		/* Version 0xffffffff means that previous update was interrupted.
		   Set version to 0.0.0 in both broken and interrupted cases to make sure that new
		   firmware will not be rejected because of older version. It is safe to always
		   pass firmware because setup in bootloader mode can only happen during
		   emergency update */
		if (version_raw == G_MAXUINT32)
			version_raw = 0;
	}

	/* success */
	fu_device_set_version_raw(FU_DEVICE(self), version_raw);
	return TRUE;
}

static gboolean
fu_corsair_device_ensure_battery_level(FuCorsairDevice *self, GError **error)
{
	guint32 battery_level = 0;
	if (!fu_corsair_device_get_property(self,
					    FU_CORSAIR_DESTINATION_SELF,
					    FU_CORSAIR_DEVICE_PROPERTY_BATTERY_LEVEL,
					    &battery_level,
					    error)) {
		g_prefix_error_literal(error, "cannot get battery level: ");
		return FALSE;
	}
	if (battery_level > 1000) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "battery level is invalid: %u%%",
			    battery_level / 10);
		return FALSE;
	}
	fu_device_set_battery_level(FU_DEVICE(self), battery_level / 10);
	return TRUE;
}

static gboolean
fu_corsair_device_ensure_bootloader_version(FuCorsairDevice *self, GError **error)
{
	guint32 version_raw = 0;
	g_autofree gchar *version_str = NULL;

	if (!fu_corsair_device_get_property(self,
					    FU_CORSAIR_DESTINATION_SELF,
					    FU_CORSAIR_DEVICE_PROPERTY_BOOTLOADER_VERSION,
					    &version_raw,
					    error)) {
		g_prefix_error_literal(error, "cannot get bootloader version: ");
		return FALSE;
	}

	version_str = fu_corsair_device_convert_version(FU_DEVICE(self), version_raw);
	fu_device_set_version_bootloader(FU_DEVICE(self), version_str);
	return TRUE;
}

static gboolean
fu_corsair_device_setup(FuDevice *device, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);

	if (!FU_DEVICE_CLASS(fu_corsair_device_parent_class)->setup(device, error))
		return FALSE;

	/* clears any dangling IN reports that the device may have sent after the enumeration */
	fu_corsair_device_flush_input_reports(self);
	if (!fu_corsair_device_ensure_mode(self, error))
		return FALSE;
	if (!fu_corsair_device_ensure_version(self, error))
		return FALSE;
	if (!fu_corsair_device_ensure_bootloader_version(self, error))
		return FALSE;

	/* a usb-receiver has no battery level */
	if (!fu_device_has_private_flag(device, FU_CORSAIR_DEVICE_FLAG_IS_RECEIVER) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_corsair_device_ensure_battery_level(self, error))
			return FALSE;
	}

	/* check for a subdevice */
	if (fu_device_has_private_flag(device, FU_CORSAIR_DEVICE_FLAG_IS_RECEIVER) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		gboolean subdevice_added = FALSE;
		g_autoptr(GError) error_local = NULL;

		/* give some time to a subdevice to get connected to the receiver */
		fu_device_sleep(device, 10); /* ms */
		if (!fu_corsair_device_poll_subdevice(self, &subdevice_added, &error_local)) {
			g_warning("error polling subdevice: %s", error_local->message);
		} else {
			/* start polling if a subdevice was not added */
			if (!subdevice_added)
				fu_device_set_poll_interval(device, 30000); /* ms */
		}
	}

	/* make look pretty */
	if (fu_device_has_private_flag(device, FU_CORSAIR_DEVICE_FLAG_IS_RECEIVER))
		fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_USB_RECEIVER);
	else
		fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_MOUSE);

	/* success */
	return TRUE;
}

static gboolean
fu_corsair_device_is_subdevice_connected_cb(FuDevice *device, gpointer user_data, GError **error)
{
	guint32 subdevices = 0;
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);

	if (!fu_corsair_device_get_property(self,
					    FU_CORSAIR_DESTINATION_SELF,
					    FU_CORSAIR_DEVICE_PROPERTY_SUBDEVICES,
					    &subdevices,
					    error)) {
		g_prefix_error_literal(error, "cannot get subdevices: ");
		return FALSE;
	}
	if (subdevices == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "subdevice is not connected");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_corsair_device_reconnect_subdevice(FuCorsairDevice *self, GError **error)
{
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_corsair_device_is_subdevice_connected_cb,
				  30,
				  1000, /* ms */
				  NULL,
				  error)) {
		g_prefix_error_literal(error, "subdevice did not reconnect: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_corsair_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 25, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 73, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static gboolean
fu_corsair_device_set_quirk_kv(FuDevice *device,
			       const gchar *key,
			       const gchar *value,
			       GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	guint64 vendor_interface;

	if (g_strcmp0(key, "CorsairVendorInterfaceId") == 0) {
		if (!fu_strtoull(value,
				 &vendor_interface,
				 0,
				 G_MAXUINT8,
				 FU_INTEGER_BASE_AUTO,
				 error)) {
			g_prefix_error_literal(error, "cannot parse CorsairVendorInterface: ");
			return FALSE;
		}
		self->vendor_interface = vendor_interface;
		return TRUE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static gboolean
fu_corsair_device_poll(FuDevice *device, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	gboolean subdevice_added = FALSE;
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL) {
		g_prefix_error_literal(error, "cannot open device: ");
		return FALSE;
	}
	if (!fu_corsair_device_poll_subdevice(self, &subdevice_added, error))
		return FALSE;

	/* stop polling if a subdevice was added */
	if (subdevice_added) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "subdevice added successfully");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_corsair_device_init(FuCorsairDevice *self)
{
	self->vendor_interface = CORSAIR_DEFAULT_VENDOR_INTERFACE_ID;
	self->cmd_read_size = CORSAIR_DEFAULT_CMD_SIZE;
	self->cmd_write_size = CORSAIR_DEFAULT_CMD_SIZE;
	fu_device_register_private_flag(FU_DEVICE(self), FU_CORSAIR_DEVICE_FLAG_IS_RECEIVER);
	fu_device_register_private_flag(FU_DEVICE(self), FU_CORSAIR_DEVICE_FLAG_LEGACY_ATTACH);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_battery_threshold(FU_DEVICE(self), 30);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_AUTO_PAUSE_POLLING);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_add_protocol(FU_DEVICE(self), "com.corsair.bp");
}

static void
fu_corsair_device_class_init(FuCorsairDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->poll = fu_corsair_device_poll;
	device_class->probe = fu_corsair_device_probe;
	device_class->set_quirk_kv = fu_corsair_device_set_quirk_kv;
	device_class->setup = fu_corsair_device_setup;
	device_class->attach = fu_corsair_device_attach;
	device_class->detach = fu_corsair_device_detach;
	device_class->write_firmware = fu_corsair_device_write_firmware;
	device_class->to_string = fu_corsair_device_to_string;
	device_class->set_progress = fu_corsair_device_set_progress;
	device_class->convert_version = fu_corsair_device_convert_version;
}
