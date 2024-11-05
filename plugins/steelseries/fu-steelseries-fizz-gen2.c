/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-steelseries-fizz-gen2.h"
#include "fu-steelseries-fizz-impl.h"
#include "fu-steelseries-fizz-struct.h"

#define FU_STEELSERIES_FIZZ_GEN2_FILESYSTEM_RECEIVER 0x01U
#define FU_STEELSERIES_FIZZ_GEN2_FILESYSTEM_HEADSET  0x01U
#define FU_STEELSERIES_FIZZ_GEN2_APP_ID		     0x01U

#define FU_STEELSERIES_FIZZ_GEN2_NOT_PAIRED 0x00;
#define FU_STEELSERIES_FIZZ_GEN2_PAIRED	    0x01;

struct _FuSteelseriesFizzGen2 {
	FuSteelseriesDevice parent_instance;
};

static void
fu_steelseries_fizz_gen2_impl_iface_init(FuSteelseriesFizzImplInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuSteelseriesFizzGen2,
			fu_steelseries_fizz_gen2,
			FU_TYPE_STEELSERIES_DEVICE,
			G_IMPLEMENT_INTERFACE(FU_TYPE_STEELSERIES_FIZZ_IMPL,
					      fu_steelseries_fizz_gen2_impl_iface_init))

static gboolean
fu_steelseries_fizz_gen2_request(FuSteelseriesFizzImpl *self, const GByteArray *buf, GError **error)
{
	return fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), buf, error);
}

static GByteArray *
fu_steelseries_fizz_gen2_response(FuSteelseriesFizzImpl *self, GError **error)
{
	return fu_steelseries_device_response(FU_STEELSERIES_DEVICE(self), error);
}

static gchar *
fu_steelseries_fizz_gen2_get_version(FuSteelseriesFizzImpl *self, gboolean tunnel, GError **error)
{
	guint64 version[3] = {0};
	g_autofree gchar *version_raw = NULL;
	g_autoptr(FuStructSteelseriesFizzVersion2Req) st_req =
	    fu_struct_steelseries_fizz_version2_req_new();
	g_autoptr(FuStructSteelseriesVersion2Res) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error))
		return NULL;
	buf_res = fu_steelseries_device_response(FU_STEELSERIES_DEVICE(self), error);
	if (buf_res == NULL)
		return NULL;

	st_res = fu_struct_steelseries_version2_res_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return NULL;
	if (tunnel)
		version_raw = fu_struct_steelseries_version2_res_get_version_device(st_res);
	else
		version_raw = fu_struct_steelseries_version2_res_get_version_receiver(st_res);
	if (version_raw == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "version number provided");
		return NULL;
	}
	if (strlen(version_raw) != FU_STRUCT_STEELSERIES_VERSION2_RES_SIZE_VERSION_RECEIVER) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid version number: %s",
			    version_raw);
		return NULL;
	}

	/* very interesting version format */
	if (version_raw[1] == 0x2E && version_raw[4] == 0x2E && version_raw[8] == 0x2E) {
		/* format triple */
		version[0] = ((version_raw[2] - 0x30) << 4) + (version_raw[3] - 0x30);
		version[1] = ((version_raw[6] - 0x30) << 4) + (version_raw[7] - 0x30);
		version[2] = ((version_raw[9] - 0x30) << 4) + (version_raw[10] - 0x30);
	} else {
		/* format dual */
		version[0] = ((version_raw[7] - 0x30) << 4) + (version_raw[8] - 0x30);
		version[1] = ((version_raw[10] - 0x30) << 4) + (version_raw[11] - 0x30);
		version[2] = 0x00U;
	};

	return g_strdup_printf("%" G_GUINT64_FORMAT "."
			       "%" G_GUINT64_FORMAT "."
			       "%" G_GUINT64_FORMAT "",
			       version[0],
			       version[1],
			       version[2]);
}

