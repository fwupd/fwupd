/*
 * Copyright 2024 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-dell-dock-common.h"

#define BIT_SET(x, y)	(x |= (1 << y))
#define BIT_CLEAR(x, y) (x &= (~(1 << y)))

/* Used for EC HID communication */
#define I2C_EC_ADDRESS 0xec
const FuHIDI2CParameters ec_v2_base_settings = {
    .i2ctargetaddr = I2C_EC_ADDRESS,
    .regaddrlen = 1,
    .i2cspeed = I2C_SPEED_250K,
};

const DellDockComponent dock_component_ec_v2[] = {
    {DOCK_BASE_TYPE_K2, 0, 0, "USB\\VID_413C&PID_B06E&hub&k2_embedded"},
    {DOCK_BASE_TYPE_UNKNOWN, 0, 0, NULL},
};

typedef struct __attribute__((packed)) {
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
	gchar service_tag[7];
	gchar marketing_name[32];
	guint32 dock_error;
	guint32 dock_module_status;
	guint32 dock_module_error;
	guint8 reserved;
	guint32 dock_status;
	guint16 dock_state;
	guint16 dock_config;
	gchar dock_mac_addr[48];
	guint32 dock_capabilities;
	guint32 dock_policy;
	guint32 dock_temperature;
	guint32 dock_fan_speed;
	guint8 unused[35];
} FuDellDockVer2DockDataStructure;

typedef struct __attribute__((packed)) {
	guint32 ec_version;
	guint32 mst_version;
	guint32 hub1_version;
	guint32 hub2_version;
	guint32 tbt_version;
	guint32 pkg_version;
	guint32 pd_version;
	guint32 epr_version;
	guint32 dpmux_version;
	guint32 rmm_version;
	guint32 reserved[6];
} FuDellDockVer2DockFWVersion;

typedef struct __attribute__((packed)) {
	struct FuDellDockV2DockInfoHeader {
		guint8 total_devices;
		guint8 first_index;
		guint8 last_index;
	} header;
	struct FuDellDockEcV2QueryEntry {
		struct FuDellDockV2EcAddrMap {
			guint8 location;
			guint8 device_type;
			guint8 sub_type;
			guint8 arg;
			guint8 instance;
		} ec_addr_map;
		union {
			guint32 version_32;
			guint8 version_8[4];
		} __attribute__((packed)) version;
	} devices[20];
} FuDellDockVer2DockInfoStructure;

/* Private structure */
struct _FuDellDockEcV2 {
	FuDevice parent_instance;
	FuDellDockVer2DockDataStructure *dock_data;
	FuDellDockVer2DockInfoStructure *dock_info;
	FuDellDockVer2DockFWVersion *raw_versions;
	DockBaseType base_type;
	guint8 base_sku;
	guint8 unlock_target;
	gchar *ec_minimum_version;
	guint64 blob_version_offset;
	guint8 passive_flow;
	guint32 dock_unlock_status;
};

G_DEFINE_TYPE(FuDellDockEcV2, fu_dell_dock_ec_v2, FU_TYPE_HID_DEVICE)

const gchar *
fu_dell_dock_ec_v2_get_data_module_type(FuDevice *device)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);

	switch (self->dock_data->module_type) {
	case EC_V2_MODULE_TYPE_NO_MODULE:
		return "No module";
	case EC_V2_MODULE_TYPE_WATT130_DP:
		return "130W (DP)";
	case EC_V2_MODULE_TYPE_WATT130_UNIVERSAL:
		return "130W (Universal)";
	case EC_V2_MODULE_TYPE_WATT210_DUAL_C:
		return "210W (Dual Cable)";
	case EC_V2_MODULE_TYPE_WATT130_TBT4:
		return "130W (TBT4)";
	case EC_V2_MODULE_TYPE_QI_CHARGER:
		return "Qi Charger";
	case EC_V2_MODULE_TYPE_WIFI_RMM:
		return "WiFi RMM";
	default:
		return "unknown";
	}
}

