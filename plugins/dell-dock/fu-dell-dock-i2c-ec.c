/*
 * Copyright (C) 2018 Dell Inc.
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
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include <appstream-glib.h>
#include <string.h>

#include "fu-usb-device.h"
#include "fwupd-error.h"

#include "fu-dell-dock-common.h"

#define I2C_EC_ADDRESS			0xec

#define EC_CMD_SET_DOCK_PKG		0x01
#define EC_CMD_GET_DOCK_INFO		0x02
#define EC_CMD_GET_DOCK_DATA		0x03
#define EC_CMD_GET_DOCK_TYPE		0x05
#define EC_CMD_MODIFY_LOCK		0x0a
#define EC_CMD_RESET			0x0b
#define EC_CMD_REBOOT			0x0c
#define EC_GET_FW_UPDATE_STATUS		0x0f

#define EXPECTED_DOCK_INFO_SIZE		0xb7
#define EXPECTED_DOCK_TYPE		0x04

typedef enum {
	FW_UPDATE_IN_PROGRESS,
	FW_UPDATE_COMPLETE,
} FuDellDockECFWUpdateStatus;

const FuHIDI2CParameters ec_base_settings = {
	.i2cslaveaddr = I2C_EC_ADDRESS,
	.regaddrlen = 1,
	.i2cspeed = I2C_SPEED_250K,
};

typedef enum {
	LOCATION_BASE,
	LOCATION_MODULE,
} FuDellDockLocationEnum;

typedef enum {
	FU_DELL_DOCK_DEVICETYPE_MAIN_EC = 0,
	FU_DELL_DOCK_DEVICETYPE_HUB = 3,
	FU_DELL_DOCK_DEVICETYPE_MST = 4,
	FU_DELL_DOCK_DEVICETYPE_TBT = 5,
} FuDellDockDeviceTypeEnum;

typedef enum {
	SUBTYPE_GEN2,
	SUBTYPE_GEN1,
} FuDellDockHubSubTypeEnum;

typedef struct __attribute__ ((packed)) {
	guint8 total_devices;
	guint8 first_index;
	guint8 last_index;
} FuDellDockDockInfoHeader;

typedef struct __attribute__ ((packed)) {
	guint8 location;
	guint8 device_type;
	guint8 sub_type;
	guint8 arg;
	guint8 instance;
} FuDellDockEcAddrMap;

typedef struct __attribute__ ((packed)) {
	FuDellDockEcAddrMap ec_addr_map;
	union {
		guint32 version_32;
		guint8 version_8[4];
	} version;
} FuDellDockEcQueryEntry;

typedef enum {
	MODULE_TYPE_SINGLE = 1,
	MODULE_TYPE_DUAL,
	MODULE_TYPE_TBT,
} FuDellDockDockModule;

typedef struct __attribute__ ((packed)) {
	guint8 dock_configuration;
	guint8 dock_type;
	guint16	power_supply_wattage;
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

typedef struct __attribute__ ((packed)) {
	guint32 ec_version;
	guint32 mst_version;
	guint32 hub1_version;
	guint32 hub2_version;
	guint32 tbt_version;
	guint32 pkg_version;
} FuDellDockDockPackageFWVersion;

struct _FuDellDockEc {
	FuDevice			 parent_instance;
	FuDellDockDockDataStructure 	*data;
	FuDellDockDockPackageFWVersion 	*raw_versions;
	gchar 				*ec_version;
	gchar 				*mst_version;
	gchar 				*tbt_version;
	FuDevice 			*symbiote;
	guint8				 unlock_target;
	guint8				 board_min;
	gchar				*ec_minimum_version;
	guint64				 blob_version_offset;
};

G_DEFINE_TYPE (FuDellDockEc, fu_dell_dock_ec, FU_TYPE_DEVICE)

/* Used to root out I2C communication problems */
static gboolean
fu_dell_dock_test_valid_byte (const guint8 *str, gint index)
{
	if (str[index] == 0x00 || str[index] == 0xff)
		return FALSE;
	return TRUE;
}

