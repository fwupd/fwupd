/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-dell-kestrel-common.h"

typedef struct __attribute__((packed)) { /* nocheck:blocked */
	guint8 dock_configuration;
	guint8 dock_type;
	guint16 power_supply_wattage;
	guint16 module_type;
	guint16 board_id;
	guint16 port0_dock_status;
	guint16 port1_dock_status; // unused for K2 dock, should be 0
	guint32 dock_firmware_pkg_ver;
	guint64 module_serial;
	guint64 original_module_serial;
	guint8 service_tag[7];
	guint8 marketing_name[32];
	guint32 dock_error;
	guint32 dock_module_status;
	guint32 dock_module_error;
	guint8 reserved;
	guint32 dock_status;
	guint16 dock_state;
	guint16 dock_config;
	guint8 dock_mac_addr[6];
	guint32 dock_capabilities;
	guint32 dock_policy;
	guint32 dock_temperature;
	guint32 dock_fan_speed;
	guint16 upf_power;
	guint8 eppid;
	guint8 unused[74];
} FuDellKestrelDockDataStructure;

typedef struct __attribute__((packed)) { /* nocheck:blocked */
	struct FuDellKestrelDockInfoHeader {
		guint8 total_devices;
		guint8 first_index;
		guint8 last_index;
	} header;
	struct FuDellKestrelEcQueryEntry {
		struct FuDellKestrelEcAddrMap {
			guint8 location;
			guint8 device_type;
			guint8 sub_type;
			guint8 arg;
			guint8 instance;
		} ec_addr_map;
		union {
			guint32 version_32;
			guint8 version_8[4];
		} __attribute__((packed)) version; /* nocheck:blocked */
	} devices[20];
} FuDellKestrelDockInfoStructure;

/* Private structure */
struct _FuDellKestrelEc {
	FuDevice parent_instance;
	FuDellKestrelDockDataStructure *dock_data;
	FuDellKestrelDockInfoStructure *dock_info;
	FuDellDockBaseType base_type;
	FuDellKestrelDockSku base_sku;
};

G_DEFINE_TYPE(FuDellKestrelEc, fu_dell_kestrel_ec, FU_TYPE_HID_DEVICE)

static struct FuDellKestrelEcQueryEntry *
fu_dell_kestrel_ec_dev_entry(FuDevice *device,
			     FuDellKestrelEcDevType dev_type,
			     FuDellKestrelEcDevSubtype subtype,
			     FuDellKestrelEcDevInstance instance)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);

	for (guint i = 0; i < self->dock_info->header.total_devices; i++) {
		if (self->dock_info->devices[i].ec_addr_map.device_type != dev_type)
			continue;
		if (subtype != 0 && self->dock_info->devices[i].ec_addr_map.sub_type != subtype)
			continue;

		/* vary by instance index */
		if (dev_type == FU_DELL_KESTREL_EC_DEV_TYPE_PD &&
		    self->dock_info->devices[i].ec_addr_map.instance != instance)
			continue;

		return &self->dock_info->devices[i];
	}
	return NULL;
}

gboolean
fu_dell_kestrel_ec_is_dev_present(FuDevice *device,
				  FuDellKestrelEcDevType dev_type,
				  FuDellKestrelEcDevSubtype subtype,
				  FuDellKestrelEcDevInstance instance)
{
	return fu_dell_kestrel_ec_dev_entry(device, dev_type, subtype, instance) != NULL;
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
fu_dell_kestrel_ec_get_dock_type(FuDevice *device)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);
	return self->base_type;
}

FuDellKestrelDockSku
fu_dell_kestrel_ec_get_dock_sku(FuDevice *device)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);
	return self->base_sku;
}