const gchar *
fu_dell_dock_ec_v2_devicetype_to_str(guint8 device_type, guint8 sub_type, guint8 instance)
{
	switch (device_type) {
	case EC_V2_DOCK_DEVICE_TYPE_MAIN_EC:
		return "EC";
	case EC_V2_DOCK_DEVICE_TYPE_PD:
		if (sub_type == EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI) {
			if (instance == EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI_INSTANCE_UP5)
				return "PD UP5";
			if (instance == EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI_INSTANCE_UP15)
				return "PD UP15";
			if (instance == EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI_INSTANCE_UP17)
				return "PD UP17";
		}
		return NULL;
	case EC_V2_DOCK_DEVICE_TYPE_USBHUB:
		if (sub_type == EC_V2_DOCK_DEVICE_USBHUB_SUBTYPE_RTS5480)
			return "RTS5480 USB Hub";
		if (sub_type == EC_V2_DOCK_DEVICE_USBHUB_SUBTYPE_RTS5485)
			return "RTS5485 USB Hub";
		return NULL;
	case EC_V2_DOCK_DEVICE_TYPE_MST:
		if (sub_type == EC_V2_DOCK_DEVICE_MST_SUBTYPE_VMM8430)
			return "MST VMM8430";
		if (sub_type == EC_V2_DOCK_DEVICE_MST_SUBTYPE_VMM9430)
			return "MST VMM9430";
		return NULL;
	case EC_V2_DOCK_DEVICE_TYPE_TBT:
		if (sub_type == EC_V2_DOCK_DEVICE_TBT_SUBTYPE_TR)
			return "Titan Ridge";
		if (sub_type == EC_V2_DOCK_DEVICE_TBT_SUBTYPE_GR)
			return "Goshen Ridge";
		if (sub_type == EC_V2_DOCK_DEVICE_TBT_SUBTYPE_BR)
			return "Barlow Ridge";
		return NULL;
	case EC_V2_DOCK_DEVICE_TYPE_QI:
		return "Qi";
	case EC_V2_DOCK_DEVICE_TYPE_DP_MUX:
		return "DP Mux";
	case EC_V2_DOCK_DEVICE_TYPE_LAN:
		return "LAN";
	case EC_V2_DOCK_DEVICE_TYPE_FAN:
		return "Fan";
	case EC_V2_DOCK_DEVICE_TYPE_RMM:
		return "Remote Management";
	case EC_V2_DOCK_DEVICE_TYPE_WTPD:
		return "Weltrend PD";
	default:
		return NULL;
	}
}

DockBaseType
fu_dell_dock_ec_v2_get_dock_type(FuDevice *device)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	return self->base_type;
}

guint8
fu_dell_dock_ec_v2_get_dock_sku(FuDevice *device)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	return self->base_sku;
}

gboolean
fu_dell_dock_ec_v2_enable_tbt_passive(FuDevice *device)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);

	if (self->passive_flow > 0) {
		self->passive_flow |= EC_V2_PASSIVE_ACTION_AUTH_TBT;
		return TRUE;
	}
	return FALSE;
}

