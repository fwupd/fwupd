/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-steelseries-fizz-gen2.h"
#include "fu-steelseries-fizz-impl.h"

#define STEELSERIES_FIZZ_VERSION_COMMAND	0x10U
#define STEELSERIES_FIZZ_VERSION_COMMAND_OFFSET 0x00U
#define STEELSERIES_FIZZ_VERSION_MODE_OFFSET	0x01U

#define STEELSERIES_FIZZ_VERSION_SIZE		 0x0CU
#define STEELSERIES_FIZZ_VERSION_RECEIVER_OFFSET 0x01U
#define STEELSERIES_FIZZ_VERSION_DEVICE_OFFSET	 0x19U

#define STEELSERIES_FIZZ_COMMAND_TUNNEL_BIT 1U << 6

#define STEELSERIES_FIZZ_GEN2_FILESYSTEM_RECEIVER 0x01U
#define STEELSERIES_FIZZ_GEN2_FILESYSTEM_HEADSET  0x01U
#define STEELSERIES_FIZZ_GEN2_APP_ID		  0x01U

#define STEELSERIES_FIZZ_CONNECTION_STATUS_COMMAND	  0xB0U
#define STEELSERIES_FIZZ_CONNECTION_STATUS_COMMAND_OFFSET 0x00U
#define STEELSERIES_FIZZ_CONNECTION_STATUS_STATUS_OFFSET  0x01U

#define STEELSERIES_FIZZ_GEN2_NOT_PAIRED 0x00U;
#define STEELSERIES_FIZZ_GEN2_PAIRED	 0x01U;

typedef enum {
	STEELSERIES_FIZZ_GEN2_CONNECTION_UNEXPECTED = 0,
	STEELSERIES_FIZZ_GEN2_CONNECTION_PAIRING,
	STEELSERIES_FIZZ_GEN2_CONNECTION_DISCONNECTED,
	STEELSERIES_FIZZ_GEN2_CONNECTION_CONNECTED,
} ConnectionStatus;

#define STEELSERIES_FIZZ_BATTERY_LEVEL_COMMAND_OFFSET 0x00U
#define STEELSERIES_FIZZ_BATTERY_LEVEL_LEVEL_OFFSET   0x03U

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
fu_steelseries_fizz_gen2_cmd(FuSteelseriesFizzImpl *self,
			     guint8 *data,
			     gsize datasz,
			     gboolean answer,
			     GError **error)
{
	return fu_steelseries_device_cmd(FU_STEELSERIES_DEVICE(self), data, datasz, answer, error);
}

static gchar *
fu_steelseries_fizz_gen2_get_version(FuSteelseriesFizzImpl *self, gboolean tunnel, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	guint8 cmd = STEELSERIES_FIZZ_VERSION_COMMAND;
	gsize offset = STEELSERIES_FIZZ_VERSION_RECEIVER_OFFSET;
	guint64 version[3] = {0};
	g_autofree gchar *version_raw = NULL;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_VERSION_COMMAND_OFFSET,
				    cmd,
				    error))
		return NULL;

	fu_dump_raw(G_LOG_DOMAIN, "Version", data, sizeof(data));
	if (!fu_steelseries_device_cmd(FU_STEELSERIES_DEVICE(self),
				       data,
				       sizeof(data),
				       TRUE,
				       error))
		return NULL;
	fu_dump_raw(G_LOG_DOMAIN, "Version", data, sizeof(data));

	if (tunnel)
		offset = STEELSERIES_FIZZ_VERSION_DEVICE_OFFSET;

	version_raw =
	    fu_memstrsafe(data, sizeof(data), offset, STEELSERIES_FIZZ_VERSION_SIZE, error);
	if (version_raw == NULL)
		return NULL;

	/* very interesting version format */
	if (version_raw[1] == 0x2E && version_raw[4] == 0x2E && version_raw[8] == 0x2E) {
		/* format triple */
		version[0] = ((version_raw[2] - 0x30) << 4) + (version_raw[3] - 0x30);
		version[1] = ((version_raw[6] - 0x30) << 4) + (version_raw[7] - 0x30);
		version[2] = ((version_raw[9] - 0x30) << 4) + (version_raw[10] - 0x30);
		/* FIXME: check with vendor */
		// version[0] = (version_raw[2] - 0x30) << 4 + (version_raw[3] - 0x30);
		// /* no minor version */
		// version[2] = (version_raw[6] - 0x30) << 4 + (version_raw[7] - 0x30);
		/* ignoring test subversion */
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
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	guint8 cmd = STEELSERIES_FIZZ_CONNECTION_STATUS_COMMAND;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_CONNECTION_STATUS_COMMAND_OFFSET,
				    cmd,
				    error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "BatteryLevel", data, sizeof(data));
	if (!fu_steelseries_fizz_gen2_cmd(self, data, sizeof(data), TRUE, error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "BatteryLevel", data, sizeof(data));

	if (!fu_memread_uint8_safe(data,
				   sizeof(data),
				   STEELSERIES_FIZZ_BATTERY_LEVEL_LEVEL_OFFSET,
				   level,
				   error))
		return FALSE;

	/* success */
	return TRUE;
}

