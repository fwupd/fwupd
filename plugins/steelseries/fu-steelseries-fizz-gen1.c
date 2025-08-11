/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-steelseries-fizz-gen1.h"
#include "fu-steelseries-fizz-impl.h"

struct _FuSteelseriesFizzGen1 {
	FuSteelseriesDevice parent_instance;
};

static void
fu_steelseries_fizz_gen1_impl_iface_init(FuSteelseriesFizzImplInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuSteelseriesFizzGen1,
			fu_steelseries_fizz_gen1,
			FU_TYPE_STEELSERIES_DEVICE,
			G_IMPLEMENT_INTERFACE(FU_TYPE_STEELSERIES_FIZZ_IMPL,
					      fu_steelseries_fizz_gen1_impl_iface_init))

static gboolean
fu_steelseries_fizz_gen1_request(FuSteelseriesFizzImpl *self, const GByteArray *buf, GError **error)
{
	return fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), buf, error);
}

static GByteArray *
fu_steelseries_fizz_gen1_response(FuSteelseriesFizzImpl *self, GError **error)
{
	return fu_steelseries_device_response(FU_STEELSERIES_DEVICE(self), error);
}

static gchar *
fu_steelseries_fizz_gen1_get_version(FuSteelseriesFizzImpl *self, gboolean tunnel, GError **error)
{
	guint8 cmd = FU_STEELSERIES_FIZZ_CMD_VERSION;
	g_autoptr(FuStructSteelseriesFizzVersionReq) st_req = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (tunnel)
		cmd |= FU_STEELSERIES_FIZZ_CMD_TUNNEL_BIT;

	st_req = fu_struct_steelseries_fizz_version_req_new();
	fu_struct_steelseries_fizz_version_req_set_cmd(st_req, cmd);
	if (!fu_steelseries_fizz_gen1_request(self, st_req, error))
		return NULL;
	buf_res = fu_steelseries_fizz_gen1_response(self, error);
	if (buf_res == NULL)
		return NULL;

	/* success */
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	return fu_memstrsafe(buf_res->data, buf_res->len, 0x0, buf_res->len, error);
}

static guint8
fu_steelseries_fizz_gen1_get_fs_id(FuSteelseriesFizzImpl *self, gboolean is_receiver)
{
	if (is_receiver)
		return FU_STEELSERIES_FIZZ_FILESYSTEM_RECEIVER;
	return FU_STEELSERIES_FIZZ_FILESYSTEM_MOUSE;
}

static guint8
fu_steelseries_fizz_gen1_get_file_id(FuSteelseriesFizzImpl *self, gboolean is_receiver)
{
	if (is_receiver)
		return FU_STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_ID_BACKUP_APP;
	return FU_STEELSERIES_FIZZ_MOUSE_FILESYSTEM_ID_BACKUP_APP;
}

static gboolean
fu_steelseries_fizz_gen1_get_paired_status(FuSteelseriesFizzImpl *self,
					   guint8 *status,
					   GError **error)
{
	g_autoptr(FuStructSteelseriesPairedStatusReq) st_req =
	    fu_struct_steelseries_paired_status_req_new();
	g_autoptr(FuStructSteelseriesPairedStatusRes) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (!fu_steelseries_fizz_gen1_request(self, st_req, error))
		return FALSE;
	buf_res = fu_steelseries_fizz_gen1_response(self, error);
	if (buf_res == NULL)
		return FALSE;
	st_res =
	    fu_struct_steelseries_paired_status_res_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	*status = fu_struct_steelseries_paired_status_res_get_status(st_res);
	return TRUE;
}

static gboolean
fu_steelseries_fizz_gen1_get_connection_status(FuSteelseriesFizzImpl *self,
					       FuSteelseriesFizzConnectionStatus *status,
					       GError **error)
{
	g_autoptr(FuStructSteelseriesConnectionStatusReq) st_req =
	    fu_struct_steelseries_connection_status_req_new();
	g_autoptr(FuStructSteelseriesConnectionStatusRes) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (!fu_steelseries_fizz_gen1_request(self, st_req, error))
		return FALSE;
	buf_res = fu_steelseries_fizz_gen1_response(self, error);
	if (buf_res == NULL)
		return FALSE;
	st_res = fu_struct_steelseries_connection_status_res_parse(buf_res->data,
								   buf_res->len,
								   0x0,
								   error);
	if (st_res == NULL)
		return FALSE;
	*status = fu_struct_steelseries_connection_status_res_get_status(st_res);
	return TRUE;
}

static gboolean
fu_steelseries_fizz_gen1_get_battery_level(FuSteelseriesFizzImpl *self,
					   gboolean tunnel,
					   guint8 *level,
					   GError **error)
{
	guint8 cmd = FU_STEELSERIES_FIZZ_CMD_BATTERY_LEVEL;
	g_autoptr(FuStructSteelseriesBatteryLevelReq) st_req = NULL;
	g_autoptr(FuStructSteelseriesBatteryLevelRes) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (tunnel)
		cmd |= FU_STEELSERIES_FIZZ_CMD_TUNNEL_BIT;

	st_req = fu_struct_steelseries_battery_level_req_new();
	fu_struct_steelseries_battery_level_req_set_cmd(st_req, cmd);
	if (!fu_steelseries_fizz_gen1_request(self, st_req, error))
		return FALSE;
	buf_res = fu_steelseries_fizz_gen1_response(self, error);
	if (buf_res == NULL)
		return FALSE;
	st_res =
	    fu_struct_steelseries_battery_level_res_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;

	/*
	 * CHARGING: Most significant bit. When bit is set to 1 it means battery is currently
	 * charging/plugged in
	 *
	 * LEVEL: 7 least significant bit value of the battery. Values are between 2-21, to get %
	 * you can do (LEVEL - 1) * 5
	 */
	*level = ((fu_struct_steelseries_battery_level_res_get_level(st_res) &
		   FU_STEELSERIES_FIZZ_BATTERY_LEVEL_STATUS_BITS) -
		  1U) *
		 5U;

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_gen1_setup(FuDevice *device, GError **error)
{
	/* in bootloader mode */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* FuUsbDevice->setup */
	return FU_DEVICE_CLASS(fu_steelseries_fizz_gen1_parent_class)->setup(device, error);
}

static void
fu_steelseries_fizz_gen1_impl_iface_init(FuSteelseriesFizzImplInterface *iface)
{
	iface->request = fu_steelseries_fizz_gen1_request;
	iface->response = fu_steelseries_fizz_gen1_response;
	iface->get_version = fu_steelseries_fizz_gen1_get_version;
	iface->get_fs_id = fu_steelseries_fizz_gen1_get_fs_id;
	iface->get_file_id = fu_steelseries_fizz_gen1_get_file_id;
	iface->get_paired_status = fu_steelseries_fizz_gen1_get_paired_status;
	iface->get_connection_status = fu_steelseries_fizz_gen1_get_connection_status;
	iface->get_battery_level = fu_steelseries_fizz_gen1_get_battery_level;
}

static void
fu_steelseries_fizz_gen1_class_init(FuSteelseriesFizzGen1Class *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->setup = fu_steelseries_fizz_gen1_setup;
}

static void
fu_steelseries_fizz_gen1_init(FuSteelseriesFizzGen1 *self)
{
	fu_steelseries_device_set_iface_number(FU_STEELSERIES_DEVICE(self), 0x03);
}
