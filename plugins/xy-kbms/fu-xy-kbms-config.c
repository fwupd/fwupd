/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-xy-kbms-common.h"
#include "fu-xy-kbms-config.h"
#include "fu-xy-kbms-device.h"
#include "fu-xy-kbms-pinmap.h"
#include "fu-xy-kbms-struct.h"

#define FU_XY_KBMS_CONFIG_RESET_DELAY_MS 3000

struct _FuXyKbmsConfig {
	FuDevice parent_instance;
	guint16 auto_off;
	guint16 tp_auto_off;
	guint8 scroll_speed;
	guint8 move_speed;
	FuXyKbmsFlagsA flags_a;
	FuXyKbmsFlagsB flags_b;
	gchar *bt_name;
	FuXyKbmsRfMode rf_mode;
	guint8 rf_channel;
	guint32 crc;
};

G_DEFINE_TYPE(FuXyKbmsConfig, fu_xy_kbms_config, FU_TYPE_DEVICE)

static void
fu_xy_kbms_config_to_string(FuDevice *device, guint idt, GString *str)
{
	FuXyKbmsConfig *self = FU_XY_KBMS_CONFIG(device);
	g_autofree gchar *flags_a = fu_xy_kbms_flags_a_to_string(self->flags_a);
	g_autofree gchar *flags_b = fu_xy_kbms_flags_b_to_string(self->flags_b);
	fwupd_codec_string_append_int(str, idt, "AutoOff", self->auto_off);
	fwupd_codec_string_append_int(str, idt, "TpAutoOff", self->tp_auto_off);
	fwupd_codec_string_append_int(str, idt, "ScrollSpeed", self->scroll_speed);
	fwupd_codec_string_append_int(str, idt, "MoveSpeed", self->move_speed);
	fwupd_codec_string_append(str, idt, "FlagsA", flags_a);
	fwupd_codec_string_append(str, idt, "FlagsB", flags_b);
	fwupd_codec_string_append(str, idt, "BtName", self->bt_name);
	fwupd_codec_string_append(str, idt, "RfMode", fu_xy_kbms_rf_mode_to_string(self->rf_mode));
	/* the vendor tool shows channels 1-4 */
	fwupd_codec_string_append_int(str, idt, "RfChannel", self->rf_channel + 1);
	fwupd_codec_string_append_hex(str, idt, "Crc", self->crc);
}

/* copy the values rather than keeping the struct */
static gboolean
fu_xy_kbms_config_parse_config(FuXyKbmsConfig *self, GByteArray *buf, GError **error)
{
	g_autoptr(FuStructXyKbmsConfig) st = NULL;

	st = fu_struct_xy_kbms_config_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	self->auto_off = fu_struct_xy_kbms_config_get_auto_off(st);
	self->tp_auto_off = fu_struct_xy_kbms_config_get_tp_auto_off(st);
	self->scroll_speed = fu_struct_xy_kbms_config_get_scroll_speed(st);
	self->move_speed = fu_struct_xy_kbms_config_get_move_speed(st);
	self->flags_a = fu_struct_xy_kbms_config_get_flags_a(st);
	self->flags_b = fu_struct_xy_kbms_config_get_flags_b(st);
	g_free(self->bt_name);
	self->bt_name = fu_struct_xy_kbms_config_get_bt_name(st);
	self->rf_mode = fu_struct_xy_kbms_config_get_rf_mode(st);
	self->rf_channel = fu_struct_xy_kbms_config_get_rf_channel(st);
	self->crc = fu_struct_xy_kbms_config_get_crc(st);
	return TRUE;
}

static gboolean
fu_xy_kbms_config_probe(FuDevice *device, GError **error)
{
	FuDevice *proxy;

	/* use the same VEN and DEV as the parent */
	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	fu_device_incorporate(device,
			      proxy,
			      FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
				  FU_DEVICE_INCORPORATE_FLAG_INSTANCE_KEYS);
	fu_device_add_instance_str(device, "PART", "PINMAP");
	return fu_device_build_instance_id(device, error, "HIDRAW", "VEN", "DEV", "PART", NULL);
}

static gboolean
fu_xy_kbms_config_setup(FuDevice *device, GError **error)
{
	FuXyKbmsConfig *self = FU_XY_KBMS_CONFIG(device);
	FuDevice *proxy;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GByteArray) buf = NULL;

	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	buf = fu_xy_kbms_device_read_config(FU_XY_KBMS_DEVICE(proxy), progress, error);
	if (buf == NULL)
		return FALSE;
	return fu_xy_kbms_config_parse_config(self, buf, error);
}

