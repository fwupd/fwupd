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

#define I2C_EC_ADDRESS 0xec

#define EC_CMD_SET_DOCK_PKG	0x01
#define EC_CMD_GET_DOCK_INFO	0x02
#define EC_CMD_GET_DOCK_DATA	0x03
#define EC_CMD_GET_DOCK_TYPE	0x05
#define EC_CMD_MODIFY_LOCK	0x0a
#define EC_CMD_RESET		0x0b
#define EC_CMD_PASSIVE		0x0d
#define EC_GET_FW_UPDATE_STATUS 0x0f

#define EXPECTED_DOCK_INFO_SIZE 0xb7

#define TBT_MODE_MASK 0x01

#define PASSIVE_RESET_MASK  0x01
#define PASSIVE_REBOOT_MASK 0x02
#define PASSIVE_TBT_MASK    0x04

typedef enum {
	FW_UPDATE_IN_PROGRESS,
	FW_UPDATE_COMPLETE,
	FW_UPDATE_AUTHENTICATION_FAILED,
} FuDellDockECFWUpdateStatus;

const FuHIDI2CParameters ec_base_settings = {
    .i2ctargetaddr = I2C_EC_ADDRESS,
    .regaddrlen = 1,
    .i2cspeed = I2C_SPEED_250K,
};

typedef enum {
	LOCATION_BASE,
	LOCATION_MODULE,
} FuDellDockLocationEnum;

typedef enum {
	FU_DELL_DOCK_DEVICETYPE_MAIN_EC = 0,
	FU_DELL_DOCK_DEVICETYPE_PD = 1,
	FU_DELL_DOCK_DEVICETYPE_HUB = 3,
	FU_DELL_DOCK_DEVICETYPE_MST = 4,
	FU_DELL_DOCK_DEVICETYPE_TBT = 5,
} FuDellDockDeviceTypeEnum;

typedef enum {
	SUBTYPE_GEN2,
	SUBTYPE_GEN1,
} FuDellDockHubSubTypeEnum;

typedef struct __attribute__((packed)) { /* nocheck:blocked */
	guint8 total_devices;
	guint8 first_index;
	guint8 last_index;
} FuDellDockDockInfoHeader;

typedef struct __attribute__((packed)) { /* nocheck:blocked */
	guint8 location;
	guint8 device_type;
	guint8 sub_type;
	guint8 arg;
	guint8 instance;
} FuDellDockEcAddrMap;

typedef struct __attribute__((packed)) { /* nocheck:blocked */
	FuDellDockEcAddrMap ec_addr_map;
	union {
		guint32 version_32;
		guint8 version_8[4];
	} version;
} FuDellDockEcQueryEntry;

typedef enum {
	MODULE_TYPE_45_TBT = 1,
	MODULE_TYPE_45,
	MODULE_TYPE_130_TBT,
	MODULE_TYPE_130_DP,
	MODULE_TYPE_130_UNIVERSAL,
	MODULE_TYPE_240_TRIN,
	MODULE_TYPE_210_DUAL,
	MODULE_TYPE_130_USB4,
} FuDellDockDockModule;

typedef struct __attribute__((packed)) { /* nocheck:blocked */
	guint8 dock_configuration;
	guint8 dock_type;
	guint16 power_supply_wattage;
	guint16 module_type;
	guint16 board_id;
	guint16 port0_dock_status;
	guint16 port1_dock_status;
	guint32 dock_firmware_pkg_ver;
	guint64 module_serial;
	guint64 original_module_serial;
	gchar service_tag[7];
	gchar marketing_name[64];
} FuDellDockDockDataStructure;

typedef struct __attribute__((packed)) { /* nocheck:blocked */
	guint32 ec_version;
	guint32 mst_version;
	guint32 hub1_version;
	guint32 hub2_version;
	guint32 tbt_version;
	guint32 pkg_version;
} FuDellDockDockPackageFWVersion;

struct _FuDellDockEc {
	FuDevice parent_instance;
	FuDellDockDockDataStructure *data;
	FuDellDockDockPackageFWVersion *raw_versions;
	guint8 base_type;
	gchar *ec_version;
	gchar *mst_version;
	gchar *tbt_version;
	guint8 unlock_target;
	guint8 board_min;
	gchar *ec_minimum_version;
	guint64 blob_version_offset;
	guint8 passive_flow;
	guint32 dock_unlock_status;
};

