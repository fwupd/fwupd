/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-common.h"
#include "fu-focal-moc-firmware.h"
#include "fu-focal-moc-signed-device.h"
#include "fu-focal-moc-struct.h"
#include "fu-focal-moc-transport.h"

struct _FuFocalMocSignedDevice {
	FuFocalMocDevice parent_instance;
	FuFocalMocTransport *transport;
};

static void
fu_focal_moc_signed_device_transport_impl_iface_init(FuFocalMocTransportImplInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuFocalMocSignedDevice,
			fu_focal_moc_signed_device,
			FU_TYPE_FOCAL_MOC_DEVICE,
			G_IMPLEMENT_INTERFACE(FU_TYPE_FOCAL_MOC_TRANSPORT_IMPL,
					      fu_focal_moc_signed_device_transport_impl_iface_init))

#define FU_FOCAL_MOC_SIGNED_DEVICE_INSTALL_DURATION 25

/* the plaintext IAP interface, set from the quirk file */
#define FU_FOCAL_MOC_SIGNED_DEVICE_FLAG_IS_IAP "is-iap"

/* a valid P-256 keypair packed as x||y||scalar; only used for emulations,
 * so secrecy is not required */
#define FU_FOCAL_MOC_SIGNED_DEVICE_EMULATION_HOST_KEY                                              \
	"509507d9afe7885875ec3e82f27f3f26b101438117aa843f827b3cc8ff3a9cbb"                         \
	"1376086829ab873a45e968a030485bdf98f2766b35fda2496c43e9606392e337"                         \
	"59567a7e2a43194bdef67291ed1273a5d677258d48662f282467a710f37923f2"

static gboolean
fu_focal_moc_signed_device_is_iap(FuFocalMocSignedDevice *self)
{
	return fu_device_has_private_flag(FU_DEVICE(self), FU_FOCAL_MOC_SIGNED_DEVICE_FLAG_IS_IAP);
}

static void
fu_focal_moc_signed_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuFocalMocSignedDevice *self = FU_FOCAL_MOC_SIGNED_DEVICE(device);

	fwupd_codec_string_append_bool(str,
				       idt,
				       "TransportActive",
				       fu_focal_moc_transport_is_active(self->transport));
}

static gboolean
fu_focal_moc_signed_device_transport_write(FuFocalMocTransportImpl *impl,
					   const guint8 *buf,
					   gsize bufsz,
					   guint timeout_ms,
					   GError **error)
{
	return fu_focal_moc_device_send_raw(FU_FOCAL_MOC_DEVICE(impl),
					    buf,
					    bufsz,
					    timeout_ms,
					    error);
}

static GByteArray *
fu_focal_moc_signed_device_transport_read(FuFocalMocTransportImpl *impl,
					  guint timeout_ms,
					  GError **error)
{
	return fu_focal_moc_device_receive_raw(FU_FOCAL_MOC_DEVICE(impl), timeout_ms, error);
}

static void
fu_focal_moc_signed_device_transport_impl_iface_init(FuFocalMocTransportImplInterface *iface)
{
	iface->write = fu_focal_moc_signed_device_transport_write;
	iface->read = fu_focal_moc_signed_device_transport_read;
}

static GByteArray *
fu_focal_moc_signed_device_command(FuFocalMocDevice *device,
				   guint8 command,
				   const guint8 *payload,
				   gsize payload_sz,
				   guint timeout_ms,
				   guint8 *status,
				   GError **error)
{
	FuFocalMocSignedDevice *self = FU_FOCAL_MOC_SIGNED_DEVICE(device);

	/* the IAP is plaintext, so a device stuck in IAP stays recoverable */
	if (fu_focal_moc_signed_device_is_iap(self)) {
		return FU_FOCAL_MOC_DEVICE_CLASS(fu_focal_moc_signed_device_parent_class)
		    ->command(device, command, payload, payload_sz, timeout_ms, status, error);
	}
	/* make the ephemeral key predictable, so a recording and its replay
	 * derive identical session keys; the context flag covers recording,
	 * where the device flag is not yet set during setup */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		g_autoptr(GBytes) host_key =
		    fu_bytes_from_string(FU_FOCAL_MOC_SIGNED_DEVICE_EMULATION_HOST_KEY, error);
		if (host_key == NULL)
			return NULL;
		fu_focal_moc_transport_set_fixed_host_key(self->transport,
							  g_bytes_get_data(host_key, NULL),
							  g_bytes_get_size(host_key));
	} else {
		fu_focal_moc_transport_clear_fixed_host_key(self->transport);
	}
	if (!fu_focal_moc_transport_handshake(self->transport, error)) {
		g_prefix_error_literal(error, "transport handshake failed: ");
		return NULL;
	}
	return fu_focal_moc_transport_command(self->transport,
					      command,
					      payload,
					      payload_sz,
					      timeout_ms,
					      status,
					      error);
}