static gboolean
fu_dell_kestrel_ec_read(FuDevice *device,
			FuDellKestrelEcHidCmd cmd,
			GByteArray *res,
			GError **error)
{
	if (!fu_dell_kestrel_ec_hid_i2c_read(device, cmd, res, 800, error)) {
		g_prefix_error(error, "read over HID-I2C failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_write(FuDevice *device, GByteArray *buf, GError **error)
{
	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(buf->len > 1, FALSE);

	if (!fu_dell_kestrel_ec_hid_i2c_write(device, buf, error)) {
		g_prefix_error(error, "write over HID-I2C failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_create_node(FuDevice *ec_device, FuDevice *new_device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(new_device, error);
	if (locker == NULL)
		return FALSE;

	/* setup relationship */
	fu_device_add_child(ec_device, FU_DEVICE(new_device));
	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_probe_package(FuDevice *ec_dev, GError **error)
{
	g_autoptr(FuDellKestrelPackage) pkg_dev = NULL;

	pkg_dev = fu_dell_kestrel_package_new(ec_dev);
	return fu_dell_kestrel_ec_create_node(ec_dev, FU_DEVICE(pkg_dev), error);
}

static gboolean
fu_dell_kestrel_ec_probe_pd(FuDevice *ec_dev,
			    FuDellKestrelEcDevType dev_type,
			    FuDellKestrelEcDevSubtype subtype,
			    FuDellKestrelEcDevInstance instance,
			    GError **error)
{
	g_autoptr(FuDellKestrelPd) pd_dev = NULL;

	if (fu_dell_kestrel_ec_dev_entry(ec_dev, dev_type, subtype, instance) == NULL)
		return TRUE;

	pd_dev = fu_dell_kestrel_pd_new(ec_dev, subtype, instance);
	return fu_dell_kestrel_ec_create_node(ec_dev, FU_DEVICE(pd_dev), error);
}

static gboolean
fu_dell_kestrel_ec_probe_subcomponents(FuDevice *device, GError **error)
{
	g_return_val_if_fail(device != NULL, FALSE);

	/* Package */
	if (!fu_dell_kestrel_ec_probe_package(device, error))
		return FALSE;

	/* PD UP5 */
	if (!fu_dell_kestrel_ec_probe_pd(device,
					 FU_DELL_KESTREL_EC_DEV_TYPE_PD,
					 FU_DELL_KESTREL_EC_DEV_SUBTYPE_TI,
					 FU_DELL_KESTREL_EC_DEV_INSTANCE_TI_UP5,
					 error))
		return FALSE;

	/* PD UP15 */
	if (!fu_dell_kestrel_ec_probe_pd(device,
					 FU_DELL_KESTREL_EC_DEV_TYPE_PD,
					 FU_DELL_KESTREL_EC_DEV_SUBTYPE_TI,
					 FU_DELL_KESTREL_EC_DEV_INSTANCE_TI_UP15,
					 error))
		return FALSE;

	/* PD UP17 */
	if (!fu_dell_kestrel_ec_probe_pd(device,
					 FU_DELL_KESTREL_EC_DEV_TYPE_PD,
					 FU_DELL_KESTREL_EC_DEV_SUBTYPE_TI,
					 FU_DELL_KESTREL_EC_DEV_INSTANCE_TI_UP17,
					 error))
		return FALSE;

	/* DP MUX | Retimer */
	if (fu_dell_kestrel_ec_dev_entry(device, FU_DELL_KESTREL_EC_DEV_TYPE_DP_MUX, 0, 0) !=
	    NULL) {
		g_autoptr(FuDellKestrelDpmux) dpmux_device = NULL;

		dpmux_device = fu_dell_kestrel_dpmux_new(device);
		if (!fu_dell_kestrel_ec_create_node(device, FU_DEVICE(dpmux_device), error))
			return FALSE;
	}

	/* WT PD */
	if (fu_dell_kestrel_ec_dev_entry(device, FU_DELL_KESTREL_EC_DEV_TYPE_WTPD, 0, 0) != NULL) {
		g_autoptr(FuDellKestrelWtpd) wt_dev = NULL;

		wt_dev = fu_dell_kestrel_wtpd_new(device);
		if (!fu_dell_kestrel_ec_create_node(device, FU_DEVICE(wt_dev), error))
			return FALSE;
	}

	/* LAN */
	if (fu_dell_kestrel_ec_dev_entry(device, FU_DELL_KESTREL_EC_DEV_TYPE_LAN, 0, 0) != NULL) {
		g_autoptr(FuDellKestrelIlan) ilan_device = NULL;

		ilan_device = fu_dell_kestrel_ilan_new(device);
		if (!fu_dell_kestrel_ec_create_node(device, FU_DEVICE(ilan_device), error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_dock_type_extract(FuDevice *device, GError **error)
{
	FuDellDockBaseType dock_type = fu_dell_kestrel_ec_get_dock_type(device);
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_MAIN_EC;

	/* don't change error type, the plugin ignores it */
	if (dock_type != FU_DELL_DOCK_BASE_TYPE_KESTREL) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "No valid dock was found");
		return FALSE;
	}

	/* this will trigger setting up all the quirks */
	fu_device_add_instance_u8(device, "DOCKTYPE", dock_type);
	fu_device_add_instance_u8(device, "DEVTYPE", dev_type);
	fu_device_build_instance_id(device,
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
fu_dell_kestrel_ec_dock_type_cmd(FuDevice *device, GError **error)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);
	FuDellKestrelEcHidCmd cmd = FU_DELL_KESTREL_EC_HID_CMD_GET_DOCK_TYPE;
	gsize length = 1;
	g_autoptr(GByteArray) res = g_byte_array_new_take(g_malloc0(length), length);

	/* expect response 1 byte */
	if (!fu_dell_kestrel_ec_read(device, cmd, res, error)) {
		g_prefix_error(error, "Failed to query dock type: ");
		return FALSE;
	}

	self->base_type = res->data[0];

	/* check dock type to proceed with this plugin or exit as unsupported */
	return fu_dell_kestrel_ec_dock_type_extract(device, error);
}

static gboolean
fu_dell_kestrel_ec_dock_info_extract(FuDevice *device, GError **error)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);

	if (!self->dock_info) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "Failed to parse dock info");
		return FALSE;
	}

	if (self->dock_info->header.total_devices == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_SIGNATURE_INVALID,
			    "No bridge devices detected, dock may be booting up");
		return FALSE;
	}
	g_info("found %u devices [%u->%u]",
	       self->dock_info->header.total_devices,
	       self->dock_info->header.first_index,
	       self->dock_info->header.last_index);

	for (guint i = 0; i < self->dock_info->header.total_devices; i++) {
		struct FuDellKestrelEcQueryEntry dev_entry = self->dock_info->devices[i];
		const gchar *type_str;
		const gchar *location_str;
		guint32 version32 = GUINT32_FROM_BE(dev_entry.version.version_32);
		g_autofree gchar *version_str = NULL;

		/* name the component */
		type_str = fu_dell_kestrel_ec_devicetype_to_str(dev_entry.ec_addr_map.device_type,
								dev_entry.ec_addr_map.sub_type,
								dev_entry.ec_addr_map.instance);
		if (type_str == NULL) {
			g_warning("missing device name, DevType: %u, SubType: %u, Inst: %u",
				  dev_entry.ec_addr_map.device_type,
				  dev_entry.ec_addr_map.sub_type,
				  dev_entry.ec_addr_map.instance);
			continue;
		}

		/* name the location of component */
		location_str = (dev_entry.ec_addr_map.location == FU_DELL_KESTREL_EC_LOCATION_BASE)
				   ? "Base"
				   : "Module";

		/* show the component location */
		g_debug("#%u: %s located in %s (A: %u I: %u)",
			i,
			type_str,
			location_str,
			dev_entry.ec_addr_map.arg,
			dev_entry.ec_addr_map.instance);

		/* show the component version */
		version_str = fu_version_from_uint32_hex(version32, FWUPD_VERSION_FORMAT_QUAD);
		g_debug("version32: %08x, version8: %s", dev_entry.version.version_32, version_str);
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_dock_info_cmd(FuDevice *device, GError **error)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);
	FuDellKestrelEcHidCmd cmd = FU_DELL_KESTREL_EC_HID_CMD_GET_DOCK_INFO;
	gsize length = sizeof(FuDellKestrelDockInfoStructure);
	g_autoptr(GByteArray) res = g_byte_array_new_take(g_malloc0(length), length);

	/* get dock info over HID */
	if (!fu_dell_kestrel_ec_read(device, cmd, res, error)) {
		g_prefix_error(error, "Failed to query dock info: ");
		return FALSE;
	}

	if (!fu_memcpy_safe((guint8 *)self->dock_info,
			    length,
			    0,
			    res->data,
			    res->len,
			    0,
			    length,
			    error))
		return FALSE;

	if (!fu_dell_kestrel_ec_dock_info_extract(device, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_dock_data_extract(FuDevice *device, GError **error)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);
	const gchar *service_tag_default = "0000000";
	g_autofree gchar *serial = NULL;

	/* set FuDevice name */
	if (self->dock_data->marketing_name[0] != '\0')
		fu_device_set_name(device, (const gchar *)&self->dock_data->marketing_name);
	else
		g_warning("[EC bug] Invalid dock name detected");

	/* repair service tag (if not set) */
	if (self->dock_data->service_tag[0] == '\0')
		if (!fu_memcpy_safe(self->dock_data->service_tag,
				    sizeof(self->dock_data->service_tag),
				    0,
				    (const guint8 *)service_tag_default,
				    sizeof(service_tag_default),
				    0,
				    sizeof(self->dock_data->service_tag),
				    error))
			return FALSE;

	/* set FuDevice serial */
	serial = g_strdup_printf("%.7s/%016" G_GUINT64_FORMAT,
				 self->dock_data->service_tag,
				 self->dock_data->module_serial);
	fu_device_set_serial(device, serial);

	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_dock_data_cmd(FuDevice *device, GError **error)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);
	FuDellKestrelEcHidCmd cmd = FU_DELL_KESTREL_EC_HID_CMD_GET_DOCK_DATA;
	gsize length = sizeof(FuDellKestrelDockDataStructure);
	g_autoptr(GByteArray) res = g_byte_array_new_take(g_malloc0(length), length);

	/* get dock data over HID */
	if (!fu_dell_kestrel_ec_read(device, cmd, res, error)) {
		g_prefix_error(error, "Failed to query dock data: ");
		return FALSE;
	}

	if (!fu_memcpy_safe((guint8 *)self->dock_data,
			    length,
			    0,
			    res->data,
			    res->len,
			    0,
			    length,
			    error))
		return FALSE;

	if (!fu_dell_kestrel_ec_dock_data_extract(device, error))
		return FALSE;

	return TRUE;
}

gboolean
fu_dell_kestrel_ec_is_dock_ready4update(FuDevice *device, GError **error)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);
	guint16 bitmask_fw_update_pending = 1 << 8;

	if (!fu_dell_kestrel_ec_dock_data_cmd(device, error))
		return FALSE;

	if ((self->dock_data->dock_status & bitmask_fw_update_pending) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "dock status (%x) has pending updates, unavailable for now.",
			    self->dock_data->dock_status);
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_dell_kestrel_ec_own_dock(FuDevice *device, gboolean lock, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new();
	g_autoptr(GError) error_local = NULL;

	fu_byte_array_append_uint8(req, FU_DELL_KESTREL_EC_HID_CMD_SET_MODIFY_LOCK);
	fu_byte_array_append_uint8(req, 2); // length of data
	fu_byte_array_append_uint16(req, lock ? 0xFFFF : 0x0000, G_LITTLE_ENDIAN);

	fu_device_sleep(device, 1000);
	if (!fu_dell_kestrel_ec_write(device, req, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND))
			g_debug("ignoring: %s", error_local->message);
		else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			g_prefix_error(error, "failed to %s dock: ", lock ? "own" : "release");
			return FALSE;
		}
	}
	g_debug("dock is %s successfully", lock ? "owned" : "released");

	return TRUE;
}