static gboolean
fu_dell_dock_ec_v2_read(FuDevice *device, guint32 cmd, gsize length, GBytes **bytes, GError **error)
{
	/* The first byte of result data will be the size of the return,
	hide this from callers */
	guint8 result_length = length + 1;
	g_autoptr(GBytes) bytes_local = NULL;
	const guint8 *result;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);

	if (!fu_dell_dock_hid_i2c_read(device,
				       cmd,
				       result_length,
				       &bytes_local,
				       &ec_v2_base_settings,
				       800,
				       error)) {
		g_prefix_error(error, "read over HID-I2C failed: ");
		return FALSE;
	}
	result = g_bytes_get_data(bytes_local, NULL);

	/* first byte of result should be size of our data */
	/*
	if (result[0] != length) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Invalid result data: %d expected %" G_GSIZE_FORMAT,
			    result[0],
			    length);
		return FALSE;
	}
	*/
	*bytes = g_bytes_new(result + 1, length);

	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_write(FuDevice *device, gsize length, guint8 *data, GError **error)
{
	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(length > 1, FALSE);

	if (!fu_dell_dock_hid_i2c_write(device, data, length, &ec_v2_base_settings, error)) {
		g_prefix_error(error, "write over HID-I2C failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_dock_type_extract(FuDevice *device, GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	g_autofree gchar *instance_id = NULL;

	/* this will trigger setting up all the quirks */
	instance_id =
	    g_strdup(fu_dell_dock_get_instance_id(self->base_type, dock_component_ec_v2, 0, 0));
	if (instance_id == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "No valid dock was found");
		return FALSE;
	}

	fu_device_add_instance_id(device, instance_id);
	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_dock_type_cmd(FuDevice *device, GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	const guint8 *result = NULL;
	gsize sz = 0;
	g_autoptr(GBytes) data = NULL;

	g_return_val_if_fail(device != NULL, FALSE);

	if (!fu_dell_dock_ec_v2_read(device, EC_V2_HID_CMD_GET_DOCK_TYPE, 1, &data, error)) {
		g_prefix_error(error, "Failed to query dock type: ");
		return FALSE;
	}

	result = g_bytes_get_data(data, &sz);
	if (sz != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "No valid dock was found");
		return FALSE;
	}

	self->base_type = result[0];
	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_dock_info_extract(FuDevice *device, GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);

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
		const gchar *type_str;
		const gchar *location_str;

		/* name the component */
		type_str = fu_dell_dock_ec_v2_devicetype_to_str(
		    self->dock_info->devices[i].ec_addr_map.device_type,
		    self->dock_info->devices[i].ec_addr_map.sub_type,
		    self->dock_info->devices[i].ec_addr_map.instance);
		if (type_str == NULL)
			continue;

		/* name the location of component */
		location_str =
		    (self->dock_info->devices[i].ec_addr_map.location == EC_V2_LOCATION_BASE)
			? "Base"
			: "Module";
		if (location_str == NULL)
			continue;

		/* show the component location */
		g_debug("#%u: %s located in %s (A: %u I: %u)",
			i,
			type_str,
			location_str,
			self->dock_info->devices[i].ec_addr_map.arg,
			self->dock_info->devices[i].ec_addr_map.instance);

		/* show the component version */
		g_debug("\tVersion32: %08x, Version8: %x %x %x %x",
			self->dock_info->devices[i].version.version_32,
			self->dock_info->devices[i].version.version_8[0],
			self->dock_info->devices[i].version.version_8[1],
			self->dock_info->devices[i].version.version_8[2],
			self->dock_info->devices[i].version.version_8[3]);

		/* setup EC component */
		if (self->dock_info->devices[i].ec_addr_map.device_type ==
		    EC_V2_DOCK_DEVICE_TYPE_MAIN_EC) {
			const gchar *ec_version = NULL;
			ec_version =
			    g_strdup_printf("%02x.%02x.%02x.%02x",
					    self->dock_info->devices[i].version.version_8[0],
					    self->dock_info->devices[i].version.version_8[1],
					    self->dock_info->devices[i].version.version_8[2],
					    self->dock_info->devices[i].version.version_8[3]);

			g_debug("\tParsed version %s", ec_version);
			fu_device_set_version(device, ec_version);
		}

		/* setup TBT component */
		if (self->dock_info->devices[i].ec_addr_map.device_type ==
		    EC_V2_DOCK_DEVICE_TYPE_TBT) {
			/* Thunderbolt SKU takes a little longer */
			guint64 tmp = fu_device_get_install_duration(device);
			fu_device_set_install_duration(device, tmp + 20);
		}
	}
	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_dock_info_cmd(FuDevice *device, GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	gsize length = sizeof(FuDellDockVer2DockInfoStructure);

	g_autoptr(GBytes) data = NULL;
	g_return_val_if_fail(device != NULL, FALSE);

	/* get dock info over HID */
	if (!fu_dell_dock_ec_v2_read(device, EC_V2_HID_CMD_GET_DOCK_INFO, length, &data, error)) {
		g_prefix_error(error, "Failed to query dock info: ");
		return FALSE;
	}
	if (!g_bytes_get_data(data, NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "Failed to read dock info");
		return FALSE;
	}
	if (g_bytes_get_size(data) != length) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Unexpected dock info size %" G_GSIZE_FORMAT,
			    g_bytes_get_size(data));
		return FALSE;
	}
	memcpy(self->dock_info, g_bytes_get_data(data, NULL), length);
	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_dock_data_extract(FuDevice *device, GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	g_autoptr(GString) name = NULL;
	g_autofree gchar *serial = NULL;
	const gchar *summary = NULL;
	const gchar service_tag_default[8] = "0000000";
	g_autofree gchar *board_type_str = NULL;

	/* set FuDevice name */
	name = g_string_new(self->dock_data->marketing_name);
	if (name->len > 0)
		fu_device_set_name(device, name->str);
	else
		g_warning("[EC bug] Invalid dock name detected");

	/* repair service tag (if not set) */
	if (self->dock_data->service_tag[0] == '\0')
		if (!fu_memcpy_safe((guint8 *)self->dock_data->service_tag,
				    sizeof(self->dock_data->service_tag),
				    0x0,
				    (guint8 *)service_tag_default,
				    sizeof(service_tag_default) - 1,
				    0x0,
				    sizeof(service_tag_default) - 1,
				    error))
			return FALSE;

	/* set FuDevice serial */
	serial = g_strdup_printf("%s/%08" G_GUINT64_FORMAT,
				 self->dock_data->service_tag,
				 self->dock_data->module_serial);
	fu_device_set_serial(device, serial);

	/* set FuDevice summary */
	board_type_str = g_strdup_printf("DellDockBoard%u", self->dock_data->board_id);
	summary = fu_device_get_metadata(device, board_type_str);
	if (summary != NULL)
		fu_device_set_summary(device, summary);

	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_dock_data_cmd(FuDevice *device, GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	g_autoptr(GBytes) data = NULL;
	const guint8 *result;
	gsize length = sizeof(FuDellDockVer2DockDataStructure);

	g_return_val_if_fail(device != NULL, FALSE);

	/* get dock data over HID */
	if (!fu_dell_dock_ec_v2_read(device, EC_V2_HID_CMD_GET_DOCK_DATA, length, &data, error)) {
		g_prefix_error(error, "Failed to query dock info: ");
		return FALSE;
	}

	result = g_bytes_get_data(data, NULL);
	if (result == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "Failed to read dock data");
		return FALSE;
	}

	memcpy(self->dock_data, result, length);
	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_is_fwupdate_available_cmd(FuDevice *device, GError **error)
{
	const guint8 *result = NULL;
	gsize sz = 0;
	g_autoptr(GBytes) data = NULL;

	g_return_val_if_fail(device != NULL, FALSE);
	if (!fu_dell_dock_ec_v2_read(device,
				     EC_V2_HID_CMD_GET_UPDATE_RDY_STATUS,
				     1,
				     &data,
				     error)) {
		g_prefix_error(error, "Failed to query dock fwupdate readiness status: ");
		return FALSE;
	}

	result = g_bytes_get_data(data, &sz);
	if (sz != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Invalid dock fwupdate readiness status size");
		return FALSE;
	}

	g_debug("Dock update readiness status: %x", result[0]);
	return result[0] == EC_V2_DOCK_UPDATE_AVAILABLE;
}

static void
fu_dell_dock_ec_v2_to_string(FuDevice *device, guint idt, GString *str)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	gchar service_tag[8] = {0x00};

	fwupd_codec_string_append_int(str, idt, "BaseType", self->base_type);
	fwupd_codec_string_append_int(str, idt, "BoardId", self->dock_data->board_id);
	fwupd_codec_string_append_int(str,
				      idt,
				      "PowerSupply",
				      self->dock_data->power_supply_wattage);
	fwupd_codec_string_append_hex(str, idt, "StatusPort0", self->dock_data->port0_dock_status);
	fwupd_codec_string_append_hex(str, idt, "StatusPort1", self->dock_data->port1_dock_status);
	memcpy(service_tag, self->dock_data->service_tag, 7);
	fwupd_codec_string_append(str, idt, "ServiceTag", service_tag);
	fwupd_codec_string_append_int(str,
				      idt,
				      "Configuration",
				      self->dock_data->dock_configuration);
	fwupd_codec_string_append_hex(str,
				      idt,
				      "PackageFirmwareVersion",
				      self->dock_data->dock_firmware_pkg_ver);
	fwupd_codec_string_append_int(str, idt, "ModuleSerial", self->dock_data->module_serial);
	fwupd_codec_string_append_int(str,
				      idt,
				      "OriginalModuleSerial",
				      self->dock_data->original_module_serial);
	fwupd_codec_string_append_int(str, idt, "Type", self->dock_data->dock_type);
	fwupd_codec_string_append_hex(str, idt, "ModuleType", self->dock_data->module_type);
	fwupd_codec_string_append(str, idt, "MinimumEc", self->ec_minimum_version);
	fwupd_codec_string_append_int(str, idt, "PassiveFlow", self->passive_flow);
}

