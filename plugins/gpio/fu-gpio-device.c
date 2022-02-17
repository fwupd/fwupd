/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib/gstdio.h>
#include <linux/gpio.h>

#include "fu-gpio-device.h"

struct _FuGpioDevice {
	FuUdevDevice parent_instance;
	guint num_lines;
	gint fd; /* valid when the GPIO bit is assigned */
};

G_DEFINE_TYPE(FuGpioDevice, fu_gpio_device, FU_TYPE_UDEV_DEVICE)

static void
fu_gpio_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuGpioDevice *self = FU_GPIO_DEVICE(device);
	FU_DEVICE_CLASS(fu_gpio_device_parent_class)->to_string(device, idt, str);
	fu_common_string_append_ku(str, idt, "NumLines", self->num_lines);
	fu_common_string_append_kb(str, idt, "FdOpen", self->fd > 0);
}

static gboolean
fu_gpio_device_probe(FuDevice *device, GError **error)
{
	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_gpio_device_parent_class)->probe(device, error))
		return FALSE;

	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "gpio", error);
}

static gboolean
fu_gpio_device_setup(FuDevice *device, GError **error)
{
	FuGpioDevice *self = FU_GPIO_DEVICE(device);
	struct gpiochip_info info = {0x0};

	/* get info */
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  GPIO_GET_CHIPINFO_IOCTL,
				  (guint8 *)&info,
				  NULL,
				  error)) {
		g_prefix_error(error, "failed to get chipinfo: ");
		return FALSE;
	}

	/* sanity check */
	self->num_lines = info.lines;
	if (self->num_lines == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "0 lines is not supported");
		return FALSE;
	}

	/* label is optional, but name is always set */
	if (info.label[0] != '\0') {
		g_autofree gchar *logical_id = fu_common_strsafe(info.label, sizeof(info.label));
		fu_device_set_logical_id(device, logical_id);

		/* add instance ID */
		fu_device_add_instance_strsafe(device, "ID", logical_id);
		if (!fu_device_build_instance_id(device, error, "GPIO", "ID", NULL))
			return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_gpio_device_unassign(FuGpioDevice *self, GError **error)
{
	if (self->fd < 0)
		return TRUE;
	g_debug("unsetting %s", fu_device_get_logical_id(FU_DEVICE(self)));
	if (!g_close(self->fd, error))
		return FALSE;
	self->fd = -1;
	return TRUE;
}

static gboolean
fu_gpio_device_assign_full(FuGpioDevice *self, guint64 line, gboolean value, GError **error)
{
	const gchar consumer[] = "fwupd";
	struct gpio_v2_line_request req = {
	    .num_lines = 1,
	    req.offsets[0] = line,
	    .config.flags = GPIO_V2_LINE_FLAG_OUTPUT,
	    .config.num_attrs = 1,
	    .config.attrs[0].attr.values = value ? 0x1 : 0x0,
	    .config.attrs[0].mask = 0x1,
	};

	/* this is useful if we have contention with other tools */
	if (!fu_memcpy_safe((guint8 *)req.consumer,
			    sizeof(req.consumer),
			    0x0, /* dst */
			    (const guint8 *)consumer,
			    sizeof(consumer),
			    0x0, /* src */
			    sizeof(consumer),
			    error))
		return FALSE;

	/* slightly weird API, but roll with it */
	g_debug("setting %s:0x%02x → %i",
		fu_device_get_logical_id(FU_DEVICE(self)),
		(guint)line,
		value);
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  GPIO_V2_GET_LINE_IOCTL,
				  (guint8 *)&req,
				  NULL,
				  error)) {
		g_prefix_error(error, "failed to assign: ");
		return FALSE;
	}

	/* success */
	self->fd = req.fd;
	return TRUE;
}

gboolean
fu_gpio_device_assign(FuGpioDevice *self, const gchar *id, gboolean value, GError **error)
{
	guint64 line = G_MAXUINT64;

	/* sanity check */
	if (self->fd > 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "GPIO %s already in use",
			    id);
		return FALSE;
	}

	/* specified as a number, or look for @id as named pin */
	if (fu_common_strtoull_full(id, &line, 0, self->num_lines - 1, NULL)) {
		struct gpio_v2_line_info info = {.offset = line};
		if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
					  GPIO_V2_GET_LINEINFO_IOCTL,
					  (guint8 *)&info,
					  NULL,
					  error)) {
			g_prefix_error(error, "failed to get lineinfo: ");
			return FALSE;
		}
	} else {
		for (guint i = 0; i < self->num_lines; i++) {
			struct gpio_v2_line_info info = {.offset = i};
			g_autofree gchar *name = NULL;
			if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
						  GPIO_V2_GET_LINEINFO_IOCTL,
						  (guint8 *)&info,
						  NULL,
						  error)) {
				g_prefix_error(error, "failed to get lineinfo: ");
				return FALSE;
			}
			name = fu_common_strsafe(info.name, sizeof(info.name));
			if (g_strcmp0(name, id) == 0) {
				line = i;
				break;
			}
		}
	}
	if (line == G_MAXUINT64) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "failed to find %s", id);
		return FALSE;
	}
	return fu_gpio_device_assign_full(self, line, value, error);
}

static void
fu_gpio_device_init(FuGpioDevice *self)
{
}

static void
fu_gpio_device_finalize(GObject *object)
{
	FuGpioDevice *self = FU_GPIO_DEVICE(object);
	if (self->fd > 0)
		g_close(self->fd, NULL);
	G_OBJECT_CLASS(fu_gpio_device_parent_class)->finalize(object);
}

static void
fu_gpio_device_class_init(FuGpioDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_gpio_device_finalize;
	klass_device->to_string = fu_gpio_device_to_string;
	klass_device->setup = fu_gpio_device_setup;
	klass_device->probe = fu_gpio_device_probe;
}