gboolean
fu_dell_kestrel_ec_run_passive_update(FuDevice *device, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new();
	g_return_val_if_fail(device != NULL, FALSE);

	/* ec included in cmd, set bit2 in data for tbt */
	fu_byte_array_append_uint8(req, FU_DELL_KESTREL_EC_HID_CMD_SET_PASSIVE);
	fu_byte_array_append_uint8(req, 1); // length of data
	fu_byte_array_append_uint8(req, 0x02);

	g_debug("registered passive update (uod) flow");
	return fu_dell_kestrel_ec_write(device, req, error);
}

static gboolean
fu_dell_kestrel_ec_set_dock_sku(FuDevice *device, GError **error)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);

	switch (self->base_type) {
	case FU_DELL_DOCK_BASE_TYPE_KESTREL:
		/* TBT type yet available, do workaround */
		if (fu_dell_kestrel_ec_dev_entry(device,
						 FU_DELL_KESTREL_EC_DEV_TYPE_TBT,
						 FU_DELL_KESTREL_EC_DEV_SUBTYPE_BR,
						 0) != NULL) {
			self->base_sku = FU_DELL_KESTREL_DOCK_SKU_T5;
			return TRUE;
		}
		if (fu_dell_kestrel_ec_dev_entry(device,
						 FU_DELL_KESTREL_EC_DEV_TYPE_TBT,
						 FU_DELL_KESTREL_EC_DEV_SUBTYPE_GR,
						 0) != NULL) {
			self->base_sku = FU_DELL_KESTREL_DOCK_SKU_T4;
			return TRUE;
		}
		self->base_sku = FU_DELL_KESTREL_DOCK_SKU_DPALT;
		return TRUE;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "unsupported dock type: %x",
			    self->base_type);
		return FALSE;
	}
}