static gboolean
fu_dell_dock_ec_get_status(FuDevice *device,
			   FuDellDockECFWUpdateStatus *status_out,
			   GError **error);

G_DEFINE_TYPE(FuDellDockEc, fu_dell_dock_ec, FU_TYPE_DEVICE)

/* Used to root out I2C communication problems */
static gboolean
fu_dell_dock_ec_test_valid_byte(const guint8 *str, gint index)
{
	if (str[index] == 0x00 || str[index] == 0xff)
		return FALSE;
	return TRUE;
}

static void
fu_dell_dock_ec_set_board(FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	const gchar *summary = NULL;
	g_autofree gchar *board_type_str = NULL;

	board_type_str = g_strdup_printf("DellDockBoard%u", self->data->board_id);
	summary = fu_device_get_metadata(device, board_type_str);
	if (summary != NULL)
		fu_device_set_summary(device, summary);
}

gboolean
fu_dell_dock_ec_module_is_usb4(FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	return self->data->module_type == MODULE_TYPE_130_USB4;
}

guint8
fu_dell_dock_ec_get_dock_type(FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	return self->base_type;
}

const gchar *
fu_dell_dock_ec_get_module_type(FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	switch (self->data->module_type) {
	case MODULE_TYPE_45_TBT:
		return "45 (TBT)";
	case MODULE_TYPE_45:
		return "45";
	case MODULE_TYPE_130_TBT:
		return "130 (TBT)";
	case MODULE_TYPE_130_DP:
		return "130 (DP)";
	case MODULE_TYPE_130_UNIVERSAL:
		return "130 (Universal)";
	case MODULE_TYPE_240_TRIN:
		return "240 (Trinity)";
	case MODULE_TYPE_210_DUAL:
		return "210 (Dual)";
	case MODULE_TYPE_130_USB4:
		return "130 (TBT4)";
	default:
		return "unknown";
	}
}

gboolean
fu_dell_dock_ec_needs_tbt(FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	gboolean port0_tbt_mode = self->data->port0_dock_status & TBT_MODE_MASK;

	/* check for TBT module type */
	if (self->data->module_type != MODULE_TYPE_130_TBT &&
	    self->data->module_type != MODULE_TYPE_45_TBT)
		return FALSE;
	g_info("found thunderbolt dock, port mode: %d", port0_tbt_mode);

	return !port0_tbt_mode;
}

gboolean
fu_dell_dock_ec_tbt_passive(FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);

	if (self->passive_flow > 0) {
		self->passive_flow |= PASSIVE_TBT_MASK;
		return TRUE;
	}
	return FALSE;
}

static const gchar *
fu_dell_dock_ec_devicetype_to_str(guint device_type, guint sub_type)
{
	switch (device_type) {
	case FU_DELL_DOCK_DEVICETYPE_MAIN_EC:
		return "EC";
	case FU_DELL_DOCK_DEVICETYPE_MST:
		return "MST";
	case FU_DELL_DOCK_DEVICETYPE_TBT:
		return "Thunderbolt";
	case FU_DELL_DOCK_DEVICETYPE_HUB:
		if (sub_type == SUBTYPE_GEN2)
			return "USB 3.1 Gen2";
		else if (sub_type == SUBTYPE_GEN1)
			return "USB 3.1 Gen1";
		return NULL;
	case FU_DELL_DOCK_DEVICETYPE_PD:
		return "PD";
	default:
		return NULL;
	}
}

static gboolean
fu_dell_dock_ec_read(FuDevice *device, guint32 cmd, gsize length, GBytes **bytes, GError **error)
{
	/* The first byte of result data will be the size of the return,
	hide this from callers */
	guint8 result_length = length + 1;
	g_autoptr(GBytes) bytes_local = NULL;
	const guint8 *result;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(fu_device_get_proxy(device) != NULL, FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);

	if (!fu_dell_dock_hid_i2c_read(fu_device_get_proxy(device),
				       cmd,
				       result_length,
				       &bytes_local,
				       &ec_base_settings,
				       error)) {
		g_prefix_error(error, "read over HID-I2C failed: ");
		return FALSE;
	}
	result = g_bytes_get_data(bytes_local, NULL);
	/* first byte of result should be size of our data */
	if (result[0] != length) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Invalid result data: %d expected %" G_GSIZE_FORMAT,
			    result[0],
			    length);
		return FALSE;
	}
	*bytes = g_bytes_new(result + 1, length);

	return TRUE;
}

