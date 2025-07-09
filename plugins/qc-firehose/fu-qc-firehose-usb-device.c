/*
 * Copyright 2021 Quectel Wireless Solutions Co., Ltd.
 *                    Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qc-firehose-impl.h"
#include "fu-qc-firehose-sahara-impl.h"
#include "fu-qc-firehose-struct.h"
#include "fu-qc-firehose-usb-device.h"

FU_DEFINE_QUARK(FU_QC_FIREHOSE_USB_DEVICE_NO_ZLP, "no-zlp")

#define FU_QC_FIREHOSE_USB_DEVICE_RAW_BUFFER_SIZE (4 * 1024)

struct _FuQcFirehoseUsbDevice {
	FuUsbDevice parent_instance;
	guint8 ep_in;
	guint8 ep_out;
	gsize maxpktsize_in;
	gsize maxpktsize_out;
	FuQcFirehoseFunctions supported_functions;
};

static void
fu_qc_firehose_usb_device_impl_iface_init(FuQcFirehoseImplInterface *iface);
static void
fu_qc_firehose_usb_device_sahara_impl_iface_init(FuQcFirehoseSaharaImplInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuQcFirehoseUsbDevice,
			fu_qc_firehose_usb_device,
			FU_TYPE_USB_DEVICE,
			G_IMPLEMENT_INTERFACE(FU_TYPE_QC_FIREHOSE_IMPL,
					      fu_qc_firehose_usb_device_impl_iface_init)
			    G_IMPLEMENT_INTERFACE(FU_TYPE_QC_FIREHOSE_SAHARA_IMPL,
						  fu_qc_firehose_usb_device_sahara_impl_iface_init))

static void
fu_qc_firehose_usb_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuQcFirehoseUsbDevice *self = FU_QC_FIREHOSE_USB_DEVICE(device);
	g_autofree gchar *functions = fu_qc_firehose_functions_to_string(self->supported_functions);
	fwupd_codec_string_append_hex(str, idt, "EpIn", self->ep_in);
	fwupd_codec_string_append_hex(str, idt, "EpOut", self->ep_out);
	fwupd_codec_string_append_hex(str, idt, "MaxpktsizeIn", self->maxpktsize_in);
	fwupd_codec_string_append_hex(str, idt, "MaxpktsizeOut", self->maxpktsize_out);
	fwupd_codec_string_append(str, idt, "SupportedFunctions", functions);
}

static gboolean
fu_qc_firehose_usb_device_impl_has_function(FuQcFirehoseImpl *impl, FuQcFirehoseFunctions func)
{
	FuQcFirehoseUsbDevice *self = FU_QC_FIREHOSE_USB_DEVICE(impl);
	return (self->supported_functions & func) > 0;
}

static void
fu_qc_firehose_usb_device_impl_add_function(FuQcFirehoseImpl *impl, FuQcFirehoseFunctions func)
{
	FuQcFirehoseUsbDevice *self = FU_QC_FIREHOSE_USB_DEVICE(impl);
	self->supported_functions |= func;
}

static GByteArray *
fu_qc_firehose_usb_device_read(FuQcFirehoseUsbDevice *self, guint timeout_ms, GError **error)
{
	gsize actual_len = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_set_size(buf, FU_QC_FIREHOSE_USB_DEVICE_RAW_BUFFER_SIZE, 0x00);
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 self->ep_in,
					 buf->data,
					 buf->len,
					 &actual_len,
					 timeout_ms,
					 NULL,
					 error)) {
		g_prefix_error(error, "failed to do bulk transfer (read): ");
		return NULL;
	}

	g_byte_array_set_size(buf, actual_len);
	fu_dump_raw(G_LOG_DOMAIN, "rx packet", buf->data, buf->len);
	return g_steal_pointer(&buf);
}

static gboolean
fu_qc_firehose_usb_device_write(FuQcFirehoseUsbDevice *self,
				const guint8 *buf,
				gsize sz,
				guint timeout_ms,
				GError **error)
{
	gsize actual_len = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GByteArray) bufmut = g_byte_array_sized_new(sz);

	/* copy const data to mutable GByteArray */
	g_byte_array_append(bufmut, buf, sz);
	chunks = fu_chunk_array_mutable_new(bufmut->data, bufmut->len, 0, 0, self->maxpktsize_out);
	if (chunks->len > 1)
		g_debug("split into %u chunks", chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		fu_dump_raw(G_LOG_DOMAIN,
			    "tx packet",
			    fu_chunk_get_data(chk),
			    fu_chunk_get_data_sz(chk));
		if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
						 self->ep_out,
						 fu_chunk_get_data_out(chk),
						 fu_chunk_get_data_sz(chk),
						 &actual_len,
						 timeout_ms,
						 NULL,
						 error)) {
			g_prefix_error(error, "failed to do bulk transfer (write data): ");
			return FALSE;
		}
		if (actual_len != fu_chunk_get_data_sz(chk)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "only wrote %" G_GSIZE_FORMAT "bytes",
				    actual_len);
			return FALSE;
		}
	}

	/* sent zlp packet if needed */
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_QC_FIREHOSE_USB_DEVICE_NO_ZLP) &&
	    sz % self->maxpktsize_out == 0) {
		if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
						 self->ep_out,
						 NULL,
						 0,
						 NULL,
						 timeout_ms,
						 NULL,
						 error)) {
			g_prefix_error(error, "failed to do bulk transfer (write zlp): ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_qc_firehose_usb_device_parse_eps(FuQcFirehoseUsbDevice *self, GPtrArray *endpoints)
{
	for (guint i = 0; i < endpoints->len; i++) {
		FuUsbEndpoint *ep = g_ptr_array_index(endpoints, i);
		if (fu_usb_endpoint_get_direction(ep) == FU_USB_DIRECTION_DEVICE_TO_HOST) {
			self->ep_in = fu_usb_endpoint_get_address(ep);
			self->maxpktsize_in = fu_usb_endpoint_get_maximum_packet_size(ep);
		} else {
			self->ep_out = fu_usb_endpoint_get_address(ep);
			self->maxpktsize_out = fu_usb_endpoint_get_maximum_packet_size(ep);
		}
	}
}

static gboolean
fu_qc_firehose_usb_device_probe(FuDevice *device, GError **error)
{
	FuQcFirehoseUsbDevice *self = FU_QC_FIREHOSE_USB_DEVICE(device);
	g_autoptr(GPtrArray) intfs = NULL;

	/* most devices have a BCD version of 0.0 (i.e. unset), but we still want to show the
	 * device in gnome-firmware -- allow overwriting if the descriptor has something better */
	fu_device_set_version(device, "0.0");

	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS(fu_qc_firehose_usb_device_parent_class)->probe(device, error))
		return FALSE;

	/* parse usb interfaces and find suitable endpoints */
	intfs = fu_usb_device_get_interfaces(FU_USB_DEVICE(self), error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		FuUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (fu_usb_interface_get_class(intf) == 0xFF &&
		    fu_usb_interface_get_subclass(intf) == 0xFF &&
		    (fu_usb_interface_get_protocol(intf) == 0xFF ||
		     fu_usb_interface_get_protocol(intf) == 0x11)) {
			g_autoptr(GPtrArray) endpoints = fu_usb_interface_get_endpoints(intf);
			if (endpoints == NULL || endpoints->len == 0)
				continue;
			fu_qc_firehose_usb_device_parse_eps(self, endpoints);
			fu_usb_device_add_interface(FU_USB_DEVICE(self),
						    fu_usb_interface_get_number(intf));
			return TRUE;
		}
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no update interface found");
	return FALSE;
}

static gboolean
fu_qc_firehose_usb_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuQcFirehoseUsbDevice *self = FU_QC_FIREHOSE_USB_DEVICE(device);

	/* if called in recovery we have no supported functions */
	if (self->supported_functions == 0 ||
	    (self->supported_functions & FU_QC_FIREHOSE_FUNCTIONS_POWER) > 0) {
		if (!fu_qc_firehose_impl_reset(FU_QC_FIREHOSE_IMPL(self), error))
			return FALSE;
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_qc_firehose_usb_device_impl_write_firmware(FuDevice *device,
					      FuFirmware *firmware,
					      FuProgress *progress,
					      FwupdInstallFlags flags,
					      GError **error)
{
	FuQcFirehoseUsbDevice *self = FU_QC_FIREHOSE_USB_DEVICE(device);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "sahara");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, "firehose");

	/* load the sahara binary */
	if (!fu_qc_firehose_sahara_impl_write_firmware(FU_QC_FIREHOSE_SAHARA_IMPL(self),
						       firmware,
						       fu_progress_get_child(progress),
						       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* use firehose XML */
	if (!fu_qc_firehose_impl_setup(FU_QC_FIREHOSE_IMPL(self), error))
		return FALSE;
	if (!fu_qc_firehose_impl_write_firmware(
		FU_QC_FIREHOSE_IMPL(self),
		firmware,
		fu_device_has_private_flag(device, FU_QC_FIREHOSE_USB_DEVICE_NO_ZLP),
		fu_progress_get_child(progress),
		error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_qc_firehose_usb_device_replace(FuDevice *device, FuDevice *donor)
{
	if (fu_device_has_private_flag(donor, FU_QC_FIREHOSE_USB_DEVICE_NO_ZLP))
		fu_device_add_private_flag(device, FU_QC_FIREHOSE_USB_DEVICE_NO_ZLP);
}

static void
fu_qc_firehose_usb_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static GByteArray *
fu_qc_firehose_usb_device_sahara_impl_read(FuQcFirehoseSaharaImpl *impl,
					   guint timeout_ms,
					   GError **error)
{
	FuQcFirehoseUsbDevice *self = FU_QC_FIREHOSE_USB_DEVICE(impl);
	return fu_qc_firehose_usb_device_read(self, timeout_ms, error);
}

static GByteArray *
fu_qc_firehose_usb_device_impl_read(FuQcFirehoseImpl *impl, guint timeout_ms, GError **error)
{
	FuQcFirehoseUsbDevice *self = FU_QC_FIREHOSE_USB_DEVICE(impl);
	return fu_qc_firehose_usb_device_read(self, timeout_ms, error);
}

static gboolean
fu_qc_firehose_usb_device_sahara_impl_write(FuQcFirehoseSaharaImpl *impl,
					    const guint8 *buf,
					    gsize sz,
					    guint timeout_ms,
					    GError **error)
{
	FuQcFirehoseUsbDevice *self = FU_QC_FIREHOSE_USB_DEVICE(impl);
	return fu_qc_firehose_usb_device_write(self, buf, sz, timeout_ms, error);
}

static gboolean
fu_qc_firehose_usb_device_impl_write(FuQcFirehoseImpl *impl,
				     const guint8 *buf,
				     gsize sz,
				     guint timeout_ms,
				     GError **error)
{
	FuQcFirehoseUsbDevice *self = FU_QC_FIREHOSE_USB_DEVICE(impl);
	return fu_qc_firehose_usb_device_write(self, buf, sz, timeout_ms, error);
}

static void
fu_qc_firehose_usb_device_impl_iface_init(FuQcFirehoseImplInterface *iface)
{
	iface->read = fu_qc_firehose_usb_device_impl_read;
	iface->write = fu_qc_firehose_usb_device_impl_write;
	iface->has_function = fu_qc_firehose_usb_device_impl_has_function;
	iface->add_function = fu_qc_firehose_usb_device_impl_add_function;
}

static void
fu_qc_firehose_usb_device_sahara_impl_iface_init(FuQcFirehoseSaharaImplInterface *iface)
{
	iface->read = fu_qc_firehose_usb_device_sahara_impl_read;
	iface->write = fu_qc_firehose_usb_device_sahara_impl_write;
}

static void
fu_qc_firehose_usb_device_init(FuQcFirehoseUsbDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.qualcomm.firehose");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ARCHIVE_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), 60000);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x00);
}

static void
fu_qc_firehose_usb_device_class_init(FuQcFirehoseUsbDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_qc_firehose_usb_device_to_string;
	device_class->probe = fu_qc_firehose_usb_device_probe;
	device_class->replace = fu_qc_firehose_usb_device_replace;
	device_class->write_firmware = fu_qc_firehose_usb_device_impl_write_firmware;
	device_class->attach = fu_qc_firehose_usb_device_attach;
	device_class->set_progress = fu_qc_firehose_usb_device_set_progress;
}
