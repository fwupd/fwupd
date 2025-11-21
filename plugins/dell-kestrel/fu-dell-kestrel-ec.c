/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-kestrel-common.h"

/* Private structure */
struct _FuDellKestrelEc {
	FuDellKestrelHidDevice parent_instance;
	FuStructDellKestrelDockData *dock_data;
	FuStructDellKestrelDockInfo *dock_info;
	FuDellDockBaseType base_type;
	FuDellKestrelDockSku base_sku;
};

G_DEFINE_TYPE(FuDellKestrelEc, fu_dell_kestrel_ec, FU_TYPE_DELL_KESTREL_HID_DEVICE)

static FuStructDellKestrelDockInfoEcQueryEntry *
fu_dell_kestrel_ec_dev_entry(FuDellKestrelEc *self,
			     FuDellKestrelEcDevType dev_type,
			     FuDellKestrelEcDevSubtype subtype,
			     FuDellKestrelEcDevInstance instance)
{
	g_autoptr(FuStructDellKestrelDockInfoHeader) hdr = NULL;
	guint num = 0;

	hdr = fu_struct_dell_kestrel_dock_info_get_header(self->dock_info);
	num = fu_struct_dell_kestrel_dock_info_header_get_total_devices(hdr);
	if (num < 1) {
		g_debug("no device found in dock info hdr");
		return NULL;
	}

	for (guint i = 0; i < num; i++) {
		g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) comp_dev = NULL;
		g_autoptr(FuStructDellKestrelDockInfoEcAddrMap) comp_info = NULL;

		comp_dev = fu_struct_dell_kestrel_dock_info_get_devices(self->dock_info, i);
		comp_info =
		    fu_struct_dell_kestrel_dock_info_ec_query_entry_get_ec_addr_map(comp_dev);

		if (dev_type !=
		    fu_struct_dell_kestrel_dock_info_ec_addr_map_get_device_type(comp_info))
			continue;

		if (subtype != 0 &&
		    subtype != fu_struct_dell_kestrel_dock_info_ec_addr_map_get_subtype(comp_info))
			continue;

		/* vary by instance index */
		if (dev_type == FU_DELL_KESTREL_EC_DEV_TYPE_PD &&
		    instance !=
			fu_struct_dell_kestrel_dock_info_ec_addr_map_get_instance(comp_info))
			continue;

		return g_steal_pointer(&comp_dev);
	}
	return NULL;
}

gboolean
fu_dell_kestrel_ec_is_dev_present(FuDellKestrelEc *self,
				  FuDellKestrelEcDevType dev_type,
				  FuDellKestrelEcDevSubtype subtype,
				  FuDellKestrelEcDevInstance instance)
{
	g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) dev_entry = NULL;
	dev_entry = fu_dell_kestrel_ec_dev_entry(self, dev_type, subtype, instance);
	return dev_entry != NULL;
}

gboolean
fu_dell_kestrel_ec_is_chunk_supported(FuDellKestrelEc *self, FuDellKestrelEcDevType dev_type)
{
	if (dev_type == FU_DELL_KESTREL_EC_DEV_TYPE_PD) {
		guint8 chunk_support = 0;

		chunk_support = fu_struct_dell_kestrel_dock_data_get_chunk_support(self->dock_data);
		return (chunk_support & FU_DELL_KESTREL_DOCK_DATA_CHUNK_SUPPORT_BITMAP_PD);
	}
	return TRUE;
}