static gboolean
fu_dell_dock_ec_write(FuDevice *device, gsize length, guint8 *data, GError **error)
{
	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(fu_device_get_proxy(device) != NULL, FALSE);
	g_return_val_if_fail(length > 1, FALSE);

	if (!fu_dell_dock_hid_i2c_write(fu_device_get_proxy(device),
					data,
					length,
					&ec_base_settings,
					error)) {
		g_prefix_error(error, "write over HID-I2C failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_dock_ec_is_valid_dock(FuDevice *device, GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	const guint8 *result = NULL;
	gsize sz = 0;
	g_autoptr(GBytes) data = NULL;

	g_return_val_if_fail(device != NULL, FALSE);

	if (!fu_dell_dock_ec_read(device, EC_CMD_GET_DOCK_TYPE, 1, &data, error)) {
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

	/* this will trigger setting up all the quirks */
	if (self->base_type == DOCK_BASE_TYPE_SALOMON) {
		fu_device_add_instance_id(device, DELL_DOCK_EC_INSTANCE_ID);
		return TRUE;
	}
	if (self->base_type == DOCK_BASE_TYPE_ATOMIC) {
		fu_device_add_instance_id(device, DELL_DOCK_ATOMIC_EC_INSTANCE_ID);
		return TRUE;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "Invalid dock type: %x",
		    self->base_type);
	return FALSE;
}

static gboolean
fu_dell_dock_ec_get_dock_info(FuDevice *device, GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	const FuDellDockDockInfoHeader *header = NULL;
	const FuDellDockEcQueryEntry *device_entry = NULL;
	const FuDellDockEcAddrMap *map = NULL;
	guint32 oldest_base_pd = 0;
	g_autoptr(GBytes) data = NULL;

	g_return_val_if_fail(device != NULL, FALSE);

	if (!fu_dell_dock_ec_read(device,
				  EC_CMD_GET_DOCK_INFO,
				  EXPECTED_DOCK_INFO_SIZE,
				  &data,
				  error)) {
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

	header = (FuDellDockDockInfoHeader *)g_bytes_get_data(data, NULL);
	if (!header) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "Failed to parse dock info");
		return FALSE;
	}

	/* guard against EC not yet ready and fail init */
	if (header->total_devices == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_SIGNATURE_INVALID,
			    "No bridge devices detected, dock may be booting up");
		return FALSE;
	}
	g_info("%u devices [%u->%u]",
	       header->total_devices,
	       header->first_index,
	       header->last_index);
	device_entry =
	    (FuDellDockEcQueryEntry *)((guint8 *)header + sizeof(FuDellDockDockInfoHeader));
	for (guint i = 0; i < header->total_devices; i++) {
		const gchar *type_str;
		map = &(device_entry[i].ec_addr_map);
		type_str = fu_dell_dock_ec_devicetype_to_str(map->device_type, map->sub_type);
		if (type_str == NULL)
			continue;
		g_debug("#%u: %s in %s (A: %u I: %u)",
			i,
			type_str,
			(map->location == LOCATION_BASE) ? "Base" : "Module",
			map->arg,
			map->instance);
		g_debug("\tVersion32: %08x\tVersion8: %x %x %x %x",
			device_entry[i].version.version_32,
			device_entry[i].version.version_8[0],
			device_entry[i].version.version_8[1],
			device_entry[i].version.version_8[2],
			device_entry[i].version.version_8[3]);
		/* BCD but guint32 */
		if (map->device_type == FU_DELL_DOCK_DEVICETYPE_MAIN_EC) {
			self->raw_versions->ec_version = device_entry[i].version.version_32;
			self->ec_version = g_strdup_printf("%02x.%02x.%02x.%02x",
							   device_entry[i].version.version_8[0],
							   device_entry[i].version.version_8[1],
							   device_entry[i].version.version_8[2],
							   device_entry[i].version.version_8[3]);
			g_debug("\tParsed version %s", self->ec_version);
			fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
			fu_device_set_version(FU_DEVICE(self), self->ec_version);

		} else if (map->device_type == FU_DELL_DOCK_DEVICETYPE_MST) {
			self->raw_versions->mst_version = device_entry[i].version.version_32;
			/* guard against invalid MST version read from EC */
			if (!fu_dell_dock_ec_test_valid_byte(device_entry[i].version.version_8,
							     1)) {
				g_warning("[EC Bug] EC read invalid MST version %08x",
					  device_entry[i].version.version_32);
				continue;
			}
			self->mst_version = g_strdup_printf("%02x.%02x.%02x",
							    device_entry[i].version.version_8[1],
							    device_entry[i].version.version_8[2],
							    device_entry[i].version.version_8[3]);
			g_debug("\tParsed version %s", self->mst_version);
		} else if (map->device_type == FU_DELL_DOCK_DEVICETYPE_TBT &&
			   (self->data->module_type == MODULE_TYPE_130_TBT ||
			    self->data->module_type == MODULE_TYPE_45_TBT ||
			    self->data->module_type == MODULE_TYPE_130_USB4)) {
			/* guard against invalid Thunderbolt version read from EC */
			if (!fu_dell_dock_ec_test_valid_byte(device_entry[i].version.version_8,
							     2)) {
				g_warning("[EC bug] EC read invalid Thunderbolt version %08x",
					  device_entry[i].version.version_32);
				continue;
			}
			self->raw_versions->tbt_version = device_entry[i].version.version_32;
			self->tbt_version = g_strdup_printf("%02x.%02x",
							    device_entry[i].version.version_8[2],
							    device_entry[i].version.version_8[3]);
			g_debug("\tParsed version %s", self->tbt_version);
		} else if (map->device_type == FU_DELL_DOCK_DEVICETYPE_HUB) {
			g_debug("\thub subtype: %u", map->sub_type);
			if (map->sub_type == SUBTYPE_GEN2)
				self->raw_versions->hub2_version =
				    device_entry[i].version.version_32;
			else if (map->sub_type == SUBTYPE_GEN1)
				self->raw_versions->hub1_version =
				    device_entry[i].version.version_32;
		} else if (map->device_type == FU_DELL_DOCK_DEVICETYPE_PD &&
			   map->location == LOCATION_BASE && map->sub_type == 0) {
			if (oldest_base_pd == 0 ||
			    device_entry[i].version.version_32 < oldest_base_pd)
				oldest_base_pd = GUINT32_TO_BE(device_entry[i].version.version_32);
			g_debug("\tParsed version: %02x.%02x.%02x.%02x",
				device_entry[i].version.version_8[0],
				device_entry[i].version.version_8[1],
				device_entry[i].version.version_8[2],
				device_entry[i].version.version_8[3]);
		}
	}

	/* Thunderbolt SKU takes a little longer */
	if (self->data->module_type == MODULE_TYPE_130_TBT ||
	    self->data->module_type == MODULE_TYPE_45_TBT ||
	    self->data->module_type == MODULE_TYPE_130_USB4) {
		guint64 tmp = fu_device_get_install_duration(device);
		fu_device_set_install_duration(device, tmp + 20);
	}

	/* passive flow is default enabled for production docks */
	self->passive_flow = PASSIVE_REBOOT_MASK;
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_SKIPS_RESTART);
	return TRUE;
}

