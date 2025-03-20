/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qc-firehose-impl.h"
#include "fu-qc-firehose-raw-device.h"
#include "fu-qc-firehose-struct.h"

#define FU_QC_FIREHOSE_RAW_DEVICE_RAW_BUFFER_SIZE (4 * 1024)

#define FU_QC_FIREHOSE_RAW_DEVICE_TIMEOUT_MS 500

struct _FuQcFirehoseRawDevice {
	FuUdevDevice parent_instance;
	FuQcFirehoseFunctions supported_functions;
};

static void
fu_qc_firehose_raw_device_impl_iface_init(FuQcFirehoseImplInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuQcFirehoseRawDevice,
			fu_qc_firehose_raw_device,
			FU_TYPE_UDEV_DEVICE,
			G_IMPLEMENT_INTERFACE(FU_TYPE_QC_FIREHOSE_IMPL,
					      fu_qc_firehose_raw_device_impl_iface_init))

static void
fu_qc_firehose_raw_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuQcFirehoseRawDevice *self = FU_QC_FIREHOSE_RAW_DEVICE(device);
	g_autofree gchar *functions = fu_qc_firehose_functions_to_string(self->supported_functions);
	fwupd_codec_string_append(str, idt, "SupportedFunctions", functions);
}

static gboolean
fu_qc_firehose_raw_device_impl_has_function(FuQcFirehoseImpl *impl, FuQcFirehoseFunctions func)
{
	FuQcFirehoseRawDevice *self = FU_QC_FIREHOSE_RAW_DEVICE(impl);
	return (self->supported_functions & func) > 0;
}

static void
fu_qc_firehose_raw_device_impl_add_function(FuQcFirehoseImpl *impl, FuQcFirehoseFunctions func)
{
	FuQcFirehoseRawDevice *self = FU_QC_FIREHOSE_RAW_DEVICE(impl);
	self->supported_functions |= func;
}

static gboolean
fu_qc_firehose_raw_device_impl_write_firmware(FuDevice *device,
					      FuFirmware *firmware,
					      FuProgress *progress,
					      FwupdInstallFlags flags,
					      GError **error)
{
	FuQcFirehoseRawDevice *self = FU_QC_FIREHOSE_RAW_DEVICE(device);
	return fu_qc_firehose_impl_write_firmware(FU_QC_FIREHOSE_IMPL(self),
						  firmware,
						  FALSE,
						  progress,
						  error);
}

static gboolean
fu_qc_firehose_raw_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuQcFirehoseRawDevice *self = FU_QC_FIREHOSE_RAW_DEVICE(device);

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
fu_qc_firehose_raw_device_probe(FuDevice *device, GError **error)
{
	const gchar *device_file;
	g_autoptr(FuDevice) pci_parent = NULL;

	/* sanity check */
	device_file = fu_udev_device_get_device_file(FU_UDEV_DEVICE(device));
	if (device_file == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no device file");
		return FALSE;
	}
	if (!g_pattern_match_simple("/dev/wwan*firehose*", device_file)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not a firehose wwan port");
		return FALSE;
	}

	/* use the PCI parent to set the physical ID */
	pci_parent = fu_device_get_backend_parent_with_subsystem(device, "pci", error);
	if (pci_parent == NULL)
		return FALSE;
	if (!fu_device_probe(pci_parent, error))
		return FALSE;
	fu_device_incorporate(
	    device,
	    pci_parent,
	    FU_DEVICE_INCORPORATE_FLAG_SUPERCLASS | FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID |
		FU_DEVICE_INCORPORATE_FLAG_INSTANCE_IDS | FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS);

	/* FuUdevDevice->probe */
	return FU_DEVICE_CLASS(fu_qc_firehose_raw_device_parent_class)->probe(device, error);
}

static void
fu_qc_firehose_raw_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static GByteArray *
fu_qc_firehose_raw_device_impl_read(FuQcFirehoseImpl *impl, guint timeout_ms, GError **error)
{
	FuQcFirehoseRawDevice *self = FU_QC_FIREHOSE_RAW_DEVICE(impl);
	gsize actual_len = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_set_size(buf, FU_QC_FIREHOSE_RAW_DEVICE_RAW_BUFFER_SIZE, 0x00);
	if (!fu_udev_device_read(FU_UDEV_DEVICE(self),
				 buf->data,
				 buf->len,
				 &actual_len,
				 timeout_ms,
				 FU_IO_CHANNEL_FLAG_NONE,
				 error)) {
		g_prefix_error(error, "failed to do bulk transfer (read): ");
		return NULL;
	}

	g_byte_array_set_size(buf, actual_len);
	fu_dump_raw(G_LOG_DOMAIN, "rx packet", buf->data, buf->len);
	return g_steal_pointer(&buf);
}

static gboolean
fu_qc_firehose_raw_device_impl_write(FuQcFirehoseImpl *impl,
				     const guint8 *buf,
				     gsize bufsz,
				     GError **error)
{
	FuQcFirehoseRawDevice *self = FU_QC_FIREHOSE_RAW_DEVICE(impl);
	return fu_udev_device_write(FU_UDEV_DEVICE(self),
				    buf,
				    bufsz,
				    FU_QC_FIREHOSE_RAW_DEVICE_TIMEOUT_MS,
				    FU_IO_CHANNEL_FLAG_FLUSH_INPUT,
				    error);
}

static void
fu_qc_firehose_raw_device_impl_iface_init(FuQcFirehoseImplInterface *iface)
{
	iface->read = fu_qc_firehose_raw_device_impl_read;
	iface->write = fu_qc_firehose_raw_device_impl_write;
	iface->has_function = fu_qc_firehose_raw_device_impl_has_function;
	iface->add_function = fu_qc_firehose_raw_device_impl_add_function;
}

static void
fu_qc_firehose_raw_device_init(FuQcFirehoseRawDevice *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_name(FU_DEVICE(self), "Firehose");
	fu_device_add_protocol(FU_DEVICE(self), "com.qualcomm.firehose");
	fu_device_set_version(FU_DEVICE(self), "0.0");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ARCHIVE_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_retry_add_recovery(FU_DEVICE(self), FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, NULL);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

static void
fu_qc_firehose_raw_device_class_init(FuQcFirehoseRawDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_qc_firehose_raw_device_to_string;
	device_class->write_firmware = fu_qc_firehose_raw_device_impl_write_firmware;
	device_class->set_progress = fu_qc_firehose_raw_device_set_progress;
	device_class->probe = fu_qc_firehose_raw_device_probe;
	device_class->attach = fu_qc_firehose_raw_device_attach;
}