static void
fu_dell_dock_ec_set_board (FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);
	const gchar *summary = NULL;
	g_autofree gchar *board_type_str = NULL;

	board_type_str = g_strdup_printf ("DellDockBoard%u", self->data->board_id);
	summary = fu_device_get_metadata (device, board_type_str);
	if (summary != NULL)
		fu_device_set_summary (device, summary);
}

FuDevice *
fu_dell_dock_ec_get_symbiote (FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);

	return self->symbiote;
}

gboolean
fu_dell_dock_ec_has_tbt (FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);

	return self->data->module_type == MODULE_TYPE_TBT;
}

static const gchar*
fu_dell_dock_devicetype_to_str (guint device_type, guint sub_type)
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
	default:
		return NULL;
	}
}

static gboolean
fu_dell_dock_ec_read (FuDevice *device, guint32 cmd, gsize length,
		      GBytes **bytes, GError **error)
{
	/* The first byte of result data will be the size of the return,
	hide this from callers */
	guint8 result_length = length + 1;
	g_autoptr(GBytes) bytes_local = NULL;
	const guint8 *result;
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);

	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (self->symbiote != NULL, FALSE);
	g_return_val_if_fail (bytes != NULL, FALSE);

	if (!fu_dell_dock_hid_i2c_read (self->symbiote, cmd, result_length,
					&bytes_local, &ec_base_settings, error)) {
		g_prefix_error (error, "read over HID-I2C failed: ");
		return FALSE;
	}
	result = g_bytes_get_data (bytes_local, NULL);
	/* first byte of result should be size of our data */
	if (result[0] != length) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
				"Invalid result data: %d expected %" G_GSIZE_FORMAT,
				result[0], length);
		return FALSE;
	}
	*bytes = g_bytes_new (result + 1, length);

	return TRUE;
}

