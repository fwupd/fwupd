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

#define FU_FOCAL_MOC_SIGNED_DEVICE_PID_IAP	    0x0201
#define FU_FOCAL_MOC_SIGNED_DEVICE_INSTALL_DURATION 25

#define FU_FOCAL_MOC_SIGNED_DEVICE_HOST_KEY_EVENT_ID "FuFocalMocTransportHostKey"

static gboolean
fu_focal_moc_signed_device_is_iap(FuFocalMocSignedDevice *self)
{
	return fu_device_get_pid(FU_DEVICE(self)) == FU_FOCAL_MOC_SIGNED_DEVICE_PID_IAP;
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

static gboolean
fu_focal_moc_signed_device_load_host_key(FuFocalMocTransportImpl *impl,
					 guint8 *host_key,
					 gboolean *found,
					 GError **error)
{
	FuDevice *device = FU_DEVICE(impl);
	FuDeviceEvent *event;
	g_autoptr(GBytes) blob = NULL;

	/* only a replay reuses a recorded key; a live device negotiates fresh */
	*found = FALSE;
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;
	event = fu_device_load_event(device, FU_FOCAL_MOC_SIGNED_DEVICE_HOST_KEY_EVENT_ID, error);
	if (event == NULL)
		return FALSE;
	blob = fu_device_event_get_bytes(event, "Data", error);
	if (blob == NULL)
		return FALSE;
	if (g_bytes_get_size(blob) != FU_FOCAL_MOC_TRANSPORT_HOST_KEY_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "recorded host key size invalid: 0x%zx",
			    g_bytes_get_size(blob));
		return FALSE;
	}
	if (!fu_memcpy_safe(host_key,
			    FU_FOCAL_MOC_TRANSPORT_HOST_KEY_SIZE,
			    0,
			    g_bytes_get_data(blob, NULL),
			    g_bytes_get_size(blob),
			    0,
			    FU_FOCAL_MOC_TRANSPORT_HOST_KEY_SIZE,
			    error))
		return FALSE;
	*found = TRUE;

	/* success */
	return TRUE;
}

static gboolean
fu_focal_moc_signed_device_save_host_key(FuFocalMocTransportImpl *impl,
					 const guint8 *host_key,
					 GError **error)
{
	FuDevice *device = FU_DEVICE(impl);
	FuDeviceEvent *event;

	/* only journal the ephemeral key while recording an emulation; the event
	 * stores the private scalar, which is acceptable only because the key is
	 * ephemeral and scoped to the recorded session */
	if (!fu_context_has_flag(fu_device_get_context(device), FU_CONTEXT_FLAG_SAVE_EVENTS))
		return TRUE;
	event = fu_device_save_event(device, FU_FOCAL_MOC_SIGNED_DEVICE_HOST_KEY_EVENT_ID);
	fu_device_event_set_data(event, "Data", host_key, FU_FOCAL_MOC_TRANSPORT_HOST_KEY_SIZE);

	/* success */
	return TRUE;
}

static void
fu_focal_moc_signed_device_transport_impl_iface_init(FuFocalMocTransportImplInterface *iface)
{
	iface->write = fu_focal_moc_signed_device_transport_write;
	iface->read = fu_focal_moc_signed_device_transport_read;
	iface->load_host_key = fu_focal_moc_signed_device_load_host_key;
	iface->save_host_key = fu_focal_moc_signed_device_save_host_key;
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
}