guint32
fu_dell_kestrel_ec_get_pd_version(FuDevice *device,
				  FuDellKestrelEcDevSubtype subtype,
				  FuDellKestrelEcDevInstance instance)
{
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_PD;
	struct FuDellKestrelEcQueryEntry *dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(device, dev_type, subtype, instance);
	return (dev_entry == NULL) ? 0 : GUINT32_FROM_BE(dev_entry->version.version_32);
}

guint32
fu_dell_kestrel_ec_get_ilan_version(FuDevice *device)
{
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_LAN;
	struct FuDellKestrelEcQueryEntry *dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(device, dev_type, 0, 0);
	return (dev_entry == NULL) ? 0 : GUINT32_FROM_BE(dev_entry->version.version_32);
}

guint32
fu_dell_kestrel_ec_get_wtpd_version(FuDevice *device)
{
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_WTPD;
	struct FuDellKestrelEcQueryEntry *dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(device, dev_type, 0, 0);
	return (dev_entry == NULL) ? 0 : GUINT32_FROM_BE(dev_entry->version.version_32);
}

guint32
fu_dell_kestrel_ec_get_dpmux_version(FuDevice *device)
{
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_DP_MUX;
	struct FuDellKestrelEcQueryEntry *dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(device, dev_type, 0, 0);
	return (dev_entry == NULL) ? 0 : GUINT32_FROM_BE(dev_entry->version.version_32);
}