static guint8
fu_steelseries_fizz_gen2_get_fs_id(FuSteelseriesFizzImpl *self,
				   gboolean is_receiver,
				   GError **error)
{
	if (is_receiver)
		return STEELSERIES_FIZZ_GEN2_FILESYSTEM_RECEIVER;

	return STEELSERIES_FIZZ_GEN2_FILESYSTEM_HEADSET;
}

static guint8
fu_steelseries_fizz_gen2_get_file_id(FuSteelseriesFizzImpl *self,
				     gboolean is_receiver,
				     GError **error)
{
	return STEELSERIES_FIZZ_GEN2_APP_ID;
}

static gboolean
fu_steelseries_fizz_gen2_get_paired_status(FuSteelseriesFizzImpl *self,
					   guint8 *status,
					   GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	guint8 tmp_status = STEELSERIES_FIZZ_CONNECTION_STATUS_NOT_CONNECTED;
	guint8 cmd = STEELSERIES_FIZZ_CONNECTION_STATUS_COMMAND;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_CONNECTION_STATUS_COMMAND_OFFSET,
				    cmd,
				    error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "ConnectionStatus", data, sizeof(data));
	if (!fu_steelseries_fizz_gen2_cmd(self, data, sizeof(data), TRUE, error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "ConnectionStatus", data, sizeof(data));

	if (!fu_memread_uint8_safe(data,
				   sizeof(data),
				   STEELSERIES_FIZZ_CONNECTION_STATUS_STATUS_OFFSET,
				   &tmp_status,
				   error))
		return FALSE;

	/* treat CONNECTED and DISCONNECTED as paired */
	switch (tmp_status) {
	case STEELSERIES_FIZZ_GEN2_CONNECTION_CONNECTED:
	case STEELSERIES_FIZZ_GEN2_CONNECTION_DISCONNECTED:
		*status = STEELSERIES_FIZZ_GEN2_PAIRED;
		break;
	default:
		*status = STEELSERIES_FIZZ_GEN2_NOT_PAIRED;
	}
	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_gen2_get_connection_status(FuSteelseriesFizzImpl *self,
					       guint8 *status,
					       GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};
	guint8 tmp_status = STEELSERIES_FIZZ_CONNECTION_STATUS_NOT_CONNECTED;
	guint8 cmd = STEELSERIES_FIZZ_CONNECTION_STATUS_COMMAND;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_FIZZ_CONNECTION_STATUS_COMMAND_OFFSET,
				    cmd,
				    error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "ConnectionStatus", data, sizeof(data));
	if (!fu_steelseries_fizz_gen2_cmd(self, data, sizeof(data), TRUE, error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "ConnectionStatus", data, sizeof(data));

	if (!fu_memread_uint8_safe(data,
				   sizeof(data),
				   STEELSERIES_FIZZ_CONNECTION_STATUS_STATUS_OFFSET,
				   &tmp_status,
				   error))
		return FALSE;

	if (tmp_status == STEELSERIES_FIZZ_GEN2_CONNECTION_CONNECTED)
		*status = STEELSERIES_FIZZ_CONNECTION_STATUS_CONNECTED;
	else
		*status = STEELSERIES_FIZZ_CONNECTION_STATUS_NOT_CONNECTED;

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_gen2_probe(FuDevice *device, GError **error)
{
	/* in bootloader mode */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		fu_steelseries_device_set_iface_idx_offset(FU_STEELSERIES_DEVICE(device), 0x00);

	/* FuUsbDevice->setup */
	return FU_DEVICE_CLASS(fu_steelseries_fizz_gen2_parent_class)->probe(device, error);
}

static void
fu_steelseries_fizz_gen2_impl_iface_init(FuSteelseriesFizzImplInterface *iface)
{
	iface->cmd = fu_steelseries_fizz_gen2_cmd;
	iface->get_version = fu_steelseries_fizz_gen2_get_version;
	iface->get_fs_id = fu_steelseries_fizz_gen2_get_fs_id;
	iface->get_file_id = fu_steelseries_fizz_gen2_get_file_id;
	iface->get_paired_status = fu_steelseries_fizz_gen2_get_paired_status;
	iface->get_connection_status = fu_steelseries_fizz_gen2_get_connection_status;
	iface->get_battery_level = fu_steelseries_fizz_gen2_get_battery_level;
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