gboolean
fu_dell_dock_ec_v2_modify_lock(FuDevice *device, guint8 target, gboolean unlocked, GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	guint32 cmd;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(target != 0, FALSE);

	cmd = EC_V2_HID_CMD_SET_MODIFY_LOCK | /* cmd */
	      2 << 8 |			      /* length of data arguments */
	      target << 16 |		      /* device to operate on */
	      unlocked << 24;		      /* unlock/lock */

	if (!fu_dell_dock_ec_v2_write(device, 4, (guint8 *)&cmd, error)) {
		g_prefix_error(error, "Failed to unlock device %d: ", target);
		return FALSE;
	}
	g_debug("Modified lock for %d to %d through %s (%s)",
		target,
		unlocked,
		fu_device_get_name(device),
		fu_device_get_id(device));

	if (unlocked)
		BIT_SET(self->dock_unlock_status, target);
	else
		BIT_CLEAR(self->dock_unlock_status, target);
	g_debug("current overall unlock status: 0x%08x", self->dock_unlock_status);

	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	guint16 cmd = EC_V2_HID_CMD_SET_DOCK_RESET;

	g_return_val_if_fail(device != NULL, FALSE);

	return fu_dell_dock_ec_v2_write(device, 2, (guint8 *)&cmd, error);
}

