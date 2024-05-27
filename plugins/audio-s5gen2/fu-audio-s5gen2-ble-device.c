/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-audio-s5gen2-ble-device.h"
#include "fu-audio-s5gen2-ble-struct.h"
#include "fu-audio-s5gen2-device.h"
#include "fu-audio-s5gen2-hid-struct.h"
#include "fu-audio-s5gen2-impl.h"

#define FU_QC_S5GEN2_GAIA_V2_DEFAULT_VENDOR 0x000a
#define FU_QC_S5GEN2_GAIA_V3_DEFAULT_VENDOR 0x001d

#define FU_QC_S5GEN2_BLE_DEVICE_SEND "00001101-d102-11e1-9b23-00025b00a5a5"
#define FU_QC_S5GEN2_BLE_DEVICE_RECV "00001102-d102-11e1-9b23-00025b00a5a5"
#define FU_QC_S5GEN2_BLE_DEVICE_DATA "00001103-d102-11e1-9b23-00025b00a5a5"

#define FU_QC_S5GEN2_BLE_DEVICE_TIMEOUT 300 /* ms */

#define FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ 256

#define FU_QC_S5GEN2_GAIA_PROTOCOL_VERSION 0x03

typedef struct {
	guint8 major;
	guint8 minor;
} gaia_api_version_t;

typedef struct {
	guint8 core;
	guint8 dfu;
} gaia_features_version_t;