static gboolean
fu_dell_dock_ec_write (FuDevice *device, gsize length, guint8 *data, GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);

	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (self->symbiote != NULL, FALSE);
	g_return_val_if_fail (length > 1, FALSE);

	if (!fu_dell_dock_hid_i2c_write (self->symbiote, data, length,
					 &ec_base_settings, error)) {
		g_prefix_error (error, "write over HID-I2C failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_dock_is_valid_dock (FuDevice *device, GError **error)
{
	g_autoptr(GBytes) data = NULL;
	const guint8 *result = NULL;

	g_return_val_if_fail (device != NULL, FALSE);

	if (!fu_dell_dock_ec_read (device, EC_CMD_GET_DOCK_TYPE, 1, &data, error)) {
		g_prefix_error (error, "Failed to query dock type: ");
		return FALSE;
	}
	result = g_bytes_get_data (data, NULL);

	if (result == NULL || *result != EXPECTED_DOCK_TYPE) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND,
				     "No valid dock was found");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_dock_ec_get_dock_info (FuDevice *device,
			  GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);
	const FuDellDockDockInfoHeader *header = NULL;
	const FuDellDockEcQueryEntry *device_entry = NULL;
	const FuDellDockEcAddrMap *map = NULL;
	g_autoptr(GBytes) data = NULL;

	g_return_val_if_fail (device != NULL, FALSE);

	if (!fu_dell_dock_ec_read (device, EC_CMD_GET_DOCK_INFO,
				   EXPECTED_DOCK_INFO_SIZE,
				   &data, error)) {
		g_prefix_error (error, "Failed to query dock info: ");
		return FALSE;
	}
	if (!g_bytes_get_data (data, NULL)) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND,
				     "Failed to read dock info");
		return FALSE;
	}

	header = (FuDellDockDockInfoHeader *) g_bytes_get_data (data, NULL);
	if (!header) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND,
				     "Failed to parse dock info");
		return FALSE;
	}

	/* guard against EC not yet ready and fail init */
	if (header->total_devices == 0) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID,
			     "No bridge devices detected, dock may be booting up");
		return FALSE;
	}
	g_debug ("%u devices [%u->%u]",
		 header->total_devices,
		 header->first_index,
		 header->last_index);
	device_entry =
	    (FuDellDockEcQueryEntry *) ((guint8 *) header + sizeof(FuDellDockDockInfoHeader));
	for (guint i = 0; i < header->total_devices; i++) {
		const gchar *type_str;
		map = &(device_entry[i].ec_addr_map);
		type_str = fu_dell_dock_devicetype_to_str (map->device_type, map->sub_type);
		if (type_str == NULL)
			continue;
		g_debug ("#%u: %s in %s (A: %u I: %u)", i, type_str,
			 (map->location == LOCATION_BASE) ? "Base" : "Module",
			 map->arg, map->instance);
		g_debug ("\tVersion32: %08x\tVersion8: %x %x %x %x",
			 device_entry[i].version.version_32,
			 device_entry[i].version.version_8[0],
			 device_entry[i].version.version_8[1],
			 device_entry[i].version.version_8[2],
			 device_entry[i].version.version_8[3]);
		/* BCD but guint32 */
		if (map->device_type == FU_DELL_DOCK_DEVICETYPE_MAIN_EC) {
			self->raw_versions->ec_version = device_entry[i].version.version_32;
			self->ec_version = g_strdup_printf (
			    "%02x.%02x.%02x.%02x", device_entry[i].version.version_8[0],
			    device_entry[i].version.version_8[1],
			    device_entry[i].version.version_8[2],
			    device_entry[i].version.version_8[3]);
			g_debug ("\tParsed version %s", self->ec_version);
			fu_device_set_version (self, self->ec_version);

		} else if (map->device_type == FU_DELL_DOCK_DEVICETYPE_MST) {
			self->raw_versions->mst_version = device_entry[i].version.version_32;
			/* guard against invalid MST version read from EC */
			if (!fu_dell_dock_test_valid_byte (device_entry[i].version.version_8, 1)) {
				g_warning ("[EC Bug] EC read invalid MST version %08x",
					   device_entry[i].version.version_32);
				continue;
			}
			self->mst_version = g_strdup_printf ("%02x.%02x.%02x",
			    device_entry[i].version.version_8[1],
			    device_entry[i].version.version_8[2],
			    device_entry[i].version.version_8[3]);
			g_debug ("\tParsed version %s", self->mst_version);
		} else if (map->device_type == FU_DELL_DOCK_DEVICETYPE_TBT && fu_dell_dock_ec_has_tbt (device)) {
			/* guard against invalid Thunderbolt version read from EC */
			if (!fu_dell_dock_test_valid_byte (device_entry[i].version.version_8, 2)) {
				g_warning ("[EC bug] EC read invalid Thunderbolt version %08x",
					   device_entry[i].version.version_32);
				continue;
			}
			self->raw_versions->tbt_version = device_entry[i].version.version_32;
			self->tbt_version = g_strdup_printf ("%02x.%02x",
			    device_entry[i].version.version_8[2],
			    device_entry[i].version.version_8[3]);
			g_debug ("\tParsed version %s", self->tbt_version);
		} else if (map->device_type == FU_DELL_DOCK_DEVICETYPE_HUB) {
			g_debug ("\thub subtype: %u", map->sub_type);
			if (map->sub_type == SUBTYPE_GEN2)
				self->raw_versions->hub2_version = device_entry[i].version.version_32;
			else if (map->sub_type == SUBTYPE_GEN1)
				self->raw_versions->hub1_version = device_entry[i].version.version_32;
		}
	}

	/* minimum EC version this code will support */
	if (as_utils_vercmp (self->ec_version, self->ec_minimum_version) < 0) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "dock containing EC version %s is not supported",
			     self->ec_version);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_dock_ec_get_dock_data (FuDevice *device,
			       GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GString) name = NULL;
	gchar service_tag[8] = {0x00};
	const guint8 *result;
	gsize length = sizeof(FuDellDockDockDataStructure);
	g_autofree gchar *bundled_serial = NULL;

	g_return_val_if_fail (device != NULL, FALSE);

	if (!fu_dell_dock_ec_read (device, EC_CMD_GET_DOCK_DATA, length,
				   &data, error)) {
		g_prefix_error (error, "Failed to query dock info: ");
		return FALSE;
	}
	result = g_bytes_get_data (data, NULL);
	if (result == NULL) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND,
				     "Failed to read dock data");
		return FALSE;
	}
	if (g_bytes_get_size (data) != length) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
			     "Unexpected dock data size %" G_GSIZE_FORMAT,
			      g_bytes_get_size (data));
		return FALSE;
	}
	memcpy (self->data, result, length);

	/* guard against EC not yet ready and fail init */
	name = g_string_new (self->data->marketing_name);
	if (name->len == 0) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID,
			     "Invalid name detected, dock may be booting up");
		return FALSE;
	}
	fu_device_set_name (device, name->str);

	/* set serial number */
	memcpy (service_tag, self->data->service_tag, 7);
	bundled_serial = g_strdup_printf ("%s/%08" G_GUINT64_FORMAT,
					  service_tag,
					  self->data->module_serial);
	fu_device_set_serial (device, bundled_serial);

	/* copy this for being able to send in next commit transaction */
	self->raw_versions->pkg_version = self->data->dock_firmware_pkg_ver;

	/* make sure this hardware spin matches our expecations */
	if (self->data->board_id >= self->board_min) {
		fu_dell_dock_ec_set_board (device);
		fu_device_set_version (device, self->ec_version);
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	} else {
		g_warning ("This utility does not support this board, disabling updates for %s",
			   fu_device_get_name (device));
	}

	return TRUE;
}