static gboolean
fu_dell_dock_ec_get_dock_data(FuDevice *device, GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GString) name = NULL;
	gchar service_tag[8] = {0x00};
	const guint8 *result;
	gsize length = sizeof(FuDellDockDockDataStructure);
	g_autofree gchar *bundled_serial = NULL;
	FuDellDockECFWUpdateStatus status;

	g_return_val_if_fail(device != NULL, FALSE);

	if (!fu_dell_dock_ec_read(device, EC_CMD_GET_DOCK_DATA, length, &data, error)) {
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
	if (g_bytes_get_size(data) != length) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "Unexpected dock data size %" G_GSIZE_FORMAT,
			    g_bytes_get_size(data));
		return FALSE;
	}
	memcpy(self->data, result, length); /* nocheck:blocked */

	/* guard against EC not yet ready and fail init */
	name = g_string_new(self->data->marketing_name);
	if (name->len > 0)
		fu_device_set_name(device, name->str);
	else
		g_warning("[EC bug] Invalid dock name detected");

	if (self->data->module_type >= 0xfe)
		g_warning("[EC bug] Invalid module type 0x%02x", self->data->module_type);

	/* set serial number */
	memcpy(service_tag, self->data->service_tag, 7); /* nocheck:blocked */
	bundled_serial =
	    g_strdup_printf("%s/%08" G_GUINT64_FORMAT, service_tag, self->data->module_serial);
	fu_device_set_serial(device, bundled_serial);

	/* copy this for being able to send in next commit transaction */
	self->raw_versions->pkg_version = self->data->dock_firmware_pkg_ver;

	/* read if passive update pending */
	if (!fu_dell_dock_ec_get_status(device, &status, error))
		return FALSE;

	/* make sure this hardware spin matches our expectations */
	if (self->data->board_id >= self->board_min) {
		if (status != FW_UPDATE_IN_PROGRESS) {
			fu_dell_dock_ec_set_board(device);
			fu_device_uninhibit(device, "update-pending");
		} else {
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
			fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_UPDATE_PENDING);
		}
	} else {
		fu_device_inhibit(device, "not-supported", "Utility does not support this board");
	}

	return TRUE;
}