struct _FuQcS5gen2BleDevice {
	FuBluezDevice parent_instance;
	guint16 vid_v2;
	guint16 vid_v3;
	gaia_api_version_t api_version;
	FuIOChannel *io_cmd;
	FuIOChannel *io_data;
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
fu_qc_s5gen2_ble_device_send_raw(FuQcS5gen2BleDevice *self,
				 guint8 *data,
				 gsize data_len,
				 GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	buf = g_byte_array_append(buf, data, data_len);

	fu_dump_raw(G_LOG_DOMAIN, "Write to device:", data, data_len);
	if (!fu_bluez_device_write(FU_BLUEZ_DEVICE(self), FU_QC_S5GEN2_BLE_DEVICE_SEND, buf, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_recv_raw(FuQcS5gen2BleDevice *self,
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

	if (!fu_qc_s5gen2_ble_device_send_raw(self, req->data, req->len, error))
		return TRUE;

	if (!fu_qc_s5gen2_ble_device_recv_raw(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	validate = fu_struct_qc_gaia_v3_upgrade_control_ack_parse(buf, read_len, 0, error);
	if (validate == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_msg_in(FuQcS5gen2Impl *impl,
			       guint8 *data_in,
			       gsize data_len,
			       GError **error)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(impl);
	gsize read_len;

	if (!fu_io_channel_read_raw(self->io_cmd,
				    data_in,
				    (self->mtu < (gint32)data_len) ? (gsize)self->mtu : data_len,
				    &read_len,
				    FU_QC_S5GEN2_BLE_DEVICE_TIMEOUT,
				    FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
				    error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "Read from device:", data_in, read_len);

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_msg_cmd(FuQcS5gen2Impl *impl, guint8 *data, gsize data_len, GError **error)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(impl);
	guint8 buf[FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_gaia_v3_upgrade_control_cmd_new();
	g_autoptr(GByteArray) validate = NULL;

	fu_struct_qc_gaia_v3_upgrade_control_cmd_set_vendor_id(req, self->vid_v3);

	g_byte_array_append(req, data, data_len);

	if (!fu_qc_s5gen2_ble_device_send_raw(self, req->data, req->len, error))
		return TRUE;

	if (!fu_qc_s5gen2_ble_device_recv_raw(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	validate = fu_struct_qc_gaia_v3_upgrade_control_ack_parse(buf, read_len, 0, error);
	if (validate == NULL)
		return FALSE;

	return TRUE;
}

/* Unable to get F/W version via GAIA protocol.
 * Return Application version instead. */
static gchar *
fu_qc_s5gen2_ble_device_get_version(FuQcS5gen2BleDevice *self, GError **error)
{
	guint8 buf[256] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_gaia_v3_app_version_req_new();
	g_autoptr(GByteArray) resp = NULL;
	g_autoptr(GByteArray) validate = NULL;

	fu_struct_qc_gaia_v3_app_version_req_set_vendor_id(req, self->vid_v3);

	if (!fu_qc_s5gen2_ble_device_send_raw(self, req->data, req->len, error))
		return NULL;

	if (!fu_qc_s5gen2_ble_device_recv_raw(self, buf, sizeof(buf), &read_len, error))
		return NULL;

	validate = fu_struct_qc_gaia_v3_app_version_parse(buf, read_len, 0, error);
	if (validate == NULL)
		return NULL;

	return g_strndup((gchar *)(buf + FU_STRUCT_QC_GAIA_V3_APP_VERSION_SIZE),
			 read_len - FU_STRUCT_QC_GAIA_V3_APP_VERSION_SIZE);
}

static gboolean
fu_qc_s5gen2_ble_device_get_api(FuQcS5gen2BleDevice *self, GError **error)
{
	guint8 buf[FU_QC_S5GEN2_BLE_DEVICE_BUFFER_SZ] = {0};
	gsize read_len;
	g_autoptr(GByteArray) req = fu_struct_qc_gaia_v3_api_req_new();
	g_autoptr(GByteArray) resp = NULL;

	fu_struct_qc_gaia_v3_api_req_set_vendor_id(req, self->vid_v3);

	if (!fu_qc_s5gen2_ble_device_send_raw(self, req->data, req->len, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_recv_raw(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	resp = fu_struct_qc_gaia_v3_api_parse(buf, read_len, 0, error);
	if (resp == NULL)
		return FALSE;

	self->api_version.major = fu_struct_qc_gaia_v3_api_get_major(resp);
	self->api_version.minor = fu_struct_qc_gaia_v3_api_get_minor(resp);

	if (self->api_version.major < 3) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "GAIA protocol %u.%u is not supported",
			    self->api_version.major,
			    self->api_version.minor);
		return FALSE;
	}

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

	if (!fu_qc_s5gen2_ble_device_send_raw(self, req->data, req->len, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_recv_raw(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	resp = fu_struct_qc_gaia_v3_supported_features_parse(buf, read_len, 0, error);
	if (resp == NULL)
		return FALSE;

	/* must be odd: header 5B + feature pairs */
	if (!(read_len & 0x01)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "got incorrect features list");
		return FALSE;
	}

	/* parse feature:version pairs */
	for (gsize i = FU_STRUCT_QC_GAIA_V3_SUPPORTED_FEATURES_SIZE; i < read_len; i += 2) {
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

	if (!fu_qc_s5gen2_ble_device_send_raw(self, req->data, req->len, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_recv_raw(self, buf, sizeof(buf), &read_len, error))
		return FALSE;

	/* Check if response is valid */
	validate =
	    fu_struct_qc_gaia_v3_serial_parse(buf, FU_STRUCT_QC_GAIA_V3_SERIAL_SIZE, 0, error);
	if (validate == NULL)
		return FALSE;

	serial = g_strndup((gchar *)(buf + FU_STRUCT_QC_GAIA_V3_SERIAL_SIZE),
			   read_len - FU_STRUCT_QC_GAIA_V3_SERIAL_SIZE);

	fu_device_set_serial(FU_DEVICE(self), serial);

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

	if (!fu_qc_s5gen2_ble_device_send_raw(self, req->data, req->len, error))
		return TRUE;

	if (!fu_qc_s5gen2_ble_device_recv_raw(self, buf, sizeof(buf), &read_len, error))
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
	g_autofree gchar *api_version;

	fwupd_codec_string_append_hex(str, idt, "GAIAv2 Vendor ID", self->vid_v2);
	fwupd_codec_string_append_hex(str, idt, "GAIAv3 Vendor ID", self->vid_v3);
	api_version = g_strdup_printf("%u.%u", self->api_version.major, self->api_version.minor);
	fwupd_codec_string_append(str, idt, "GAIA API version", api_version);
	fwupd_codec_string_append_hex(str, idt, "GAIA Core feature version", self->feature.core);
	fwupd_codec_string_append_hex(str, idt, "GAIA DFU feature version", self->feature.dfu);
}

static gboolean
fu_qc_s5gen2_ble_device_probe(FuDevice *device, GError **error)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(device);
	g_autofree gchar *version = NULL;
	g_autofree gchar *vendor_id = NULL;

	if (!FU_DEVICE_CLASS(fu_qc_s5gen2_ble_device_parent_class)->probe(device, error))
		return FALSE;

	self->io_cmd = fu_bluez_device_notify_acquire(FU_BLUEZ_DEVICE(device),
						      FU_QC_S5GEN2_BLE_DEVICE_RECV,
						      &(self->mtu),
						      error);
	if (self->io_cmd == NULL)
		return (FALSE);

	if (!fu_qc_s5gen2_ble_device_get_api(self, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_get_features(self, FALSE, error))
		return FALSE;

	if (!fu_qc_s5gen2_ble_device_get_serial(self, error))
		return FALSE;

	version = fu_qc_s5gen2_ble_device_get_version(self, error);
	if (version == NULL)
		return FALSE;

	if (self->feature.core >= 2)
		if (!fu_qc_s5gen2_ble_device_set_transport_protocol(
			self,
			FU_QC_S5GEN2_GAIA_PROTOCOL_VERSION,
			error))
			return FALSE;

	// set vendor ID to avoid update error
	vendor_id = g_strdup_printf("BLUETOOTH:%04X", self->vid_v3);
	fu_device_add_vendor_id(device, vendor_id);

	return TRUE;
}

static gboolean
fu_qc_s5gen2_ble_device_set_quirk_kv(FuDevice *device,
				     const gchar *key,
				     const gchar *value,
				     GError **error)
{
	FuQcS5gen2BleDevice *self = FU_QC_S5GEN2_BLE_DEVICE(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "AudioS5gen2Gaia2VendorId") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		self->vid_v2 = tmp;
		return TRUE;
	}

	if (g_strcmp0(key, "AudioS5gen2Gaia3VendorId") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
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
	self->vid_v2 = FU_QC_S5GEN2_GAIA_V2_DEFAULT_VENDOR;
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
	g_autoptr(GError) error = NULL;

	if (self->io_cmd != NULL) {
		g_object_unref(self->io_cmd);
		self->io_cmd = NULL;
		self->mtu = 0;
	}
	if (self->io_data != NULL) {
		g_object_unref(self->io_data);
		self->io_data = NULL;
		self->mtu = 0;
	}

	G_OBJECT_CLASS(fu_qc_s5gen2_ble_device_parent_class)->finalize(object);
}

static void
fu_qc_s5gen2_ble_device_impl_iface_init(FuQcS5gen2ImplInterface *iface)
{
	iface->msg_in = fu_qc_s5gen2_ble_device_msg_in;
	iface->msg_out = fu_qc_s5gen2_ble_device_msg_out;
	iface->msg_cmd = fu_qc_s5gen2_ble_device_msg_cmd;
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