const gchar *
fu_dell_kestrel_ec_devicetype_to_str(FuDellKestrelEcDevType dev_type,
				     FuDellKestrelEcDevSubtype subtype,
				     FuDellKestrelEcDevInstance instance)
{
	switch (dev_type) {
	case FU_DELL_KESTREL_EC_DEV_TYPE_MAIN_EC:
		return "EC";
	case FU_DELL_KESTREL_EC_DEV_TYPE_PD:
		if (subtype == FU_DELL_KESTREL_EC_DEV_SUBTYPE_TI) {
			if (instance == FU_DELL_KESTREL_EC_DEV_INSTANCE_TI_UP5)
				return "PD";
			if (instance == FU_DELL_KESTREL_EC_DEV_INSTANCE_TI_UP15)
				return "PD UP15";
			if (instance == FU_DELL_KESTREL_EC_DEV_INSTANCE_TI_UP17)
				return "PD UP17";
		}
		return NULL;
	case FU_DELL_KESTREL_EC_DEV_TYPE_USBHUB:
		if (subtype == FU_DELL_KESTREL_EC_DEV_SUBTYPE_RTS0)
			return "USB Hub RTS0";
		if (subtype == FU_DELL_KESTREL_EC_DEV_SUBTYPE_RTS5)
			return "USB Hub RTS5";
		return NULL;
	case FU_DELL_KESTREL_EC_DEV_TYPE_MST:
		if (subtype == FU_DELL_KESTREL_EC_DEV_SUBTYPE_VMM8)
			return "MST VMM8";
		if (subtype == FU_DELL_KESTREL_EC_DEV_SUBTYPE_VMM9)
			return "MST VMM9";
		return NULL;
	case FU_DELL_KESTREL_EC_DEV_TYPE_TBT:
		if (subtype == FU_DELL_KESTREL_EC_DEV_SUBTYPE_TR)
			return "TR";
		if (subtype == FU_DELL_KESTREL_EC_DEV_SUBTYPE_GR)
			return "GR";
		if (subtype == FU_DELL_KESTREL_EC_DEV_SUBTYPE_BR)
			return "BR";
		return NULL;
	case FU_DELL_KESTREL_EC_DEV_TYPE_QI:
		return "QI";
	case FU_DELL_KESTREL_EC_DEV_TYPE_DP_MUX:
		return "Retimer";
	case FU_DELL_KESTREL_EC_DEV_TYPE_LAN:
		return "LAN";
	case FU_DELL_KESTREL_EC_DEV_TYPE_FAN:
		return "Fan";
	case FU_DELL_KESTREL_EC_DEV_TYPE_RMM:
		return "RMM";
	case FU_DELL_KESTREL_EC_DEV_TYPE_WTPD:
		return "WT PD";
	default:
		return NULL;
	}
}

FuDellDockBaseType
fu_dell_kestrel_ec_get_dock_type(FuDellKestrelEc *self)
{
	return self->base_type;
}

FuDellKestrelDockSku
fu_dell_kestrel_ec_get_dock_sku(FuDellKestrelEc *self)
{
	return self->base_sku;
}

