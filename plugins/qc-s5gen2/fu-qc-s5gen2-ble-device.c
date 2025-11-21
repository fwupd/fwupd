/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qc-s5gen2-ble-device.h"
#include "fu-qc-s5gen2-ble-struct.h"
#include "fu-qc-s5gen2-device.h"
#include "fu-qc-s5gen2-impl.h"
#include "fu-qc-s5gen2-struct.h"

#define FU_QC_S5GEN2_GAIA_V3_SUPPORTED_VERSION_MAJOR 3

#define FU_QC_S5GEN2_GAIA_V3_DEFAULT_VENDOR 0x001d
#define FU_QC_S5GEN2_GAIA_V3_HDR_SZ	    4

#define FU_QC_S5GEN2_BLE_DEVICE_SEND "00001101-d102-11e1-9b23-00025b00a5a5"
#define FU_QC_S5GEN2_BLE_DEVICE_RECV "00001102-d102-11e1-9b23-00025b00a5a5"
#define FU_QC_S5GEN2_BLE_DEVICE_DATA "00001103-d102-11e1-9b23-00025b00a5a5"

#define FU_QC_S5GEN2_BLE_DEVICE_TIMEOUT 15000 /* ms */

#define FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ 256

#define FU_QC_S5GEN2_GAIA_PROTOCOL_VERSION 0x03

#define FU_QC_S5GEN2_BLE_DEVICE_AQUIRE_RETRIES 25
#define FU_QC_S5GEN2_BLE_DEVICE_AQUIRE_DELAY   200

typedef struct {
	guint8 core;
	guint8 dfu;
} gaia_features_version_t;

struct _FuQcS5gen2BleDevice {
	FuBluezDevice parent_instance;
	guint16 vid_v3;
	FuIOChannel *io_cmd;
	gint32 mtu;
	gaia_features_version_t feature;
};

static void
fu_qc_s5gen2_ble_device_impl_iface_init(FuQcS5gen2ImplInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuQcS5gen2BleDevice,
			fu_qc_s5gen2_ble_device,
			FU_TYPE_BLUEZ_DEVICE,
			G_IMPLEMENT_INTERFACE(FU_TYPE_QC_S5GEN2_IMPL,
					      fu_qc_s5gen2_ble_device_impl_iface_init))