static void
fu_dell_dock_ec_to_string(FuDevice *device, guint idt, GString *str)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	gchar service_tag[8] = {0x00};

	fwupd_codec_string_append_int(str, idt, "BaseType", self->base_type);
	fwupd_codec_string_append_int(str, idt, "BoardId", self->data->board_id);
	fwupd_codec_string_append_int(str, idt, "PowerSupply", self->data->power_supply_wattage);
	fwupd_codec_string_append_hex(str, idt, "StatusPort0", self->data->port0_dock_status);
	fwupd_codec_string_append_hex(str, idt, "StatusPort1", self->data->port1_dock_status);
	memcpy(service_tag, self->data->service_tag, 7); /* nocheck:blocked */
	fwupd_codec_string_append(str, idt, "ServiceTag", service_tag);
	fwupd_codec_string_append_int(str, idt, "Configuration", self->data->dock_configuration);
	fwupd_codec_string_append_hex(str,
				      idt,
				      "PackageFirmwareVersion",
				      self->data->dock_firmware_pkg_ver);
	fwupd_codec_string_append_int(str, idt, "ModuleSerial", self->data->module_serial);
	fwupd_codec_string_append_int(str,
				      idt,
				      "OriginalModuleSerial",
				      self->data->original_module_serial);
	fwupd_codec_string_append_int(str, idt, "Type", self->data->dock_type);
	fwupd_codec_string_append_hex(str, idt, "ModuleType", self->data->module_type);
	fwupd_codec_string_append(str, idt, "MinimumEc", self->ec_minimum_version);
	fwupd_codec_string_append_int(str, idt, "PassiveFlow", self->passive_flow);
}

gboolean
fu_dell_dock_ec_modify_lock(FuDevice *device, guint8 target, gboolean unlocked, GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	guint32 cmd;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(target != 0, FALSE);

	cmd = EC_CMD_MODIFY_LOCK | /* cmd */
	      2 << 8 |		   /* length of data arguments */
	      target << 16 |	   /* device to operate on */
	      unlocked << 24;	   /* unlock/lock */

	if (!fu_dell_dock_ec_write(device, 4, (guint8 *)&cmd, error)) {
		g_prefix_error(error, "Failed to unlock device %d: ", target);
		return FALSE;
	}
	g_debug("Modified lock for %d to %d through %s (%s)",
		target,
		unlocked,
		fu_device_get_name(device),
		fu_device_get_id(device));

	if (unlocked)
		FU_BIT_SET(self->dock_unlock_status, target);
	else
		FU_BIT_CLEAR(self->dock_unlock_status, target);
	g_debug("current overall unlock status: 0x%08x", self->dock_unlock_status);

	return TRUE;
}

static gboolean
fu_dell_dock_ec_reset(FuDevice *device, GError **error)
{
	guint16 cmd = EC_CMD_RESET;

	g_return_val_if_fail(device != NULL, FALSE);

	return fu_dell_dock_ec_write(device, 2, (guint8 *)&cmd, error);
}

static gboolean
fu_dell_dock_ec_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDellDockECFWUpdateStatus status;

	/* read if passive update pending */
	if (!fu_dell_dock_ec_get_status(device, &status, error))
		return FALSE;

	if (status != FW_UPDATE_IN_PROGRESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "No firmware update pending for %s",
			    fu_device_get_name(device));
		return FALSE;
	}

	return fu_dell_dock_ec_reset(device, error);
}

