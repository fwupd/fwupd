/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <errno.h>
#include <sys/io.h>

#include "fu-pci-device.h"

typedef struct {
	guint32 bus;
	guint32 dev;
	guint32 fun;
} FuPciDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuPciDevice, fu_pci_device, FU_TYPE_DEVICE)

#define PCI_CONFIG_ADDRESS 0x0CF8
#define PCI_CONFIG_DATA	   0x0CFC

#define GET_PRIVATE(o) (fu_pci_device_get_instance_private(o))

guint32
fu_pci_device_read_config(FuPciDevice *self, guint32 addr)
{
	FuPciDevicePrivate *priv = GET_PRIVATE(self);
	guint32 val = 0x80000000;

	/* we have to do this horrible port access as the PCI device is not
	 * visible to even the kernel as the vendor ID is set as 0xFFFF */
	val |= priv->bus << 16;
	val |= priv->dev << 11;
	val |= priv->fun << 8;
	val |= addr;

	/* we do this multiple times until we get the same result for every
	 * request as the port is shared between the kernel and all processes */
	for (guint cnt = 0; cnt < 0xff; cnt++) {
		guint32 results[0x20] = {0x0};
		gboolean consistent = TRUE;

		/* fill up array */
		for (guint i = 0; i < G_N_ELEMENTS(results); i++) {
			outl(val, PCI_CONFIG_ADDRESS);
			results[i] = inl(PCI_CONFIG_DATA);
		}

		/* check they are all the same */
		for (guint i = 0; i < G_N_ELEMENTS(results); i++) {
			if (results[0] != results[i]) {
				consistent = FALSE;
				break;
			}
		}

		/* success */
		if (consistent)
			return results[0];
	}

	/* failed */
	return G_MAXUINT32;
}

static gboolean
fu_pci_device_open(FuDevice *device, GError **error)
{
	/* this will fail if userspace is locked down */
	if (ioperm(PCI_CONFIG_ADDRESS, 64, 1) < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "failed to open port: %s",
			    strerror(errno));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pci_device_close(FuDevice *device, GError **error)
{
	/* this might fail if userspace is locked down */
	if (ioperm(PCI_CONFIG_ADDRESS, 64, 0) < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "failed to open port: %s",
			    strerror(errno));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_pci_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuPciDevice *self = FU_PCI_DEVICE(device);
	FuPciDevicePrivate *priv = GET_PRIVATE(self);
	fu_string_append_kx(str, idt, "Bus", priv->bus);
	fu_string_append_kx(str, idt, "Dev", priv->dev);
	fu_string_append_kx(str, idt, "Fun", priv->fun);
}

static gboolean
fu_pci_device_parse_bdf(FuPciDevice *self, const gchar *bdf, GError **error)
{
	FuPciDevicePrivate *priv = GET_PRIVATE(self);
	guint64 bus_tmp;
	guint64 dev_tmp;
	guint64 fun_tmp;
	g_auto(GStrv) split = g_strsplit_set(bdf, ":.", 0);

	/* parse the BDF */
	if (g_strv_length(split) != 3) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "%s invalid, expected '00:1f.5'",
			    bdf);
		return FALSE;
	}
	bus_tmp = g_ascii_strtoull(split[0], NULL, 16);
	dev_tmp = g_ascii_strtoull(split[1], NULL, 16);
	fun_tmp = g_ascii_strtoull(split[2], NULL, 16);
	if (bus_tmp > 0xff || dev_tmp > 0x1f || fun_tmp > 0x7) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "%s invalid, expected '00:1f.5'",
			    bdf);
		return FALSE;
	}

	/* success */
	priv->bus = bus_tmp;
	priv->dev = dev_tmp;
	priv->fun = fun_tmp;
	return TRUE;
}

static void
fu_pci_device_init(FuPciDevice *self)
{
	fu_device_set_physical_id(FU_DEVICE(self), "PCI");
}

static void
fu_pci_device_class_init(FuPciDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_pci_device_to_string;
	klass_device->open = fu_pci_device_open;
	klass_device->close = fu_pci_device_close;
}

FuDevice *
fu_pci_device_new(const gchar *bdf, GError **error)
{
	g_autoptr(FuPciDevice) self = FU_PCI_DEVICE(g_object_new(FU_TYPE_PCI_DEVICE, NULL));
	if (!fu_pci_device_parse_bdf(self, bdf, error))
		return NULL;
	return FU_DEVICE(g_steal_pointer(&self));
}
