/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-steelseries-fizz-gen1.h"
#include "fu-steelseries-fizz-impl.h"

#define STEELSERIES_FIZZ_VERSION_COMMAND	0x90U
#define STEELSERIES_FIZZ_VERSION_COMMAND_OFFSET 0x00U
#define STEELSERIES_FIZZ_VERSION_MODE_OFFSET	0x01U

#define STEELSERIES_FIZZ_COMMAND_TUNNEL_BIT 1U << 6

#define STEELSERIES_FIZZ_FILESYSTEM_RECEIVER 0x01U
#define STEELSERIES_FIZZ_FILESYSTEM_MOUSE    0x02U

#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_MAIN_BOOT_ID	  0x01U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_FSDATA_FILE_ID	  0x02U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_FACTORY_SETTINGS_ID  0x03U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_MAIN_APP_ID	  0x04U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_BACKUP_APP_ID	  0x05U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_PROFILES_MOUSE_ID	  0x06U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_PROFILES_LIGHTING_ID 0x0fU
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_PROFILES_DEVICE_ID	  0x10U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_PROFILES_RESERVED_ID 0x11U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_RECOVERY_ID	  0x0dU
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_FREE_SPACE_ID	  0xf1U

#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_SOFT_DEVICE_ID	0x00U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_PROFILES_MOUSE_ID	0x06U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_MAIN_APP_ID		0x07U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_BACKUP_APP_ID		0x08U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_MSB_DATA_ID		0x09U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_FACTORY_SETTINGS_ID	0x0aU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_FSDATA_FILE_ID	0x0bU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_MAIN_BOOT_ID		0x0cU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_RECOVERY_ID		0x0eU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_PROFILES_LIGHTING_ID	0x0fU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_PROFILES_DEVICE_ID	0x10U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_FDS_PAGES_ID		0x12U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_PROFILES_BLUETOOTH_ID 0x13U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_FREE_SPACE_ID		0xf0U

#define STEELSERIES_FIZZ_PAIRED_STATUS_COMMAND	      0xBBU
#define STEELSERIES_FIZZ_PAIRED_STATUS_COMMAND_OFFSET 0x00U
#define STEELSERIES_FIZZ_PAIRED_STATUS_STATUS_OFFSET  0x01U

#define STEELSERIES_FIZZ_CONNECTION_STATUS_COMMAND	  0xBCU
#define STEELSERIES_FIZZ_CONNECTION_STATUS_COMMAND_OFFSET 0x00U
#define STEELSERIES_FIZZ_CONNECTION_STATUS_STATUS_OFFSET  0x01U

#define STEELSERIES_FIZZ_BATTERY_LEVEL_COMMAND	      0x92U
#define STEELSERIES_FIZZ_BATTERY_LEVEL_COMMAND_OFFSET 0x00U
#define STEELSERIES_FIZZ_BATTERY_LEVEL_LEVEL_OFFSET   0x01U

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
fu_steelseries_fizz_gen1_cmd(FuSteelseriesFizzImpl *self,
			     guint8 *data,
			     gsize datasz,
			     gboolean answer,
			     GError **error)
{
	return fu_steelseries_device_cmd(FU_STEELSERIES_DEVICE(self), data, datasz, answer, error);
}

static gchar *
fu_steelseries_fizz_gen1_get_version(FuSteelseriesFizzImpl *self, gboolean tunnel, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	guint8 cmd = STEELSERIES_FIZZ_VERSION_COMMAND;
	const guint8 mode = 0U; /* string */

	if (tunnel)
		cmd |= STEELSERIES_FIZZ_COMMAND_TUNNEL_BIT;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_VERSION_COMMAND_OFFSET,
				    cmd,
				    error))
		return NULL;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_VERSION_MODE_OFFSET,
				    mode,
				    error))
		return NULL;

	fu_dump_raw(G_LOG_DOMAIN, "Version", data, sizeof(data));
	if (!fu_steelseries_fizz_gen1_cmd(self, data, sizeof(data), TRUE, error))
		return NULL;
	fu_dump_raw(G_LOG_DOMAIN, "Version", data, sizeof(data));

	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	/* success */
	return fu_memstrsafe(data, sizeof(data), 0x0, sizeof(data), error);
}