static FuFirmware *
fu_xy_kbms_config_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy;
	gsize bufsz = 0;
	const guint8 *buf;
	g_autoptr(FuFirmware) firmware = fu_xy_kbms_pinmap_new();
	g_autoptr(FuStructXyKbmsConfig) st = NULL;
	g_autoptr(GByteArray) buf_cfg = NULL;
	g_autoptr(GBytes) blob = NULL;

	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return NULL;
	buf_cfg = fu_xy_kbms_device_read_config(FU_XY_KBMS_DEVICE(proxy), progress, error);
	if (buf_cfg == NULL)
		return NULL;
	st = fu_struct_xy_kbms_config_parse(buf_cfg->data, buf_cfg->len, 0x0, error);
	if (st == NULL)
		return NULL;
	buf = fu_struct_xy_kbms_config_get_pinmap(st, &bufsz);
	blob = g_bytes_new(buf, bufsz);
	fu_firmware_set_bytes(firmware, blob);
	return g_steal_pointer(&firmware);
}

static gboolean
fu_xy_kbms_config_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuDevice *proxy;
	g_autoptr(GBytes) blob_pinmap = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GByteArray) buf_verify = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 25, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 25, NULL);

	proxy = fu_device_get_proxy(device, error);
	if (proxy == NULL)
		return FALSE;
	blob_pinmap = fu_firmware_get_bytes(firmware, error);
	if (blob_pinmap == NULL)
		return FALSE;

	/* keep the existing settings, e.g. the Bluetooth name */
	buf = fu_xy_kbms_device_read_config(FU_XY_KBMS_DEVICE(proxy),
					    fu_progress_get_child(progress),
					    error);
	if (buf == NULL)
		return FALSE;
	fu_progress_step_done(progress);

	/* replace the pinmap, and fix up the CRC */
	if (!fu_memcpy_safe(buf->data,
			    buf->len,
			    FU_STRUCT_XY_KBMS_CONFIG_OFFSET_PINMAP,
			    g_bytes_get_data(blob_pinmap, NULL),
			    g_bytes_get_size(blob_pinmap),
			    0x0,
			    FU_STRUCT_XY_KBMS_CONFIG_SIZE_PINMAP,
			    error))
		return FALSE;
	if (!fu_xy_kbms_config_update_crc(buf, error))
		return FALSE;
	if (!fu_xy_kbms_device_write_config(FU_XY_KBMS_DEVICE(proxy),
					    buf,
					    fu_progress_get_child(progress),
					    error))
		return FALSE;
	fu_progress_step_done(progress);

	/* the device resets itself to load the new config */
	fu_device_sleep(device, FU_XY_KBMS_CONFIG_RESET_DELAY_MS);

	/* the device does not check the CRC itself, and the CRC only covers 0x80 bytes */
	buf_verify = fu_xy_kbms_device_read_config(FU_XY_KBMS_DEVICE(proxy),
						   fu_progress_get_child(progress),
						   error);
	if (buf_verify == NULL)
		return FALSE;
	if (!fu_memcmp_safe(buf_verify->data,
			    buf_verify->len,
			    0x0,
			    buf->data,
			    buf->len,
			    0x0,
			    buf->len,
			    error)) {
		g_prefix_error_literal(error, "failed to verify: ");
		return FALSE;
	}
	if (!fu_xy_kbms_config_parse_config(FU_XY_KBMS_CONFIG(device), buf_verify, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_xy_kbms_config_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "reload");
}

static void
fu_xy_kbms_config_init(FuXyKbmsConfig *self)
{
	fu_device_set_logical_id(FU_DEVICE(self), "config");
	fu_device_set_name(FU_DEVICE(self), "Pinmap");
	fu_device_add_protocol(FU_DEVICE(self), "com.xy.kbms.config");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_KEYBOARD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_PROGRAMMER_TARGET);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_XY_KBMS_DEVICE);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_XY_KBMS_PINMAP);
}

static void
fu_xy_kbms_config_finalize(GObject *object)
{
	FuXyKbmsConfig *self = FU_XY_KBMS_CONFIG(object);
	g_free(self->bt_name);
	G_OBJECT_CLASS(fu_xy_kbms_config_parent_class)->finalize(object);
}

static void
fu_xy_kbms_config_class_init(FuXyKbmsConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_xy_kbms_config_finalize;
	device_class->to_string = fu_xy_kbms_config_to_string;
	device_class->probe = fu_xy_kbms_config_probe;
	device_class->setup = fu_xy_kbms_config_setup;
	device_class->write_firmware = fu_xy_kbms_config_write_firmware;
	device_class->read_firmware = fu_xy_kbms_config_read_firmware;
	device_class->set_progress = fu_xy_kbms_config_set_progress;
}

FuXyKbmsConfig *
fu_xy_kbms_config_new(FuDevice *proxy)
{
	return g_object_new(FU_TYPE_XY_KBMS_CONFIG, "proxy", proxy, NULL);
}