static gboolean
fu_dell_kestrel_ec_read(FuDellKestrelEc *self,
			FuDellKestrelEcCmd cmd,
			GByteArray *res,
			GError **error)
{
	if (!fu_dell_kestrel_hid_device_i2c_read(FU_DELL_KESTREL_HID_DEVICE(self),
						 cmd,
						 res,
						 100,
						 error)) {
		g_prefix_error(error, "read over HID-I2C failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_write(FuDellKestrelEc *self, GByteArray *buf, GError **error)
{
	g_return_val_if_fail(buf->len > 1, FALSE);

	if (!fu_dell_kestrel_hid_device_i2c_write(FU_DELL_KESTREL_HID_DEVICE(self), buf, error)) {
		g_prefix_error(error, "write over HID-I2C failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_create_node(FuDellKestrelEc *self, FuDevice *new_device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(new_device, error);
	if (locker == NULL)
		return FALSE;

	/* setup relationship */
	fu_device_add_child(FU_DEVICE(self), FU_DEVICE(new_device));
	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_probe_package(FuDellKestrelEc *self, GError **error)
{
	g_autoptr(FuDellKestrelPackage) pkg_dev = NULL;

	pkg_dev = fu_dell_kestrel_package_new(FU_DEVICE(self));
	return fu_dell_kestrel_ec_create_node(self, FU_DEVICE(pkg_dev), error);
}

static gboolean
fu_dell_kestrel_ec_probe_pd(FuDellKestrelEc *self,
			    FuDellKestrelEcDevType dev_type,
			    FuDellKestrelEcDevSubtype subtype,
			    FuDellKestrelEcDevInstance instance,
			    GError **error)
{
	g_autoptr(FuDellKestrelPd) pd_dev = NULL;
	g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(self, dev_type, subtype, instance);
	if (dev_entry == NULL)
		return TRUE;

	pd_dev = fu_dell_kestrel_pd_new(FU_DEVICE(self), subtype, instance);
	return fu_dell_kestrel_ec_create_node(self, FU_DEVICE(pd_dev), error);
}

static gboolean
fu_dell_kestrel_ec_probe_subcomponents(FuDellKestrelEc *self, GError **error)
{
	g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) dev_entry_ilan = NULL;
	g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) dev_entry_dpmux = NULL;
	g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) dev_entry_wt = NULL;

	/* Package */
	if (!fu_dell_kestrel_ec_probe_package(self, error))
		return FALSE;

	/* PD UP5 */
	if (!fu_dell_kestrel_ec_probe_pd(self,
					 FU_DELL_KESTREL_EC_DEV_TYPE_PD,
					 FU_DELL_KESTREL_EC_DEV_SUBTYPE_TI,
					 FU_DELL_KESTREL_EC_DEV_INSTANCE_TI_UP5,
					 error))
		return FALSE;

	/* PD UP15 */
	if (!fu_dell_kestrel_ec_probe_pd(self,
					 FU_DELL_KESTREL_EC_DEV_TYPE_PD,
					 FU_DELL_KESTREL_EC_DEV_SUBTYPE_TI,
					 FU_DELL_KESTREL_EC_DEV_INSTANCE_TI_UP15,
					 error))
		return FALSE;

	/* PD UP17 */
	if (!fu_dell_kestrel_ec_probe_pd(self,
					 FU_DELL_KESTREL_EC_DEV_TYPE_PD,
					 FU_DELL_KESTREL_EC_DEV_SUBTYPE_TI,
					 FU_DELL_KESTREL_EC_DEV_INSTANCE_TI_UP17,
					 error))
		return FALSE;

	/* DP MUX | Retimer */
	dev_entry_dpmux =
	    fu_dell_kestrel_ec_dev_entry(self, FU_DELL_KESTREL_EC_DEV_TYPE_DP_MUX, 0, 0);
	if (dev_entry_dpmux != NULL) {
		g_autoptr(FuDellKestrelDpmux) dpmux_device = NULL;

		dpmux_device = fu_dell_kestrel_dpmux_new(FU_DEVICE(self));
		if (!fu_dell_kestrel_ec_create_node(self, FU_DEVICE(dpmux_device), error))
			return FALSE;
	}

	/* WT PD */
	dev_entry_wt = fu_dell_kestrel_ec_dev_entry(self, FU_DELL_KESTREL_EC_DEV_TYPE_WTPD, 0, 0);
	if (dev_entry_wt != NULL) {
		g_autoptr(FuDellKestrelWtpd) wt_dev = NULL;

		wt_dev = fu_dell_kestrel_wtpd_new(FU_DEVICE(self));
		if (!fu_dell_kestrel_ec_create_node(self, FU_DEVICE(wt_dev), error))
			return FALSE;
	}

	/* LAN */
	dev_entry_ilan = fu_dell_kestrel_ec_dev_entry(self, FU_DELL_KESTREL_EC_DEV_TYPE_LAN, 0, 0);
	if (dev_entry_ilan != NULL) {
		g_autoptr(FuDellKestrelIlan) ilan_device = NULL;

		ilan_device = fu_dell_kestrel_ilan_new(FU_DEVICE(self));
		if (!fu_dell_kestrel_ec_create_node(self, FU_DEVICE(ilan_device), error))
			return FALSE;

		/* max firmware size */
		if (fu_struct_dell_kestrel_dock_data_get_board_id(self->dock_data) < 0x4)
			fu_device_set_firmware_size(FU_DEVICE(ilan_device), 2 * 1024 * 1024);
		else
			fu_device_set_firmware_size(FU_DEVICE(ilan_device), 1 * 1024 * 1024);
	}

	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_dock_type_extract(FuDellKestrelEc *self, GError **error)
{
	FuDellDockBaseType dock_type = fu_dell_kestrel_ec_get_dock_type(self);
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_MAIN_EC;

	/* don't change error type, the plugin ignores it */
	if (dock_type != FU_DELL_DOCK_BASE_TYPE_KESTREL) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "No valid dock was found");
		return FALSE;
	}

	/* this will trigger setting up all the quirks */
	fu_device_add_instance_u8(FU_DEVICE(self), "DOCKTYPE", dock_type);
	fu_device_add_instance_u8(FU_DEVICE(self), "DEVTYPE", dev_type);
	fu_device_build_instance_id(FU_DEVICE(self),
				    error,
				    "USB",
				    "VID",
				    "PID",
				    "DOCKTYPE",
				    "DEVTYPE",
				    NULL);
	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_dock_type_cmd(FuDellKestrelEc *self, GError **error)
{
	FuDellKestrelEcCmd cmd = FU_DELL_KESTREL_EC_CMD_GET_DOCK_TYPE;
	gsize length = 1;
	g_autoptr(GByteArray) res = g_byte_array_new_take(g_malloc0(length), length);

	/* expect response 1 byte */
	if (!fu_dell_kestrel_ec_read(self, cmd, res, error)) {
		g_prefix_error(error, "Failed to query dock type: ");
		return FALSE;
	}

	self->base_type = res->data[0];

	/* check dock type to proceed with this plugin or exit as unsupported */
	return fu_dell_kestrel_ec_dock_type_extract(self, error);
}

static gboolean
fu_dell_kestrel_ec_dock_info_cmd(FuDellKestrelEc *self, GError **error)
{
	FuDellKestrelEcCmd cmd = FU_DELL_KESTREL_EC_CMD_GET_DOCK_INFO;
	g_autoptr(GByteArray) res = fu_struct_dell_kestrel_dock_info_new();

	/* get dock info over HID */
	if (!fu_dell_kestrel_ec_read(self, cmd, res, error)) {
		g_prefix_error(error, "Failed to query dock info: ");
		return FALSE;
	}
	self->dock_info = fu_struct_dell_kestrel_dock_info_parse(res->data, res->len, 0, error);
	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_dock_data_extract(FuDellKestrelEc *self, GError **error)
{
	g_autofree gchar *mkt_name = NULL;
	g_autofree gchar *serial = NULL;
	g_autofree gchar *service_tag = NULL;

	/* set FuDevice name */
	mkt_name = fu_struct_dell_kestrel_dock_data_get_marketing_name(self->dock_data);
	fu_device_set_name(FU_DEVICE(self), mkt_name);

	/* set FuDevice serial */
	service_tag = fu_struct_dell_kestrel_dock_data_get_service_tag(self->dock_data);
	serial =
	    g_strdup_printf("%.7s/%016" G_GUINT64_FORMAT,
			    service_tag,
			    fu_struct_dell_kestrel_dock_data_get_module_serial(self->dock_data));
	fu_device_set_serial(FU_DEVICE(self), serial);

	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_dock_data_cmd(FuDellKestrelEc *self, GError **error)
{
	FuDellKestrelEcCmd cmd = FU_DELL_KESTREL_EC_CMD_GET_DOCK_DATA;
	g_autoptr(GByteArray) res = fu_struct_dell_kestrel_dock_data_new();

	/* get dock data over HID */
	if (!fu_dell_kestrel_ec_read(self, cmd, res, error)) {
		g_prefix_error(error, "Failed to query dock data: ");
		return FALSE;
	}

	if (self->dock_data != NULL)
		fu_struct_dell_kestrel_dock_data_unref(self->dock_data);
	self->dock_data = fu_struct_dell_kestrel_dock_data_parse(res->data, res->len, 0, error);
	if (self->dock_data == NULL)
		return FALSE;
	if (!fu_dell_kestrel_ec_dock_data_extract(self, error))
		return FALSE;
	return TRUE;
}

gboolean
fu_dell_kestrel_ec_is_dock_ready4update(FuDevice *device, GError **error)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);
	guint16 bitmask_fw_update_pending = 1 << 8;
	guint32 dock_status = 0;

	if (!fu_dell_kestrel_ec_dock_data_cmd(self, error))
		return FALSE;

	dock_status = fu_struct_dell_kestrel_dock_data_get_dock_status(self->dock_data);
	if ((dock_status & bitmask_fw_update_pending) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "dock status (%x) has pending updates, unavailable for now.",
			    dock_status);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_is_new_ownership_cmd(FuDellKestrelEc *self)
{
	FuDevice *device = FU_DEVICE(self);
	const gchar *version = fu_device_get_version(device);
	FwupdVersionFormat fmt = fu_device_get_version_format(device);

	if (fu_version_compare(version, "01.00.00.00", fmt) >= 0) {
		if (fu_version_compare(version, "01.00.05.02", fmt) >= 0)
			return TRUE;

		return FALSE;
	}
	return fu_version_compare(version, "00.00.34.00", fmt) >= 0;
}

gboolean
fu_dell_kestrel_ec_own_dock(FuDellKestrelEc *self, gboolean lock, GError **error)
{
	guint16 bitmask = 0x0;
	g_autoptr(GByteArray) st_req = fu_struct_dell_kestrel_ec_databytes_new();
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *msg = NULL;

	fu_struct_dell_kestrel_ec_databytes_set_cmd(st_req, FU_DELL_KESTREL_EC_CMD_SET_MODIFY_LOCK);
	fu_struct_dell_kestrel_ec_databytes_set_data_sz(st_req, 2);

	if (lock) {
		msg = g_strdup("own the dock");
		bitmask = fu_dell_kestrel_ec_is_new_ownership_cmd(self) ? 0x10CC : 0xFFFF;
	} else {
		msg = g_strdup("release the dock");
		bitmask = fu_dell_kestrel_ec_is_new_ownership_cmd(self) ? 0xC001 : 0x0000;
	}
	if (!fu_struct_dell_kestrel_ec_databytes_set_data(st_req,
							  (const guint8 *)&bitmask,
							  sizeof(bitmask),
							  error))
		return FALSE;

	fu_device_sleep(FU_DEVICE(self), 1000);
	if (!fu_dell_kestrel_ec_write(self, st_req, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND))
			g_debug("ignoring: %s", error_local->message);
		else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			g_prefix_error(error, "failed to %s", msg);
			return FALSE;
		}
	}
	g_debug("%s successfully", msg);

	return TRUE;
}

gboolean
fu_dell_kestrel_ec_run_passive_update(FuDellKestrelEc *self, GError **error)
{
	guint max_tries = 2;
	g_autoptr(GByteArray) st_req = fu_struct_dell_kestrel_ec_databytes_new();
	const guint8 bitmap = 0x07;

	/* ec included in cmd, set bit2 in data for tbt */
	fu_struct_dell_kestrel_ec_databytes_set_cmd(st_req, FU_DELL_KESTREL_EC_CMD_SET_PASSIVE);
	fu_struct_dell_kestrel_ec_databytes_set_data_sz(st_req, 1);
	if (!fu_struct_dell_kestrel_ec_databytes_set_data(st_req,
							  (const guint8 *)&bitmap,
							  sizeof(bitmap),
							  error))
		return FALSE;

	for (guint i = 1; i <= max_tries; i++) {
		g_debug("register passive update (uod) flow (%u/%u)", i, max_tries);
		if (!fu_dell_kestrel_ec_write(self, st_req, error)) {
			g_prefix_error(error, "failed to register uod flow: ");
			return FALSE;
		}
		fu_device_sleep(FU_DEVICE(self), 100);
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_set_dock_sku(FuDellKestrelEc *self, GError **error)
{
	if (self->base_type == FU_DELL_DOCK_BASE_TYPE_KESTREL) {
		g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) dev_entry = NULL;

		/* TBT type yet available, do workaround */
		dev_entry = fu_dell_kestrel_ec_dev_entry(self,
							 FU_DELL_KESTREL_EC_DEV_TYPE_TBT,
							 FU_DELL_KESTREL_EC_DEV_SUBTYPE_BR,
							 0);
		if (dev_entry != NULL) {
			self->base_sku = FU_DELL_KESTREL_DOCK_SKU_T5;
			return TRUE;
		}
		dev_entry = fu_dell_kestrel_ec_dev_entry(self,
							 FU_DELL_KESTREL_EC_DEV_TYPE_TBT,
							 FU_DELL_KESTREL_EC_DEV_SUBTYPE_GR,
							 0);
		if (dev_entry != NULL) {
			self->base_sku = FU_DELL_KESTREL_DOCK_SKU_T4;
			return TRUE;
		}
		self->base_sku = FU_DELL_KESTREL_DOCK_SKU_DPALT;
		return TRUE;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "unsupported dock type: %x",
		    self->base_type);
	return FALSE;
}

guint32
fu_dell_kestrel_ec_get_pd_version(FuDellKestrelEc *self,
				  FuDellKestrelEcDevSubtype subtype,
				  FuDellKestrelEcDevInstance instance)
{
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_PD;
	g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(self, dev_type, subtype, instance);
	return (dev_entry == NULL)
		   ? 0
		   : fu_struct_dell_kestrel_dock_info_ec_query_entry_get_version_32(dev_entry);
}

guint32
fu_dell_kestrel_ec_get_ilan_version(FuDellKestrelEc *self)
{
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_LAN;
	g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(self, dev_type, 0, 0);
	return (dev_entry == NULL)
		   ? 0
		   : fu_struct_dell_kestrel_dock_info_ec_query_entry_get_version_32(dev_entry);
}

guint32
fu_dell_kestrel_ec_get_wtpd_version(FuDellKestrelEc *self)
{
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_WTPD;
	g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(self, dev_type, 0, 0);
	return (dev_entry == NULL)
		   ? 0
		   : fu_struct_dell_kestrel_dock_info_ec_query_entry_get_version_32(dev_entry);
}

guint32
fu_dell_kestrel_ec_get_dpmux_version(FuDellKestrelEc *self)
{
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_DP_MUX;
	g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(self, dev_type, 0, 0);
	return (dev_entry == NULL)
		   ? 0
		   : fu_struct_dell_kestrel_dock_info_ec_query_entry_get_version_32(dev_entry);
}

guint32
fu_dell_kestrel_ec_get_rmm_version(FuDellKestrelEc *self)
{
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_RMM;
	g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(self, dev_type, 0, 0);
	return (dev_entry == NULL)
		   ? 0
		   : fu_struct_dell_kestrel_dock_info_ec_query_entry_get_version_32(dev_entry);
}

static guint32
fu_dell_kestrel_ec_get_ec_version(FuDellKestrelEc *self)
{
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_MAIN_EC;
	g_autoptr(FuStructDellKestrelDockInfoEcQueryEntry) dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(self, dev_type, 0, 0);
	return (dev_entry == NULL)
		   ? 0
		   : fu_struct_dell_kestrel_dock_info_ec_query_entry_get_version_32(dev_entry);
}

guint32
fu_dell_kestrel_ec_get_package_version(FuDellKestrelEc *self)
{
	return fu_struct_dell_kestrel_dock_data_get_dock_firmware_pkg_ver(self->dock_data);
}

gboolean
fu_dell_kestrel_ec_commit_package(FuDellKestrelEc *self, GInputStream *stream, GError **error)
{
	g_autoptr(GByteArray) st_req = fu_struct_dell_kestrel_ec_databytes_new();
	g_autoptr(GByteArray) buf = NULL;
	gsize streamsz = 0;

	/* verify package length */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	if (streamsz != FU_STRUCT_DELL_KESTREL_PACKAGE_FW_VERSIONS_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "Invalid package size %" G_GSIZE_FORMAT,
			    streamsz);
		return FALSE;
	}

	/* get the data bytes */
	buf = fu_input_stream_read_byte_array(stream,
					      0,
					      FU_STRUCT_DELL_KESTREL_PACKAGE_FW_VERSIONS_SIZE,
					      NULL,
					      error);

	fu_struct_dell_kestrel_ec_databytes_set_cmd(st_req, FU_DELL_KESTREL_EC_CMD_SET_DOCK_PKG);
	fu_struct_dell_kestrel_ec_databytes_set_data_sz(st_req, streamsz);
	if (!fu_struct_dell_kestrel_ec_databytes_set_data(st_req, buf->data, buf->len, error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "->PACKAGE", st_req->data, st_req->len);

	if (!fu_dell_kestrel_ec_write(self, st_req, error)) {
		g_prefix_error(error, "Failed to commit package: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);
	return fu_dell_kestrel_hid_device_write_firmware(FU_DELL_KESTREL_HID_DEVICE(self),
							 firmware,
							 progress,
							 FU_DELL_KESTREL_EC_DEV_TYPE_MAIN_EC,
							 0,
							 error);
}

static gboolean
fu_dell_kestrel_ec_query_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);

	/* dock data */
	if (!fu_dell_kestrel_ec_dock_data_cmd(self, error))
		return FALSE;

	/* dock info */
	if (!fu_dell_kestrel_ec_dock_info_cmd(self, error))
		return FALSE;

	/* set internal dock sku, must after dock info */
	if (!fu_dell_kestrel_ec_set_dock_sku(self, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_reload(FuDevice *device, GError **error)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);

	/* if query looks bad, wait a few seconds and retry */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_dell_kestrel_ec_query_cb,
				  DELL_KESTREL_MAX_RETRIES,
				  500,
				  NULL,
				  error)) {
		g_prefix_error(error, "failed to query dock ec: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_setup(FuDevice *device, GError **error)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);
	guint32 ec_version = 0;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_dell_kestrel_ec_parent_class)->setup(device, error))
		return FALSE;

	/* get dock type */
	if (!fu_dell_kestrel_ec_dock_type_cmd(self, error))
		return FALSE;

	/* if query looks bad, wait a few seconds and retry */
	if (!fu_device_retry_full(device,
				  fu_dell_kestrel_ec_query_cb,
				  DELL_KESTREL_MAX_RETRIES,
				  500,
				  NULL,
				  error)) {
		g_prefix_error(error, "failed to query dock ec: ");
		return FALSE;
	}

	/* setup version */
	ec_version = fu_dell_kestrel_ec_get_ec_version(self);
	fu_device_set_version_raw(device, ec_version);

	/* create the subcomponents */
	if (!fu_dell_kestrel_ec_probe_subcomponents(self, error))
		return FALSE;

	g_debug("dell-kestrel-ec->setup done successfully");
	return TRUE;
}

static gchar *
fu_dell_kestrel_ec_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32_hex(version_raw, fu_device_get_version_format(device));
}

static gboolean
fu_dell_kestrel_ec_open(FuDevice *device, GError **error)
{
	/* FuUdevDevice->open */
	return FU_DEVICE_CLASS(fu_dell_kestrel_ec_parent_class)->open(device, error);
}

static void
fu_dell_kestrel_ec_finalize(GObject *object)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(object);
	if (self->dock_data != NULL)
		fu_struct_dell_kestrel_dock_data_unref(self->dock_data);
	if (self->dock_info != NULL)
		fu_struct_dell_kestrel_dock_info_unref(self->dock_info);
	G_OBJECT_CLASS(fu_dell_kestrel_ec_parent_class)->finalize(object);
}