static void
fu_dell_dock_ec_to_string (FuDevice *device, GString *str)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);
	gchar service_tag[8] = {0x00};

	g_string_append (str, "  FuDellDellDockEc:\n");
	g_string_append_printf (str, "\tboard ID: %u\n",
				self->data->board_id);
	g_string_append_printf (str, "\tpower supply: %uW\n",
				self->data->power_supply_wattage);
	g_string_append_printf (str, "\tstatus (port0): %x\n",
				self->data->port0_dock_status);
	g_string_append_printf (str, "\tstatus (port1): %x\n",
				self->data->port1_dock_status);
	memcpy (service_tag, self->data->service_tag, 7);
	g_string_append_printf (str, "\tservice tag: %s\n",
				service_tag);
	g_string_append_printf (str, "\tconfiguration: %u\n",
				self->data->dock_configuration);
	g_string_append_printf (str, "\tpackage firmware version: %x\n",
				self->data->dock_firmware_pkg_ver);
	g_string_append_printf (str, "\tmodule serial #: %08" G_GUINT64_FORMAT "\n",
				self->data->module_serial);
	g_string_append_printf (str, "\toriginal module serial #: %08" G_GUINT64_FORMAT "\n",
				self->data->original_module_serial);
	g_string_append_printf (str, "\ttype: %u\n",
				self->data->dock_type);
	g_string_append_printf (str, "\tmodule type: %x\n",
				self->data->module_type);
	g_string_append_printf (str, "\tminimum ec: %s\n",
				self->ec_minimum_version);
}

gboolean
fu_dell_dock_ec_modify_lock (FuDevice *device,
			     guint8 target,
			     gboolean unlocked,
			     GError **error)
{
	guint32 cmd;

	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (target != 0, FALSE);

	cmd = EC_CMD_MODIFY_LOCK |	/* cmd */
	      2 << 8 |			/* length of data arguments */
	      target << 16 |		/* device to operate on */
	      unlocked << 24;		/* unlock/lock */


	if (!fu_dell_dock_ec_write (device, 4, (guint8 *) &cmd, error)) {
		g_prefix_error (error, "Failed to unlock device %d: ", target);
		return FALSE;
	}
	g_debug ("Modified lock for %d to %d through %s (%s)",
		 target, unlocked,
		 fu_device_get_name (device),
		 fu_device_get_id (device));

	return TRUE;
}