guint32
fu_dell_kestrel_ec_get_rmm_version(FuDevice *device)
{
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_RMM;
	struct FuDellKestrelEcQueryEntry *dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(device, dev_type, 0, 0);
	return (dev_entry == NULL) ? 0 : GUINT32_FROM_BE(dev_entry->version.version_32);
}

static guint32
fu_dell_kestrel_ec_get_ec_version(FuDevice *device)
{
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_MAIN_EC;
	struct FuDellKestrelEcQueryEntry *dev_entry = NULL;

	dev_entry = fu_dell_kestrel_ec_dev_entry(device, dev_type, 0, 0);
	return (dev_entry == NULL) ? 0 : GUINT32_FROM_BE(dev_entry->version.version_32);
}

guint32
fu_dell_kestrel_ec_get_package_version(FuDevice *device)
{
	FuDellKestrelEc *self = FU_DELL_KESTREL_EC(device);
	return GUINT32_FROM_BE(self->dock_data->dock_firmware_pkg_ver);
}

gboolean
fu_dell_kestrel_ec_commit_package(FuDevice *device, GBytes *blob_fw, GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new();
	gsize length = g_bytes_get_size(blob_fw);

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(blob_fw != NULL, FALSE);

	/* verify package length */
	if (length != FU_STRUCT_DELL_KESTREL_PACKAGE_FW_VERSIONS_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Invalid package size %" G_GSIZE_FORMAT,
			    length);
		return FALSE;
	}

	fu_byte_array_append_uint8(req, FU_DELL_KESTREL_EC_HID_CMD_SET_DOCK_PKG);
	fu_byte_array_append_uint8(req, length); // length of data
	fu_byte_array_append_bytes(req, blob_fw);
	fu_dump_raw(G_LOG_DOMAIN, "->PACKAGE", req->data, req->len);

	if (!fu_dell_kestrel_ec_write(device, req, error)) {
		g_prefix_error(error, "Failed to commit package: ");
		return FALSE;
	}
	return TRUE;
}

static guint
fu_dell_kestrel_ec_get_chunk_delaytime(FuDellKestrelEcDevType dev_type)
{
	switch (dev_type) {
	case FU_DELL_KESTREL_EC_DEV_TYPE_MAIN_EC:
		return 3 * 1000;
	case FU_DELL_KESTREL_EC_DEV_TYPE_RMM:
		return 60 * 1000;
	case FU_DELL_KESTREL_EC_DEV_TYPE_PD:
		return 15 * 1000;
	case FU_DELL_KESTREL_EC_DEV_TYPE_LAN:
		return 70 * 1000;
	default:
		return 30 * 1000;
	}
}