static gboolean
fu_steelseries_fizz_gen2_get_battery_level(FuSteelseriesFizzImpl *self,
					   gboolean tunnel,
					   guint8 *level,
					   GError **error)
{
	g_autoptr(FuStructSteelseriesBatteryLevel2Req) st_req =
	    fu_struct_steelseries_battery_level2_req_new();
	g_autoptr(FuStructSteelseriesBatteryLevel2Res) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (!fu_steelseries_fizz_gen2_request(self, st_req, error))
		return FALSE;
	buf_res = fu_steelseries_fizz_gen2_response(self, error);
	if (buf_res == NULL)
		return FALSE;
	st_res =
	    fu_struct_steelseries_battery_level2_res_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	*level = fu_struct_steelseries_battery_level2_res_get_level(st_res);

	/* success */
	return TRUE;
}

static guint8
fu_steelseries_fizz_gen2_get_fs_id(FuSteelseriesFizzImpl *self, gboolean is_receiver)
{
	if (is_receiver)
		return FU_STEELSERIES_FIZZ_GEN2_FILESYSTEM_RECEIVER;
	return FU_STEELSERIES_FIZZ_GEN2_FILESYSTEM_HEADSET;
}

static guint8
fu_steelseries_fizz_gen2_get_file_id(FuSteelseriesFizzImpl *self, gboolean is_receiver)
{
	return FU_STEELSERIES_FIZZ_GEN2_APP_ID;
}

static gboolean
fu_steelseries_fizz_gen2_get_paired_status(FuSteelseriesFizzImpl *self,
					   guint8 *status,
					   GError **error)
{
	g_autoptr(FuStructSteelseriesConnectionStatus2Req) st_req =
	    fu_struct_steelseries_connection_status2_req_new();
	g_autoptr(FuStructSteelseriesConnectionStatus2Res) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (!fu_steelseries_fizz_gen2_request(self, st_req, error))
		return FALSE;
	buf_res = fu_steelseries_fizz_gen2_response(self, error);
	if (buf_res == NULL)
		return FALSE;
	st_res = fu_struct_steelseries_connection_status2_res_parse(buf_res->data,
								    buf_res->len,
								    0x0,
								    error);
	if (st_res == NULL)
		return FALSE;

	/* treat CONNECTED and DISCONNECTED as paired */
	switch (fu_struct_steelseries_connection_status2_res_get_status(st_res)) {
	case FU_STEELSERIES_FIZZ_CONNECTION_CONNECTED:
	case FU_STEELSERIES_FIZZ_CONNECTION_DISCONNECTED:
		*status = FU_STEELSERIES_FIZZ_GEN2_PAIRED;
		break;
	default:
		*status = FU_STEELSERIES_FIZZ_GEN2_NOT_PAIRED;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_gen2_get_connection_status(FuSteelseriesFizzImpl *self,
					       FuSteelseriesFizzConnectionStatus *status,
					       GError **error)
{
	g_autoptr(FuStructSteelseriesConnectionStatus2Req) st_req =
	    fu_struct_steelseries_connection_status2_req_new();
	g_autoptr(FuStructSteelseriesConnectionStatus2Res) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (!fu_steelseries_fizz_gen2_request(self, st_req, error))
		return FALSE;
	buf_res = fu_steelseries_fizz_gen2_response(self, error);
	if (buf_res == NULL)
		return FALSE;
	st_res = fu_struct_steelseries_connection_status2_res_parse(buf_res->data,
								    buf_res->len,
								    0x0,
								    error);
	if (st_res == NULL)
		return FALSE;
	if (fu_struct_steelseries_connection_status2_res_get_status(st_res))
		*status = FU_STEELSERIES_FIZZ_CONNECTION_STATUS_CONNECTED;
	else
		*status = FU_STEELSERIES_FIZZ_CONNECTION_STATUS_NOT_CONNECTED;

	/* success */
	return TRUE;
}

static gchar *
fu_steelseries_fizz_gen2_get_serial(FuSteelseriesFizzImpl *self, gboolean tunnel, GError **error)
{
	g_autofree gchar *serial = NULL;
	g_autoptr(FuStructSteelseriesSerial2Req) st_req = fu_struct_steelseries_serial2_req_new();
	g_autoptr(FuStructSteelseriesSerial2Res) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error))
		return NULL;
	buf_res = fu_steelseries_device_response(FU_STEELSERIES_DEVICE(self), error);
	if (buf_res == NULL)
		return NULL;
	st_res = fu_struct_steelseries_serial2_res_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return NULL;
	serial = fu_struct_steelseries_serial2_res_get_serial(st_res);
	if (serial == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no serial number provided");
		return NULL;
	}
	return g_steal_pointer(&serial);
}