static gboolean
fu_dell_dock_ec_reset (FuDevice *device, GError **error)
{
	guint16 cmd = EC_CMD_RESET;

	g_return_val_if_fail (device != NULL, FALSE);

	return fu_dell_dock_ec_write (device, 2, (guint8 *) &cmd, error);
}

gboolean
fu_dell_dock_ec_reboot_dock (FuDevice *device, GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);
	guint16 cmd = EC_CMD_REBOOT;

	g_return_val_if_fail (device != NULL, FALSE);

	if (fu_device_has_custom_flag (device, "skip-restart")) {
		g_debug ("Skipping reboot per quirk request");
		return TRUE;
	}

	/* TODO: Drop when bumping minimum EC version to 13+ */
	if (as_utils_vercmp (self->ec_version, "00.00.00.13") < 0)
		g_print ("\nEC Reboot API may fail on EC %s.  Please manually power cycle dock.\n",
			   self->ec_version);
	g_debug ("Rebooting %s", fu_device_get_name (device));

	return fu_dell_dock_ec_write (device, 2, (guint8 *) &cmd, error);
}

static gboolean
fu_dell_dock_get_ec_status (FuDevice *device,
			    FuDellDockECFWUpdateStatus *status_out,
			    GError **error)
{
	g_autoptr(GBytes) data = NULL;
	const guint8 *result = NULL;

	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (status_out != NULL, FALSE);

	if (!fu_dell_dock_ec_read (device, EC_GET_FW_UPDATE_STATUS, 1,
				   &data, error)) {
		g_prefix_error (error, "Failed to read FW update status: ");
		return FALSE;
	}
	result = g_bytes_get_data (data, NULL);

	if (!result) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND,
				     "Failed to read FW update status");
		return FALSE;
	}
	*status_out = *result;
	return TRUE;
}

const gchar*
fu_dell_dock_ec_get_tbt_version (FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);
	return self->tbt_version;
}

const gchar*
fu_dell_dock_ec_get_mst_version (FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);
	return self->mst_version;
}

guint32
fu_dell_dock_ec_get_status_version (FuDevice *device)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);
	return self->raw_versions->pkg_version;
}