gboolean
fu_dell_dock_ec_v2_trigger_passive_flow(FuDevice *device, GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	guint32 cmd;

	cmd = EC_V2_HID_CMD_SET_PASSIVE | /* cmd */
	      1 << 8 |			  /* length of data arguments */
	      self->passive_flow << 16;

	g_return_val_if_fail(device != NULL, FALSE);

	g_info("activating passive flow (%x) for %s",
	       self->passive_flow,
	       fu_device_get_name(device));
	return fu_dell_dock_ec_v2_write(device, 3, (guint8 *)&cmd, error);
}

struct FuDellDockEcV2QueryEntry *
fu_dell_dock_ec_v2_dev_entry(FuDevice *device, guint8 device_type, guint8 sub_type, guint8 instance)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);

	for (guint i = 0; i < self->dock_info->header.total_devices; i++) {
		if (self->dock_info->devices[i].ec_addr_map.device_type != device_type)
			continue;
		if (sub_type != 0 && self->dock_info->devices[i].ec_addr_map.sub_type != sub_type)
			continue;

		/* vary by instance index */
		if (device_type == EC_V2_DOCK_DEVICE_TYPE_PD &&
		    self->dock_info->devices[i].ec_addr_map.instance != instance)
			continue;

		return &self->dock_info->devices[i];
	}
	return NULL;
}

static gboolean
fu_dell_dock_ec_v2_set_dock_sku(FuDevice *device, GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);

	switch (self->base_type) {
	case DOCK_BASE_TYPE_K2:
		/* TBT type yet available, do workaround */
		if (fu_dell_dock_ec_v2_dev_entry(device,
						 EC_V2_DOCK_DEVICE_TYPE_PD,
						 EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI,
						 EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI_INSTANCE_UP17) !=
		    NULL) {
			self->base_sku = K2_DOCK_SKU_TBT5;
			return TRUE;
		}
		if (fu_dell_dock_ec_v2_dev_entry(device,
						 EC_V2_DOCK_DEVICE_TYPE_TBT,
						 EC_V2_DOCK_DEVICE_TBT_SUBTYPE_GR,
						 0) != NULL) {
			self->base_sku = K2_DOCK_SKU_TBT4;
			return TRUE;
		}
		self->base_sku = K2_DOCK_SKU_DPALT;
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
fu_dell_dock_ec_v2_get_pd_version(FuDevice *device, guint8 sub_type, guint8 instance)
{
	struct FuDellDockEcV2QueryEntry *dev_entry = NULL;

	dev_entry =
	    fu_dell_dock_ec_v2_dev_entry(device, EC_V2_DOCK_DEVICE_TYPE_PD, sub_type, instance);
	if (dev_entry == NULL)
		return 0;

	return dev_entry->version.version_32;
}

guint32
fu_dell_dock_ec_v2_get_wtpd_version(FuDevice *device)
{
	struct FuDellDockEcV2QueryEntry *dev_entry = NULL;

	dev_entry = fu_dell_dock_ec_v2_dev_entry(device, EC_V2_DOCK_DEVICE_TYPE_WTPD, 0, 0);
	if (dev_entry == NULL)
		return 0;

	return dev_entry->version.version_32;
}

guint32
fu_dell_dock_ec_v2_get_dpmux_version(FuDevice *device)
{
	struct FuDellDockEcV2QueryEntry *dev_entry = NULL;

	dev_entry = fu_dell_dock_ec_v2_dev_entry(device, EC_V2_DOCK_DEVICE_TYPE_DP_MUX, 0, 0);
	if (dev_entry == NULL)
		return 0;

	return dev_entry->version.version_32;
}

guint32
fu_dell_dock_ec_v2_get_package_version(FuDevice *device)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);

	return self->dock_data->dock_firmware_pkg_ver;
}