static gboolean
fu_qc_s5gen2_ble_device_notify_release(FuQcS5gen2BleDevice *self, GError **error)
{
	if (self->io_cmd == NULL)
		return (TRUE);

	g_object_unref(self->io_cmd);
	self->io_cmd = NULL;
	self->mtu = 0;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_notify_acquire(FuQcS5gen2BleDevice *self, GError **error)
{
	if (self->io_cmd != NULL)
		return (TRUE);

	self->io_cmd = fu_bluez_device_notify_acquire(FU_BLUEZ_DEVICE(self),
						      FU_QC_S5GEN2_BLE_DEVICE_RECV,
						      &(self->mtu),
						      error);
	if (self->io_cmd == NULL) {
		self->mtu = 0;
		return (FALSE);
	}
	g_debug("MTU = %d", self->mtu);
	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_send(FuQcS5gen2BleDevice *self,
			     guint8 *data,
			     gsize data_len,
			     GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	buf = g_byte_array_append(buf, data, data_len);

	if (!fu_bluez_device_write(FU_BLUEZ_DEVICE(self), FU_QC_S5GEN2_BLE_DEVICE_SEND, buf, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_recv(FuQcS5gen2BleDevice *self,
			     guint8 *data_in,
			     gsize data_len,
			     gsize *read_len,
			     GError **error)
{
	if (!fu_io_channel_read_raw(self->io_cmd,
				    data_in,
				    (self->mtu < (gint32)data_len) ? (gsize)self->mtu : data_len,
				    read_len,
				    FU_QC_S5GEN2_BLE_DEVICE_TIMEOUT,
				    FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
				    error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "Read from device:", data_in, *read_len);

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_msg_out(FuQcS5gen2Impl *impl, guint8 *data, gsize data_len, GError **error)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(impl);
	guint8 buf[FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_gaia_v3_upgrade_control_cmd_new();
	g_autoptr(GByteArray) validate = NULL;

	fu_struct_qc_gaia_v3_upgrade_control_cmd_set_vendor_id(req, self->vid_v3);

	g_byte_array_append(req, data, data_len);

	if (!fu_qc_s5gen2_ble_device_send(self, req->data, req->len, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_recv(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	validate = fu_struct_qc_gaia_v3_upgrade_control_ack_parse(buf, read_len, 0, error);
	if (validate == NULL) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_msg_in(FuQcS5gen2Impl *impl,
			       guint8 *data_in,
			       gsize data_len,
			       gsize *read_len,
			       GError **error)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(impl);
	gsize bufsz = 0;
	g_autofree guint8 *buf = NULL;

	bufsz = ((gsize)self->mtu < data_len + FU_QC_S5GEN2_GAIA_V3_HDR_SZ)
		    ? (gsize)self->mtu
		    : data_len + FU_QC_S5GEN2_GAIA_V3_HDR_SZ;

	buf = g_malloc0(bufsz);

	if (!fu_io_channel_read_raw(self->io_cmd,
				    buf,
				    bufsz,
				    read_len,
				    FU_QC_S5GEN2_BLE_DEVICE_TIMEOUT,
				    FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
				    error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "Read from device:", buf, *read_len);
	if (*read_len <= FU_QC_S5GEN2_GAIA_V3_HDR_SZ) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "got %lu bytes, less or equal to GAIA header",
			    (unsigned long)*read_len);
		return FALSE;
	}
	/* don't need GAIA header for upper layer */
	*read_len -= FU_QC_S5GEN2_GAIA_V3_HDR_SZ;
	if (!fu_memcpy_safe(data_in,
			    data_len,
			    0,
			    buf,
			    bufsz,
			    FU_QC_S5GEN2_GAIA_V3_HDR_SZ,
			    *read_len,
			    error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_req_connect(FuQcS5gen2Impl *impl, GError **error)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(impl);
	guint8 buf[FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_gaia_v3_upgrade_connect_cmd_new();
	g_autoptr(GByteArray) validate = NULL;

	fu_struct_qc_gaia_v3_upgrade_connect_cmd_set_vendor_id(req, self->vid_v3);

	if (!fu_qc_s5gen2_ble_device_notify_acquire(self, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_send(self, req->data, req->len, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_recv(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	validate = fu_struct_qc_gaia_v3_upgrade_connect_ack_parse(buf, read_len, 0, error);
	if (validate == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_req_disconnect(FuQcS5gen2Impl *impl, GError **error)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(impl);
	guint8 buf[FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_gaia_v3_upgrade_disconnect_cmd_new();
	g_autoptr(GByteArray) validate = NULL;

	fu_struct_qc_gaia_v3_upgrade_disconnect_cmd_set_vendor_id(req, self->vid_v3);

	if (!fu_qc_s5gen2_ble_device_send(self, req->data, req->len, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_recv(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	validate = fu_struct_qc_gaia_v3_upgrade_disconnect_ack_parse(buf, read_len, 0, error);
	if (validate == NULL)
		return FALSE;

	return fu_qc_s5gen2_ble_device_notify_release(self, error);
}

static gboolean
fu_qc_s5gen2_ble_device_data_size(FuQcS5gen2Impl *impl, gsize *data_sz, GError **error)
{
	/* TODO: atm for GAIA only */
	gsize headers_sz = FU_STRUCT_QC_DATA_SIZE + FU_QC_S5GEN2_GAIA_V3_HDR_SZ + 3;
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(impl);

	if ((gsize)self->mtu <= headers_sz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "MTU is not sufficient");
		return FALSE;
	}

	*data_sz = (gsize)self->mtu - headers_sz;
	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_get_api(FuQcS5gen2BleDevice *self, GError **error)
{
	guint8 buf[FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ] = {0};
	gsize read_len;
	guint8 api_major;
	guint8 api_minor;
	g_autoptr(GByteArray) req = fu_struct_qc_gaia_v3_api_req_new();
	g_autoptr(GByteArray) resp = NULL;

	fu_struct_qc_gaia_v3_api_req_set_vendor_id(req, self->vid_v3);

	if (!fu_qc_s5gen2_ble_device_send(self, req->data, req->len, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_recv(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	resp = fu_struct_qc_gaia_v3_api_parse(buf, read_len, 0, error);
	if (resp == NULL)
		return FALSE;

	api_major = fu_struct_qc_gaia_v3_api_get_major(resp);
	api_minor = fu_struct_qc_gaia_v3_api_get_minor(resp);

	if (api_major < FU_QC_S5GEN2_GAIA_V3_SUPPORTED_VERSION_MAJOR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "GAIA protocol %u.%u is not supported",
			    api_major,
			    api_minor);
		return FALSE;
	}
	g_debug("GAIA API version: %u.%u", api_major, api_minor);
	return TRUE;
}

/* read the list of supported features from device */
static gboolean
fu_qc_s5gen2_ble_device_get_features(FuQcS5gen2BleDevice *self, gboolean next, GError **error)
{
	guint8 buf[FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_gaia_v3_supported_features_req_new();
	g_autoptr(GByteArray) resp = NULL;

	fu_struct_qc_gaia_v3_supported_features_req_set_vendor_id(req, self->vid_v3);

	fu_struct_qc_gaia_v3_supported_features_req_set_command(
	    req,
	    (next == FALSE) ? FU_QC_GAIA_V3_CMD_GET_SUPPORTED_FEATURES_REQ
			    : FU_QC_GAIA_V3_CMD_GET_SUPPORTED_FEATURES_NEXT_REQ);

	if (!fu_qc_s5gen2_ble_device_send(self, req->data, req->len, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_recv(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	resp = fu_struct_qc_gaia_v3_supported_features_parse(buf, read_len, 0, error);
	if (resp == NULL)
		return FALSE;

	/* must be odd: header 5B + feature pairs */
	if ((read_len & 0x01) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "got incorrect features list");
		return FALSE;
	}

	/* parse feature:version pairs */
	for (gsize i = FU_STRUCT_QC_GAIA_V3_SUPPORTED_FEATURES_SIZE;
	     i < read_len && i < sizeof(buf) - 1;
	     i += 2) {
		switch (buf[i]) {
		case FU_QC_GAIA_V3_FEATURES_CORE:
			self->feature.core = buf[i + 1];
			g_debug("Core feature version: %u", self->feature.core);
			break;
		case FU_QC_GAIA_V3_FEATURES_DFU:
			self->feature.dfu = buf[i + 1];
			g_debug("DFU feature version: %u", self->feature.dfu);
			break;
		default:
			break;
		}
	}

	/* request the rest of the list */
	if (fu_struct_qc_gaia_v3_supported_features_get_more_features(resp) == FU_QC_MORE_MORE)
		return fu_qc_s5gen2_ble_device_get_features(self, TRUE, error);

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_get_serial(FuQcS5gen2BleDevice *self, GError **error)
{
	guint8 buf[FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_gaia_v3_serial_req_new();
	g_autoptr(GByteArray) validate = NULL;
	g_autofree gchar *serial = NULL;

	fu_struct_qc_gaia_v3_serial_req_set_vendor_id(req, self->vid_v3);

	if (!fu_qc_s5gen2_ble_device_send(self, req->data, req->len, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_recv(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	/* Check if response is valid */
	validate =
	    fu_struct_qc_gaia_v3_serial_parse(buf, FU_STRUCT_QC_GAIA_V3_SERIAL_SIZE, 0, error);
	if (validate == NULL)
		return FALSE;

	serial = fu_strsafe((gchar *)(buf + FU_STRUCT_QC_GAIA_V3_SERIAL_SIZE),
			    read_len - FU_STRUCT_QC_GAIA_V3_SERIAL_SIZE);

	if (serial != NULL)
		fu_device_set_serial(FU_DEVICE(self), serial);

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_get_variant(FuQcS5gen2BleDevice *self, GError **error)
{
	guint8 buf[FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_gaia_v3_variant_req_new();
	g_autoptr(GByteArray) validate = NULL;
	g_autofree gchar *variant = NULL;

	fu_struct_qc_gaia_v3_variant_req_set_vendor_id(req, self->vid_v3);

	if (!fu_qc_s5gen2_ble_device_send(self, req->data, req->len, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_recv(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	/* check if response is valid */
	validate =
	    fu_struct_qc_gaia_v3_variant_parse(buf, FU_STRUCT_QC_GAIA_V3_VARIANT_SIZE, 0, error);
	if (validate == NULL)
		return FALSE;

	variant = fu_strsafe((gchar *)(buf + FU_STRUCT_QC_GAIA_V3_VARIANT_SIZE),
			     read_len - FU_STRUCT_QC_GAIA_V3_VARIANT_SIZE);

	if (variant == NULL) {
		g_debug("read non-printable device variant, skipping");
		return TRUE;
	}

	/* create the GUID based on variant read from device */
	fu_device_add_instance_str(FU_DEVICE(self), "GAIA", variant);
	fu_device_build_instance_id_full(FU_DEVICE(self),
					 FU_DEVICE_INSTANCE_FLAG_VISIBLE |
					     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "BLUETOOTH",
					 "GAIA",
					 NULL);

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_register_notification(FuQcS5gen2BleDevice *self, GError **error)
{
	guint8 buf[FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ] = {0};
	gsize read_len = 0;
	g_autoptr(GByteArray) req = fu_struct_qc_gaia_v3_register_notification_cmd_new();
	g_autoptr(GByteArray) validate = NULL;

	/* register only for update feature */
	fu_struct_qc_gaia_v3_register_notification_cmd_set_vendor_id(req, self->vid_v3);

	if (!fu_qc_s5gen2_ble_device_send(self, req->data, req->len, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_recv(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	/* Check if response is valid */
	validate = fu_struct_qc_gaia_v3_register_notification_ack_parse(
	    buf,
	    FU_STRUCT_QC_GAIA_V3_REGISTER_NOTIFICATION_ACK_SIZE,
	    0,
	    error);
	if (validate == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_set_transport_protocol(FuQcS5gen2BleDevice *self,
					       guint32 version,
					       GError **error)
{
	guint8 buf[FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_gaia_v3_set_transport_info_req_new();
	g_autoptr(GByteArray) validate = NULL;

	fu_struct_qc_gaia_v3_set_transport_info_req_set_vendor_id(req, self->vid_v3);
	fu_struct_qc_gaia_v3_set_transport_info_req_set_key(req, 0x07);
	fu_struct_qc_gaia_v3_set_transport_info_req_set_value(req, version);

	if (!fu_qc_s5gen2_ble_device_send(self, req->data, req->len, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_recv(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	validate = fu_struct_qc_gaia_v3_set_transport_info_parse(buf, read_len, 0, error);
	if (validate == NULL)
		return FALSE;

	return TRUE;
}

static void
fu_qc_s5gen2_ble_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "GaiaVendorId", self->vid_v3);
	fwupd_codec_string_append_hex(str, idt, "GaiaCoreFeatureVersion", self->feature.core);
	fwupd_codec_string_append_hex(str, idt, "GaiaDfuFeatureVersion", self->feature.dfu);
}

static gboolean
fu_qc_s5gen2_ble_device_notify_acquire_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(device);

	if (!fu_qc_s5gen2_ble_device_notify_release(self, error))
		return FALSE;

	return fu_qc_s5gen2_ble_device_notify_acquire(self, error);
}

static gboolean
fu_qc_s5gen2_ble_device_probe(FuDevice *device, GError **error)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(device);
	g_autofree gchar *vendor_id = NULL;

	if (!FU_DEVICE_CLASS(fu_qc_s5gen2_ble_device_parent_class)->probe(device, error))
		return FALSE;

	/* after reboot the device might appear too fast */
	if (!fu_device_retry_full(device,
				  fu_qc_s5gen2_ble_device_notify_acquire_cb,
				  FU_QC_S5GEN2_BLE_DEVICE_AQUIRE_RETRIES,
				  FU_QC_S5GEN2_BLE_DEVICE_AQUIRE_DELAY,
				  NULL,
				  error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_get_api(self, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_get_features(self, FALSE, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_get_serial(self, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_get_variant(self, error))
		return FALSE;

	if (self->feature.core >= 2) {
		if (!fu_qc_s5gen2_ble_device_set_transport_protocol(
			self,
			FU_QC_S5GEN2_GAIA_PROTOCOL_VERSION,
			error))
			return FALSE;
	}

	/* set vendor ID to avoid update error */
	vendor_id = g_strdup_printf("BLUETOOTH:%04X", self->vid_v3);
	fu_device_add_vendor_id(device, vendor_id);

	if (!fu_qc_s5gen2_ble_device_register_notification(self, error))
		return FALSE;

	return fu_qc_s5gen2_ble_device_notify_release(FU_QC_S5GEN2_BLE_DEVICE(device), error);
}

static gboolean
fu_qc_s5gen2_ble_device_set_quirk_kv(FuDevice *device,
				     const gchar *key,
				     const gchar *value,
				     GError **error)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "QcS5gen2Gaia3VendorId") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->vid_v3 = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_qc_s5gen2_ble_device_init(FuQcS5gen2BleDevice *self)
{
	self->vid_v3 = FU_QC_S5GEN2_GAIA_V3_DEFAULT_VENDOR;
	self->mtu = 0;
	self->feature.core = 0;
	self->feature.dfu = 0;
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_QC_S5GEN2_DEVICE_REMOVE_DELAY);
	fu_device_add_protocol(FU_DEVICE(self), "com.qualcomm.s5gen2");
}

static void
fu_qc_s5gen2_ble_device_finalize(GObject *object)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(object);
	if (self->io_cmd != NULL)
		g_object_unref(self->io_cmd);
	G_OBJECT_CLASS(fu_qc_s5gen2_ble_device_parent_class)->finalize(object);
}

static void
fu_qc_s5gen2_ble_device_impl_iface_init(FuQcS5gen2ImplInterface *iface)
{
	iface->msg_in = fu_qc_s5gen2_ble_device_msg_in;
	iface->msg_out = fu_qc_s5gen2_ble_device_msg_out;
	iface->req_connect = fu_qc_s5gen2_ble_device_req_connect;
	iface->req_disconnect = fu_qc_s5gen2_ble_device_req_disconnect;
	iface->data_size = fu_qc_s5gen2_ble_device_data_size;
}

static void
fu_qc_s5gen2_ble_device_class_init(FuQcS5gen2BleDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_qc_s5gen2_ble_device_finalize;
	device_class->to_string = fu_qc_s5gen2_ble_device_to_string;
	device_class->probe = fu_qc_s5gen2_ble_device_probe;
	device_class->set_quirk_kv = fu_qc_s5gen2_ble_device_set_quirk_kv;
}