gboolean
fu_dell_dock_ec_commit_package (FuDevice *device, GBytes *blob_fw,
				GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);
	gsize length = 0;
	const guint8 *data = g_bytes_get_data (blob_fw, &length);
	guint8 payload[length + 2];

	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (blob_fw != NULL, FALSE);

	if (length != sizeof(FuDellDockDockPackageFWVersion)) {
		g_set_error (error, G_IO_ERR, G_IO_ERROR_INVALID_DATA,
			     "Invalid package size %" G_GSIZE_FORMAT,
			     length);
		return FALSE;
	}
	memcpy (self->raw_versions, data, length);

	g_debug ("Committing (%zu) bytes ", sizeof(FuDellDockDockPackageFWVersion));
	g_debug ("\tec_version: %x", self->raw_versions->ec_version);
	g_debug ("\tmst_version: %x", self->raw_versions->mst_version);
	g_debug ("\thub1_version: %x", self->raw_versions->hub1_version);
	g_debug ("\thub2_version: %x", self->raw_versions->hub2_version);
	g_debug ("\ttbt_version: %x", self->raw_versions->tbt_version);
	g_debug ("\tpkg_version: %x", self->raw_versions->pkg_version);

	/* TODO: Drop when updating minimum EC to 11+ */
	if (as_utils_vercmp (self->ec_version, "00.00.00.11") < 0) {
		g_debug ("EC %s doesn't support package version, ignoring",
			 self->ec_version);
		return TRUE;
	}

	payload [0] = EC_CMD_SET_DOCK_PKG;
	payload [1] = length;
	memcpy (payload + 2, data, length);

	if (!fu_dell_dock_ec_write (device, length + 2, payload, error)) {
		g_prefix_error (error, "Failed to query dock info: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_dock_ec_write_fw (FuDevice *device, GBytes *blob_fw,
			  GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);
	FuDellDockECFWUpdateStatus status = FW_UPDATE_IN_PROGRESS;
	guint8 progress1 = 0, progress0 = 0;
	gsize fw_size = 0;
	const guint8 *data = g_bytes_get_data (blob_fw, &fw_size);
	gsize write_size =
	    (fw_size / HIDI2C_MAX_WRITE) >= 1 ? HIDI2C_MAX_WRITE : fw_size;
	gsize nwritten = 0;
	guint32 address = 0 | 0xff << 24;
	g_autofree gchar *dynamic_version = NULL;

	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (blob_fw != NULL, FALSE);

	dynamic_version = g_strndup ((gchar *) data + self->blob_version_offset, 11);
	g_debug ("Writing firmware version %s", dynamic_version);

	if (!fu_dell_dock_ec_modify_lock (device, self->unlock_target, TRUE, error))
		return FALSE;

	if (!fu_dell_dock_hid_raise_mcu_clock (self->symbiote, TRUE, error))
		return FALSE;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_dell_dock_hid_erase_bank (self->symbiote, 0xff, error))
		return FALSE;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	do {
		/* last packet */
		if (fw_size - nwritten < write_size)
			write_size = fw_size - nwritten;

		if (!fu_dell_dock_hid_write_flash (self->symbiote, address, data,
						   write_size, error)) {
			g_prefix_error (error, "write over HID failed: ");
			return FALSE;
		}
		fu_device_set_progress_full (device, nwritten, fw_size);
		nwritten += write_size;
		data += write_size;
		address += write_size;
	} while (nwritten < fw_size);

	if (!fu_dell_dock_hid_raise_mcu_clock (self->symbiote, FALSE, error))
		return FALSE;

	if (fu_device_has_custom_flag (device, "skip-restart")) {
		g_debug ("Skipping EC reset per quirk request");
		return TRUE;
	}

	if (!fu_dell_dock_ec_reset (device, error))
		return FALSE;

	/* notify daemon that this device will need to replug */
	fu_dell_dock_will_replug (device);

	/* poll for completion status */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
	while (status != FW_UPDATE_COMPLETE) {
		g_autoptr(GError) error_local = NULL;

		if (!fu_dell_dock_hid_get_ec_status (self->symbiote, &progress1,
						     &progress0, error)) {
			g_prefix_error (error, "Failed to read scratch: ");
			return FALSE;
		}
		g_debug ("Read %u and %u from scratch", progress1, progress0);
		if (progress0 > 100)
			progress0 = 100;
		fu_device_set_progress_full (device, progress0, 100);

		/* This is expected to fail until update is done
		 * TODO: After can guarantee EC version that reports status byte
		 *       don't call this until progress0 is 100
		 */
		if (!fu_dell_dock_get_ec_status (device, &status,
						 &error_local)) {
			g_debug ("Flash EC Received result: %s (status %u)",
				 error_local->message, status);
			return TRUE;
		}
	}

	/* dock will reboot to re-read; this is to appease the daemon */
	fu_device_set_version (device, dynamic_version);

	return TRUE;
}

static gboolean
fu_dell_dock_ec_set_quirk_kv (FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);

	if (g_strcmp0 (key, "DellDockUnlockTarget") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT8) {
			self->unlock_target = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid DellDockUnlockTarget");
		return FALSE;
	}
	if (g_strcmp0 (key, "DellDockBoardMin") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT8) {
			self->board_min = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid DellDockBoardMin");
		return FALSE;
	}
	if (g_strcmp0 (key, "DellDockVersionLowest") == 0) {
		self->ec_minimum_version = g_strdup (value);
		return TRUE;
	}
	if (g_str_has_prefix (key, "DellDockBoard")) {
		fu_device_set_metadata (device, key, value);
		return TRUE;
	}
	if (g_strcmp0 (key, "DellDockBlobVersionOffset") == 0) {
		self->blob_version_offset = fu_common_strtoull (value);
		return TRUE;
	}

	/* failed */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