static guint8
fu_steelseries_fizz_gen1_get_fs_id(FuSteelseriesFizzImpl *self,
				   gboolean is_receiver,
				   GError **error)
{
	if (is_receiver)
		return STEELSERIES_FIZZ_FILESYSTEM_RECEIVER;

	return STEELSERIES_FIZZ_FILESYSTEM_MOUSE;
}

static guint8
fu_steelseries_fizz_gen1_get_file_id(FuSteelseriesFizzImpl *self,
				     gboolean is_receiver,
				     GError **error)
{
	if (is_receiver)
		return STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_BACKUP_APP_ID;

	return STEELSERIES_FIZZ_MOUSE_FILESYSTEM_BACKUP_APP_ID;
}

static gboolean
fu_steelseries_fizz_gen1_get_paired_status(FuSteelseriesFizzImpl *self,
					   guint8 *status,
					   GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	const guint8 cmd = STEELSERIES_FIZZ_PAIRED_STATUS_COMMAND;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_PAIRED_STATUS_COMMAND_OFFSET,
				    cmd,
				    error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "PairedStatus", data, sizeof(data));
	if (!fu_steelseries_fizz_gen1_cmd(self, data, sizeof(data), TRUE, error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "PairedStatus", data, sizeof(data));

	if (!fu_memread_uint8_safe(data,
				   sizeof(data),
				   STEELSERIES_FIZZ_PAIRED_STATUS_STATUS_OFFSET,
				   status,
				   error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_gen1_get_connection_status(FuSteelseriesFizzImpl *self,
					       guint8 *status,
					       GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	const guint8 cmd = STEELSERIES_FIZZ_CONNECTION_STATUS_COMMAND;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_CONNECTION_STATUS_COMMAND_OFFSET,
				    cmd,
				    error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "ConnectionStatus", data, sizeof(data));
	if (!fu_steelseries_fizz_gen1_cmd(self, data, sizeof(data), TRUE, error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "ConnectionStatus", data, sizeof(data));

	if (!fu_memread_uint8_safe(data,
				   sizeof(data),
				   STEELSERIES_FIZZ_CONNECTION_STATUS_STATUS_OFFSET,
				   status,
				   error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_gen1_get_battery_level(FuSteelseriesFizzImpl *self,
					   gboolean tunnel,
					   guint8 *level,
					   GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	guint8 cmd = STEELSERIES_FIZZ_BATTERY_LEVEL_COMMAND;
	guint8 tmp_level;

	if (tunnel)
		cmd |= STEELSERIES_FIZZ_COMMAND_TUNNEL_BIT;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_BATTERY_LEVEL_COMMAND_OFFSET,
				    cmd,
				    error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "BatteryLevel", data, sizeof(data));
	if (!fu_steelseries_fizz_gen1_cmd(self, data, sizeof(data), TRUE, error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "BatteryLevel", data, sizeof(data));

	if (!fu_memread_uint8_safe(data,
				   sizeof(data),
				   STEELSERIES_FIZZ_BATTERY_LEVEL_LEVEL_OFFSET,
				   &tmp_level,
				   error))
		return FALSE;

	/*
	 * CHARGING: Most significant bit. When bit is set to 1 it means battery is currently
	 * charging/plugged in
	 *
	 * LEVEL: 7 least significant bit value of the battery. Values are between 2-21, to get %
	 * you can do (LEVEL - 1) * 5
	 */
	*level = ((tmp_level & STEELSERIES_FIZZ_BATTERY_LEVEL_STATUS_BITS) - 1U) * 5U;

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
	iface->cmd = fu_steelseries_fizz_gen1_cmd;
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
	fu_steelseries_device_set_iface_idx_offset(FU_STEELSERIES_DEVICE(self), 0x03);
}