static void
fu_dell_kestrel_ec_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_dell_kestrel_ec_init(FuDellKestrelEc *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.kestrel");
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x413C");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_DOCK_USB);
	fu_device_set_summary(FU_DEVICE(self), "Dell Dock EC");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_SKIPS_RESTART);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
}

static void
fu_dell_kestrel_ec_class_init(FuDellKestrelEcClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_dell_kestrel_ec_finalize;
	device_class->open = fu_dell_kestrel_ec_open;
	device_class->setup = fu_dell_kestrel_ec_setup;
	device_class->write_firmware = fu_dell_kestrel_ec_write_firmware;
	device_class->reload = fu_dell_kestrel_ec_reload;
	device_class->set_progress = fu_dell_kestrel_ec_set_progress;
	device_class->convert_version = fu_dell_kestrel_ec_convert_version;
}

FuDellKestrelEc *
fu_dell_kestrel_ec_new(FuDevice *device, gboolean uod)
{
	FuDellKestrelEc *self = NULL;
	FuContext *ctx = fu_device_get_context(device);

	self = g_object_new(FU_TYPE_DELL_KESTREL_EC, "context", ctx, NULL);
	fu_device_incorporate(FU_DEVICE(self), device, FU_DEVICE_INCORPORATE_FLAG_ALL);
	fu_device_set_logical_id(FU_DEVICE(self), "ec");
	if (uod)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	return self;
}