static gboolean
fu_dell_dock_ec_probe (FuDevice *device, GError **error)
{
	/* this will trigger setting up all the quirks */
	fu_device_add_guid (device, DELL_DOCK_EC_GUID);

	return TRUE;
}

static gboolean
fu_dell_dock_ec_query (FuDevice *device, GError **error)
{
	if (!fu_dell_dock_ec_get_dock_data (device, error))
		return FALSE;

	return fu_dell_dock_ec_get_dock_info (device, error);
}

static gboolean
fu_dell_dock_ec_setup (FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	GPtrArray *children;

	/* if query looks bad, wait a few seconds and retry */
	if (!fu_dell_dock_ec_query (device, &error_local)) {
		if (g_error_matches (error_local,
				     FWUPD_ERROR,
				     FWUPD_ERROR_SIGNATURE_INVALID)) {
			g_warning ("%s", error_local->message);
			g_usleep (2 * G_USEC_PER_SEC);
			if (!fu_dell_dock_ec_query (device, error))
				return FALSE;
		} else {
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
	}

	/* call setup on all the children we produced */
	children = fu_device_get_children (device);
	for (guint i=0 ; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index (children, i);
		g_autoptr(FuDeviceLocker) locker = NULL;
		g_debug ("setup %s",
			 fu_device_get_name (child));
		locker = fu_device_locker_new (child, error);
		if (locker == NULL)
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_dock_ec_open (FuDevice *device, GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);

	if (!fu_device_open (self->symbiote, error))
		return FALSE;

	return fu_dell_dock_is_valid_dock (device, error);
}

static gboolean
fu_dell_dock_ec_close (FuDevice *device, GError **error)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (device);

	return fu_device_close (self->symbiote, error);
}

static void
fu_dell_dock_ec_finalize (GObject *object)
{
	FuDellDockEc *self = FU_DELL_DOCK_EC (object);
	g_object_unref (self->symbiote);
	g_free (self->ec_version);
	g_free (self->mst_version);
	g_free (self->tbt_version);
	g_free (self->data);
	g_free (self->raw_versions);
	g_free (self->ec_minimum_version);
	G_OBJECT_CLASS (fu_dell_dock_ec_parent_class)->finalize (object);
}

static void
fu_dell_dock_ec_init (FuDellDockEc *self)
{
	self->data = g_new0 (FuDellDockDockDataStructure, 1);
	self->raw_versions = g_new0 (FuDellDockDockPackageFWVersion, 1);
}

static void
fu_dell_dock_ec_class_init (FuDellDockEcClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_dell_dock_ec_finalize;
	klass_device->to_string = fu_dell_dock_ec_to_string;
	klass_device->probe = fu_dell_dock_ec_probe;
	klass_device->setup = fu_dell_dock_ec_setup;
	klass_device->open = fu_dell_dock_ec_open;
	klass_device->close = fu_dell_dock_ec_close;
	klass_device->write_firmware = fu_dell_dock_ec_write_fw;
	klass_device->set_quirk_kv = fu_dell_dock_ec_set_quirk_kv;
}

FuDellDockEc *
fu_dell_dock_ec_new (FuDevice *symbiote)
{
	FuDellDockEc *self = NULL;

	self = g_object_new (FU_TYPE_DELL_DOCK_EC, NULL);
	self->symbiote = g_object_ref (symbiote);
	fu_device_set_physical_id (FU_DEVICE (self),
				   fu_device_get_physical_id (self->symbiote));
	fu_device_set_logical_id (FU_DEVICE (self), "ec");

	return self;
}