gboolean
fu_dell_dock_ec_v2_commit_package(FuDevice *device, GBytes *blob_fw, GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	gsize length = 0;
	const guint8 *data = g_bytes_get_data(blob_fw, &length);
	g_autofree guint8 *payload = g_malloc0(length + 2);

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(blob_fw != NULL, FALSE);

	if (length != sizeof(FuDellDockVer2DockFWVersion)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "Invalid package size %" G_GSIZE_FORMAT,
			    length);
		return FALSE;
	}
	memcpy(self->raw_versions, data, length);

	g_debug("Committing (%zu) bytes ", sizeof(FuDellDockVer2DockFWVersion));
	g_debug("\tec_version: %x", self->raw_versions->ec_version);
	g_debug("\tmst_version: %x", self->raw_versions->mst_version);
	g_debug("\thub1_version: %x", self->raw_versions->hub1_version);
	g_debug("\thub2_version: %x", self->raw_versions->hub2_version);
	g_debug("\ttbt_version: %x", self->raw_versions->tbt_version);
	g_debug("\tpkg_version: %x", self->raw_versions->pkg_version);
	g_debug("\tpd_version: %x", self->raw_versions->pd_version);
	g_debug("\tepr_version: %x", self->raw_versions->epr_version);
	g_debug("\tdpmux_version: %x", self->raw_versions->dpmux_version);
	g_debug("\trmm_version: %x", self->raw_versions->rmm_version);

	payload[0] = EC_V2_HID_CMD_SET_DOCK_PKG;
	payload[1] = length;
	memcpy(payload + 2, data, length);

	if (!fu_dell_dock_ec_v2_write(device, length + 2, payload, error)) {
		g_prefix_error(error, "Failed to query dock info: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_write_fw(FuDevice *device,
			    FuFirmware *firmware,
			    FuProgress *progress,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	g_autofree gchar *dynamic_version = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_whdr = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GByteArray) res = g_byte_array_sized_new(HID_v2_RESPONSE_LENGTH);

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	/* is EC ready to process updates */
	if (!fu_dell_dock_ec_v2_is_fwupdate_available_cmd(device, error)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "Device is not ready to process updates");
		return FALSE;
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 14, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* construct writing buffer */
	fw_whdr = fu_dell_dock_hid_v2_fwup_pkg_new(fw, EC_V2_DOCK_DEVICE_TYPE_MAIN_EC, 0);

	/* prepare the chunks */
	chunks = fu_chunk_array_new_from_bytes(fw_whdr, 0, HID_v2_DATA_PAGE_SZ);

	if (!fu_dell_dock_ec_v2_modify_lock(device, self->unlock_target, TRUE, error))
		return FALSE;

	if (!fu_dell_dock_hid_raise_mcu_clock(device, TRUE, error))
		return FALSE;

	/* erase */
	if (!fu_dell_dock_hid_erase_bank(device, 0xff, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write to device */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		if (!fu_dell_dock_hid_v2_write(device, fu_chunk_get_bytes(chk), error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* verify response
	fu_byte_array_set_size(res, HID_v2_RESPONSE_LENGTH, 0xff);
	if (!fu_dell_dock_hid_v2_read(device, res, error))
		return FALSE;

	if (res->data[1] != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed verification on HID response[1], expected 0x00, got 0x%02x",
			    res->data[1]);
		return FALSE;
	}
	*/
	if (!fu_dell_dock_hid_raise_mcu_clock(device, FALSE, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* dock will reboot to re-read; this is to appease the daemon */
	g_debug("ec firmware written successfully; waiting for dock to reboot");

	/* activate passive behavior */
	self->passive_flow |= EC_V2_PASSIVE_ACTION_FLASH_EC;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_set_quirk_kv(FuDevice *device,
				const gchar *key,
				const gchar *value,
				GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "DellDockUnlockTarget") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_10, error))
			return FALSE;
		self->unlock_target = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "DellDockVersionLowest") == 0) {
		self->ec_minimum_version = g_strdup(value);
		return TRUE;
	}
	if (g_str_has_prefix(key, "DellDockBoard")) {
		fu_device_set_metadata(device, key, value);
		return TRUE;
	}
	if (g_strcmp0(key, "DellDockBlobVersionOffset") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_10, error))
			return FALSE;
		self->blob_version_offset = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static gboolean
fu_dell_dock_ec_v2_query_cb(FuDevice *device, gpointer user_data, GError **error)
{
	/* dock data */
	if (!fu_dell_dock_ec_v2_dock_data_cmd(device, error))
		return FALSE;

	if (!fu_dell_dock_ec_v2_dock_data_extract(device, error))
		return FALSE;

	/* dock info */
	if (!fu_dell_dock_ec_v2_dock_info_cmd(device, error))
		return FALSE;

	if (!fu_dell_dock_ec_v2_dock_info_extract(device, error))
		return FALSE;

	/* set internal dock sku, must after dock info */
	if (!fu_dell_dock_ec_v2_set_dock_sku(device, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_setup(FuDevice *device, GError **error)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_dell_dock_ec_v2_parent_class)->setup(device, error))
		return FALSE;

	/* get dock type */
	if (!fu_dell_dock_ec_v2_dock_type_cmd(device, error))
		return FALSE;

	/* test dock type to proceed or exit as unsupported */
	if (!fu_dell_dock_ec_v2_dock_type_extract(device, error))
		return FALSE;

	/* if query looks bad, wait a few seconds and retry */
	if (!fu_device_retry_full(device, fu_dell_dock_ec_v2_query_cb, 10, 2000, NULL, error)) {
		g_prefix_error(error, "failed to query dock ec: ");
		return FALSE;
	}

	/* default enable dock reboot */
	self->passive_flow = EC_V2_PASSIVE_ACTION_REBOOT_DOCK;

	g_debug("dock-ec-v2->setup done successfully");
	return TRUE;
}

static gboolean
fu_dell_dock_ec_v2_open(FuDevice *device, GError **error)
{
	/* FuUdevDevice->open */
	return FU_DEVICE_CLASS(fu_dell_dock_ec_v2_parent_class)->open(device, error);
}

static void
fu_dell_dock_ec_v2_finalize(GObject *object)
{
	FuDellDockEcV2 *self = FU_DELL_DOCK_EC_V2(object);
	g_free(self->dock_data);
	g_free(self->dock_info);
	g_free(self->raw_versions);
	g_free(self->ec_minimum_version);
	G_OBJECT_CLASS(fu_dell_dock_ec_v2_parent_class)->finalize(object);
}

static void
fu_dell_dock_ec_v2_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_dell_dock_ec_v2_init(FuDellDockEcV2 *self)
{
	self->dock_data = g_new0(FuDellDockVer2DockDataStructure, 1);
	self->raw_versions = g_new0(FuDellDockVer2DockFWVersion, 1);
	self->dock_info = g_new0(FuDellDockVer2DockInfoStructure, 1);

	fu_device_add_protocol(FU_DEVICE(self), "com.dell.dock");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SKIPS_RESTART);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_EXPLICIT_ORDER);
}

static void
fu_dell_dock_ec_v2_class_init(FuDellDockEcV2Class *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_dell_dock_ec_v2_finalize;
	device_class->activate = fu_dell_dock_ec_v2_activate;
	device_class->to_string = fu_dell_dock_ec_v2_to_string;
	device_class->open = fu_dell_dock_ec_v2_open;
	device_class->setup = fu_dell_dock_ec_v2_setup;
	device_class->write_firmware = fu_dell_dock_ec_v2_write_fw;
	device_class->set_quirk_kv = fu_dell_dock_ec_v2_set_quirk_kv;
	device_class->set_progress = fu_dell_dock_ec_v2_set_progress;
}

FuDellDockEcV2 *
fu_dell_dock_ec_v2_new(FuDevice *device)
{
	FuDellDockEcV2 *self = NULL;
	FuContext *ctx = fu_device_get_context(device);

	self = g_object_new(FU_TYPE_DELL_DOCK_EC_V2, "context", ctx, NULL);
	fu_device_incorporate(FU_DEVICE(self), device);
	fu_device_set_physical_id(FU_DEVICE(self), fu_device_get_physical_id(device));
	fu_device_set_logical_id(FU_DEVICE(self), "ec");
	return self;
}