static gsize
fu_dell_kestrel_ec_get_chunk_size(FuDellKestrelEcDevType dev_type)
{
	/* return the max chunk size in bytes */
	switch (dev_type) {
	case FU_DELL_KESTREL_EC_DEV_TYPE_MAIN_EC:
		return FU_DELL_KESTREL_EC_DEV_EC_CHUNK_SZ;
	case FU_DELL_KESTREL_EC_DEV_TYPE_RMM:
		return FU_DELL_KESTREL_EC_DEV_NO_CHUNK_SZ;
	default:
		return FU_DELL_KESTREL_EC_DEV_ANY_CHUNK_SZ;
	}
}

static guint
fu_dell_kestrel_ec_get_first_page_delaytime(FuDellKestrelEcDevType dev_type)
{
	return (dev_type == FU_DELL_KESTREL_EC_DEV_TYPE_RMM) ? 75 * 1000 : 0;
}

static gboolean
fu_dell_kestrel_ec_write_firmware_pages(FuDevice *device,
					FuChunkArray *pages,
					FuProgress *progress,
					FuDellKestrelEcDevType dev_type,
					guint chunk_idx,
					GError **error)
{
	guint first_page_delay = fu_dell_kestrel_ec_get_first_page_delaytime(dev_type);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(pages));

	for (guint j = 0; j < fu_chunk_array_length(pages); j++) {
		g_autoptr(GByteArray) page_aligned = g_byte_array_new();
		g_autoptr(FuChunk) page = NULL;
		g_autoptr(GBytes) page_bytes = NULL;
		g_autoptr(GError) error_local = NULL;

		page = fu_chunk_array_index(pages, j);
		if (page == NULL)
			return FALSE;

		g_debug("sending chunk: %u, page: %u/%u.",
			chunk_idx,
			j,
			fu_chunk_array_length(pages) - 1);

		/* strictly align the page size with 0x00 as packet */
		g_byte_array_append(page_aligned,
				    fu_chunk_get_data(page),
				    fu_chunk_get_data_sz(page));
		fu_byte_array_set_size(page_aligned, FU_DELL_KESTREL_EC_HID_DATA_PAGE_SZ, 0xFF);

		/* send to ec */
		if (!fu_dell_kestrel_ec_hid_write(device, page_aligned, &error_local)) {
			/* A buggy device may fail to send an acknowledgment receipt
			   after the last page write, resulting in a timeout error.

			   This is a known issue so waive it for now.
			*/
			if (dev_type == FU_DELL_KESTREL_EC_DEV_TYPE_LAN &&
			    j == fu_chunk_array_length(pages) - 1 &&
			    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INTERNAL)) {
				g_debug("ignored error: %s", error_local->message);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}

		/* device needs time to process incoming pages */
		if (j == 0) {
			g_debug("wait %u ms before the next page", first_page_delay);
			fu_device_sleep(device, first_page_delay);
		}
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_verify_chunk_result(FuDevice *device, guint chunk_idx, GError **error)
{
	guint8 res[FU_DELL_KESTREL_EC_HID_DATA_PAGE_SZ] = {0xff};

	if (!fu_hid_device_get_report(FU_HID_DEVICE(device),
				      0x0,
				      res,
				      sizeof(res),
				      FU_DELL_KESTREL_EC_HID_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;

	switch (res[1]) {
	case FU_DELL_KESTREL_EC_RESP_TO_CHUNK_UPDATE_COMPLETE:
		g_debug("dock response '%u' to chunk[%u]: firmware updated successfully.",
			res[1],
			chunk_idx);
		break;
	case FU_DELL_KESTREL_EC_RESP_TO_CHUNK_SEND_NEXT_CHUNK:
		g_debug("dock response '%u' to chunk[%u]: send next chunk.", res[1], chunk_idx);
		break;
	case FU_DELL_KESTREL_EC_RESP_TO_CHUNK_UPDATE_FAILED:
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "dock response '%u' to chunk[%u]: failed to write firmware.",
			    res[1],
			    chunk_idx);
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_dell_kestrel_ec_write_firmware_helper(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FuDellKestrelEcDevType dev_type,
					 guint8 dev_identifier,
					 GError **error)
{
	gsize fw_sz = 0;
	gsize chunk_sz = fu_dell_kestrel_ec_get_chunk_size(dev_type);
	guint chunk_delay = fu_dell_kestrel_ec_get_chunk_delaytime(dev_type);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* basic test */
	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* payload size */
	fw_sz = g_bytes_get_size(fw);

	if (fu_firmware_get_version(firmware) != 0x0) {
		g_debug("writing %s firmware %s -> %s",
			fu_device_get_name(device),
			fu_device_get_version(device),
			fu_firmware_get_version(firmware));
	}

	/* maximum buffer size */
	chunks = fu_chunk_array_new_from_bytes(fw, 0, chunk_sz);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	/* iterate the chunks */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(FuChunkArray) pages = NULL;
		g_autoptr(GBytes) buf = NULL;

		chk = fu_chunk_array_index(chunks, i);
		if (chk == NULL)
			return FALSE;

		/* prepend header and command to the chunk data */
		buf = fu_dell_kestrel_ec_hid_fwup_pkg_new(chk, fw_sz, dev_type, dev_identifier);

		/* slice the chunk into pages */
		pages = fu_chunk_array_new_from_bytes(buf, 0, FU_DELL_KESTREL_EC_HID_DATA_PAGE_SZ);

		/* write pages */
		if (!fu_dell_kestrel_ec_write_firmware_pages(device,
							     pages,
							     fu_progress_get_child(progress),
							     dev_type,
							     i,
							     error))
			return FALSE;

		/* delay time */
		g_debug("wait %u ms for dock to finish the chunk", chunk_delay);
		fu_device_sleep(device, chunk_delay);

		/* ensure the chunk has been acknowledged */
		if (!fu_dell_kestrel_ec_verify_chunk_result(device, i, error))
			return FALSE;

		fu_progress_step_done(progress);
	}

	/* success */
	g_debug("firmware written successfully");

	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	return fu_dell_kestrel_ec_write_firmware_helper(device,
							firmware,
							progress,
							FU_DELL_KESTREL_EC_DEV_TYPE_MAIN_EC,
							0,
							error);
}

static gboolean
fu_dell_kestrel_ec_query_cb(FuDevice *device, gpointer user_data, GError **error)
{
	/* dock data */
	if (!fu_dell_kestrel_ec_dock_data_cmd(device, error))
		return FALSE;

	/* dock info */
	if (!fu_dell_kestrel_ec_dock_info_cmd(device, error))
		return FALSE;

	/* set internal dock sku, must after dock info */
	if (!fu_dell_kestrel_ec_set_dock_sku(device, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_reload(FuDevice *device, GError **error)
{
	/* if query looks bad, wait a few seconds and retry */
	if (!fu_device_retry_full(device, fu_dell_kestrel_ec_query_cb, 10, 2000, NULL, error)) {
		g_prefix_error(error, "failed to query dock ec: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_ec_setup(FuDevice *device, GError **error)
{
	guint32 ec_version = 0;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_dell_kestrel_ec_parent_class)->setup(device, error))
		return FALSE;

	/* get dock type */
	if (!fu_dell_kestrel_ec_dock_type_cmd(device, error))
		return FALSE;

	/* if query looks bad, wait a few seconds and retry */
	if (!fu_device_retry_full(device, fu_dell_kestrel_ec_query_cb, 10, 2000, NULL, error)) {
		g_prefix_error(error, "failed to query dock ec: ");
		return FALSE;
	}

	/* setup version */
	ec_version = fu_dell_kestrel_ec_get_ec_version(device);
	fu_device_set_version_raw(device, ec_version);

	/* create the subcomponents */
	if (!fu_dell_kestrel_ec_probe_subcomponents(device, error))
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
	g_free(self->dock_data);
	g_free(self->dock_info);
	G_OBJECT_CLASS(fu_dell_kestrel_ec_parent_class)->finalize(object);
}

static void
fu_dell_kestrel_ec_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_dell_kestrel_ec_init(FuDellKestrelEc *self)
{
	self->dock_data = g_new0(FuDellKestrelDockDataStructure, 1);
	self->dock_info = g_new0(FuDellKestrelDockInfoStructure, 1);

	fu_device_add_protocol(FU_DEVICE(self), "com.dell.kestrel");
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x413C");
	fu_device_add_icon(FU_DEVICE(self), "dock-usb");
	fu_device_set_summary(FU_DEVICE(self), "Dell Dock EC");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SKIPS_RESTART);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_EXPLICIT_ORDER);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_RETRY_OPEN);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_FLAGS);
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
	fu_device_incorporate(FU_DEVICE(self), device);
	fu_device_set_logical_id(FU_DEVICE(self), "ec");
	if (uod)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	return self;
}