gboolean
fu_dell_dock_ec_reboot_dock(FuDevice *device, GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	guint32 cmd = EC_CMD_PASSIVE | /* cmd */
		      1 << 8 |	       /* length of data arguments */
		      self->passive_flow << 16;

	g_return_val_if_fail(device != NULL, FALSE);

	g_info("activating passive flow (%x) for %s",
	       self->passive_flow,
	       fu_device_get_name(device));
	return fu_dell_dock_ec_write(device, 3, (guint8 *)&cmd, error);
}

static gboolean
fu_dell_dock_ec_get_status(FuDevice *device, FuDellDockECFWUpdateStatus *status_out, GError **error)
{
	g_autoptr(GBytes) data = NULL;
	const guint8 *result = NULL;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(status_out != NULL, FALSE);

	if (!fu_dell_dock_ec_read(device, EC_GET_FW_UPDATE_STATUS, 1, &data, error)) {
		g_prefix_error(error, "Failed to read FW update status: ");
		return FALSE;
	}
	result = g_bytes_get_data(data, NULL);

	if (!result) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "Failed to read FW update status");
		return FALSE;
	}
	*status_out = *result;
	return TRUE;
}

const gchar *
fu_dell_dock_ec_get_tbt_version(FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	return self->tbt_version;
}

const gchar *
fu_dell_dock_ec_get_mst_version(FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	return self->mst_version;
}

guint32
fu_dell_dock_ec_get_status_version(FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	return self->raw_versions->pkg_version;
}

gboolean
fu_dell_dock_ec_commit_package(FuDevice *device, GBytes *blob_fw, GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	gsize length = 0;
	const guint8 *data = g_bytes_get_data(blob_fw, &length);
	g_autofree guint8 *payload = g_malloc0(length + 2);

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(blob_fw != NULL, FALSE);

	if (length != sizeof(FuDellDockDockPackageFWVersion)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "Invalid package size %" G_GSIZE_FORMAT,
			    length);
		return FALSE;
	}
	memcpy(self->raw_versions, data, length); /* nocheck:blocked */

	g_debug("Committing (%zu) bytes ", sizeof(FuDellDockDockPackageFWVersion));
	g_debug("\tec_version: %x", self->raw_versions->ec_version);
	g_debug("\tmst_version: %x", self->raw_versions->mst_version);
	g_debug("\thub1_version: %x", self->raw_versions->hub1_version);
	g_debug("\thub2_version: %x", self->raw_versions->hub2_version);
	g_debug("\ttbt_version: %x", self->raw_versions->tbt_version);
	g_debug("\tpkg_version: %x", self->raw_versions->pkg_version);

	payload[0] = EC_CMD_SET_DOCK_PKG;
	payload[1] = length;
	memcpy(payload + 2, data, length); /* nocheck:blocked */

	if (!fu_dell_dock_ec_write(device, length + 2, payload, error)) {
		g_prefix_error(error, "Failed to query dock info: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_dock_ec_write_fw(FuDevice *device,
			 FuFirmware *firmware,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	gsize fw_size = 0;
	const guint8 *data;
	gsize write_size = 0;
	gsize nwritten = 0;
	guint32 address = 0 | 0xff << 24;
	g_autofree gchar *dynamic_version = NULL;
	g_autoptr(GBytes) fw = NULL;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 15, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	data = g_bytes_get_data(fw, &fw_size);
	write_size = (fw_size / HIDI2C_MAX_WRITE) >= 1 ? HIDI2C_MAX_WRITE : fw_size;
	dynamic_version = g_strndup((gchar *)data + self->blob_version_offset, 11);
	g_info("writing EC firmware version %s", dynamic_version);

	/* meet the minimum EC version */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS) == 0 &&
	    (fu_version_compare(dynamic_version,
				self->ec_minimum_version,
				FWUPD_VERSION_FORMAT_QUAD) < 0)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "New EC version %s is less than minimum required %s",
			    dynamic_version,
			    self->ec_minimum_version);
		return FALSE;
	}

	g_info("writing EC firmware version %s", dynamic_version);
	if (!fu_dell_dock_ec_modify_lock(device, self->unlock_target, TRUE, error))
		return FALSE;

	if (!fu_dell_dock_hid_raise_mcu_clock(fu_device_get_proxy(device), TRUE, error))
		return FALSE;

	/* erase */
	if (!fu_dell_dock_hid_erase_bank(fu_device_get_proxy(device), 0xff, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	do {
		/* last packet */
		if (fw_size - nwritten < write_size)
			write_size = fw_size - nwritten;

		if (!fu_dell_dock_hid_write_flash(fu_device_get_proxy(device),
						  address,
						  data,
						  write_size,
						  error)) {
			g_prefix_error(error, "write over HID failed: ");
			return FALSE;
		}
		fu_progress_set_percentage_full(fu_progress_get_child(progress), nwritten, fw_size);
		nwritten += write_size;
		data += write_size;
		address += write_size;
	} while (nwritten < fw_size);
	fu_progress_step_done(progress);

	if (!fu_dell_dock_hid_raise_mcu_clock(fu_device_get_proxy(device), FALSE, error))
		return FALSE;

	/* dock will reboot to re-read; this is to appease the daemon */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_version(device, dynamic_version);

	/* activate passive behavior */
	self->passive_flow |= PASSIVE_RESET_MASK;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	return TRUE;
}

static gboolean
fu_dell_dock_ec_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "DellDockUnlockTarget") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->unlock_target = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "DellDockBoardMin") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->board_min = tmp;
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
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
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
fu_dell_dock_ec_query(FuDevice *device, GError **error)
{
	if (!fu_dell_dock_ec_get_dock_data(device, error))
		return FALSE;

	return fu_dell_dock_ec_get_dock_info(device, error);
}