static GByteArray *
fu_focal_moc_signed_device_build_soh(FuFocalMocDevice *device,
				     gsize firmware_sz,
				     guint32 crc32,
				     GError **error)
{
	return fu_focal_moc_ymodem_build_soh_v2(firmware_sz, crc32, error);
}

static GByteArray *
fu_focal_moc_signed_device_build_data(FuFocalMocDevice *device,
				      FuFocalMocFrame kind,
				      guint16 sequence,
				      const guint8 *buf,
				      gsize bufsz,
				      GError **error)
{
	return fu_focal_moc_ymodem_build_data_v2(kind, sequence, buf, bufsz, error);
}

static FuInputStream *
fu_focal_moc_signed_device_get_firmware_stream(FuFocalMocDevice *device,
					       FuFirmware *firmware,
					       GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	/* the parsed firmware keeps only the image body as its stream, but the
	 * IAP flashes and verifies the complete signed image, so rebuild the
	 * FWHD header in front of the body */
	blob = fu_firmware_write(firmware, error);
	if (blob == NULL)
		return NULL;
	return fu_memory_input_stream_new_from_bytes(blob);
}

static gboolean
fu_focal_moc_signed_device_ensure_bootloader_layout(FuFocalMocDevice *device, GError **error)
{
	/* the bootloader already uses the unified status layout */
	fu_focal_moc_device_set_iap_status_layout(device, FU_FOCAL_MOC_IAP_STATUS_LAYOUT_ALIGNED);
	return TRUE;
}

static gboolean
fu_focal_moc_signed_device_setup(FuDevice *device, GError **error)
{
	FuFocalMocSignedDevice *self = FU_FOCAL_MOC_SIGNED_DEVICE(device);

	if (!fu_focal_moc_signed_device_is_iap(self) && !fu_focal_moc_transport_is_supported()) {
		fu_device_set_update_error(device, "TransportSec requires GnuTLS 3.8.2 or later");
		return TRUE;
	}
	return FU_DEVICE_CLASS(fu_focal_moc_signed_device_parent_class)->setup(device, error);
}

static gboolean
fu_focal_moc_signed_device_close(FuDevice *device, GError **error)
{
	FuFocalMocSignedDevice *self = FU_FOCAL_MOC_SIGNED_DEVICE(device);

	/* a fresh handshake is negotiated per engine operation, so a stale
	 * session never spans a mode switch */
	fu_focal_moc_transport_teardown(self->transport);
	return FU_DEVICE_CLASS(fu_focal_moc_signed_device_parent_class)->close(device, error);
}

static void
fu_focal_moc_signed_device_init(FuFocalMocSignedDevice *self)
{
	self->transport = fu_focal_moc_transport_new(FU_FOCAL_MOC_TRANSPORT_IMPL(self));
	fu_device_remove_private_flag(FU_DEVICE(self), FU_FOCAL_MOC_DEVICE_FLAG_LEGACY_TRAILER);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_FOCAL_MOC_FIRMWARE);
	fu_device_set_firmware_size_max(FU_DEVICE(self), FU_FOCAL_MOC_FIRMWARE_SIZE_MAX);
	fu_device_set_install_duration(FU_DEVICE(self),
				       FU_FOCAL_MOC_SIGNED_DEVICE_INSTALL_DURATION);
}

static void
fu_focal_moc_signed_device_finalize(GObject *object)
{
	FuFocalMocSignedDevice *self = FU_FOCAL_MOC_SIGNED_DEVICE(object);

	fu_focal_moc_transport_free(self->transport);
	G_OBJECT_CLASS(fu_focal_moc_signed_device_parent_class)->finalize(object);
}

static void
fu_focal_moc_signed_device_class_init(FuFocalMocSignedDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	FuFocalMocDeviceClass *moc_class = FU_FOCAL_MOC_DEVICE_CLASS(klass);
	object_class->finalize = fu_focal_moc_signed_device_finalize;
	device_class->to_string = fu_focal_moc_signed_device_to_string;
	device_class->setup = fu_focal_moc_signed_device_setup;
	device_class->close = fu_focal_moc_signed_device_close;
	moc_class->command = fu_focal_moc_signed_device_command;
	moc_class->build_soh = fu_focal_moc_signed_device_build_soh;
	moc_class->build_data = fu_focal_moc_signed_device_build_data;
	moc_class->get_firmware_stream = fu_focal_moc_signed_device_get_firmware_stream;
	moc_class->ensure_bootloader_layout = fu_focal_moc_signed_device_ensure_bootloader_layout;
	fu_device_register_private_flag(device_class, FU_FOCAL_MOC_SIGNED_DEVICE_FLAG_IS_IAP);
}