static gboolean
fu_steelseries_fizz_gen2_is_updatable(FuSteelseriesFizzImpl *self, FuDevice *device, GError **error)
{
	g_autoptr(FwupdRequest) request = NULL;
	g_autofree gchar *msg = NULL;

	/* requires direct USB only */
	if (g_strcmp0(fu_device_get_composite_id(device), fu_device_get_id(device)) == 0)
		return TRUE;

	fu_device_add_request_flag(device, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);

	/* the user has to do something */
	msg = g_strdup_printf("%s needs to be connected via the USB cable, "
			      "to start the update. "
			      "Please plug the USB-C cable.",
			      fu_device_get_name(device));
	request = fwupd_request_new();
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_INSERT_USB_CABLE);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fwupd_request_set_message(request, msg);
	if (!fu_device_emit_request(device, request, NULL, error))
		return FALSE;

	/* FIXME: return commented as soon as we have support of simultaneous connections */
	// fu_device_set_remove_delay(device, FU_DEVICE_REMOVE_DELAY_USER_REPLUG); /* 40 sec */
	// fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	// fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	// return TRUE;
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "supported update via USB-C only");
	return FALSE;
}

static gboolean
fu_steelseries_fizz_gen2_probe(FuDevice *device, GError **error)
{
	/* in bootloader mode */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		fu_steelseries_device_set_iface_idx_offset(FU_STEELSERIES_DEVICE(device), 0x00);

	/* FuUsbDevice->probe */
	return FU_DEVICE_CLASS(fu_steelseries_fizz_gen2_parent_class)->probe(device, error);
}

static void
fu_steelseries_fizz_gen2_impl_iface_init(FuSteelseriesFizzImplInterface *iface)
{
	iface->request = fu_steelseries_fizz_gen2_request;
	iface->response = fu_steelseries_fizz_gen2_response;
	iface->get_version = fu_steelseries_fizz_gen2_get_version;
	iface->get_fs_id = fu_steelseries_fizz_gen2_get_fs_id;
	iface->get_file_id = fu_steelseries_fizz_gen2_get_file_id;
	iface->get_paired_status = fu_steelseries_fizz_gen2_get_paired_status;
	iface->get_connection_status = fu_steelseries_fizz_gen2_get_connection_status;
	iface->get_battery_level = fu_steelseries_fizz_gen2_get_battery_level;
	iface->is_updatable = fu_steelseries_fizz_gen2_is_updatable;
	iface->get_serial = fu_steelseries_fizz_gen2_get_serial;
}

static gboolean
fu_steelseries_fizz_gen2_set_quirk_kv(FuDevice *device,
				      const gchar *key,
				      const gchar *value,
				      GError **error)
{
	FuSteelseriesFizzGen2 *self = FU_STEELSERIES_FIZZ_GEN2(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "SteelSeriesFizzInterface") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;

		fu_steelseries_device_set_iface_idx_offset(FU_STEELSERIES_DEVICE(self), tmp);
		return TRUE;
	}

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
	return FALSE;
}

static void
fu_steelseries_fizz_gen2_class_init(FuSteelseriesFizzGen2Class *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->probe = fu_steelseries_fizz_gen2_probe;
	device_class->set_quirk_kv = fu_steelseries_fizz_gen2_set_quirk_kv;
}

static void
fu_steelseries_fizz_gen2_init(FuSteelseriesFizzGen2 *self)
{
	fu_steelseries_device_set_iface_idx_offset(FU_STEELSERIES_DEVICE(self), 0x05);
}