static gboolean
fu_dell_dock_ec_setup(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* if query looks bad, wait a few seconds and retry */
	if (!fu_dell_dock_ec_query(device, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID)) {
			g_warning("%s", error_local->message);
			fu_device_sleep(device, 2000); /* ms */
			if (!fu_dell_dock_ec_query(device, error))
				return FALSE;
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_dell_dock_ec_open(FuDevice *device, GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(device);

	if (!fu_device_open(fu_device_get_proxy(device), error))
		return FALSE;
	if (!self->data->dock_type)
		return fu_dell_dock_ec_is_valid_dock(device, error);
	return TRUE;
}

static gboolean
fu_dell_dock_ec_close(FuDevice *device, GError **error)
{
	return fu_device_close(fu_device_get_proxy(device), error);
}

static void
fu_dell_dock_ec_finalize(GObject *object)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC(object);
	g_free(self->ec_version);
	g_free(self->mst_version);
	g_free(self->tbt_version);
	g_free(self->data);
	g_free(self->raw_versions);
	g_free(self->ec_minimum_version);
	G_OBJECT_CLASS(fu_dell_dock_ec_parent_class)->finalize(object);
}

static void
fu_dell_dock_ec_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_dell_dock_ec_init(FuDellDockEc *self)
{
	self->data = g_new0(FuDellDockDockDataStructure, 1);
	self->raw_versions = g_new0(FuDellDockDockPackageFWVersion, 1);
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.dock");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_INHIBIT_CHILDREN);
}

static void
fu_dell_dock_ec_class_init(FuDellDockEcClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_dell_dock_ec_finalize;
	device_class->activate = fu_dell_dock_ec_activate;
	device_class->to_string = fu_dell_dock_ec_to_string;
	device_class->setup = fu_dell_dock_ec_setup;
	device_class->open = fu_dell_dock_ec_open;
	device_class->close = fu_dell_dock_ec_close;
	device_class->write_firmware = fu_dell_dock_ec_write_fw;
	device_class->set_quirk_kv = fu_dell_dock_ec_set_quirk_kv;
	device_class->set_progress = fu_dell_dock_ec_set_progress;
}

FuDellDockEc *
fu_dell_dock_ec_new(FuDevice *proxy)
{
	FuDellDockEc *self = NULL;
	FuContext *ctx = fu_device_get_context(proxy);

	self = g_object_new(FU_TYPE_DELL_DOCK_EC, "context", ctx, NULL);
	fu_device_set_proxy(FU_DEVICE(self), proxy);
	fu_device_incorporate(FU_DEVICE(self), proxy, FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
	fu_device_set_logical_id(FU_DEVICE(self), "ec");

	return self;
}
