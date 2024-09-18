/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuPciDevice"

#include "config.h"

#include "fu-pci-device.h"
#include "fu-string.h"

/**
 * FuPciDevice
 *
 * See also: #FuUdevDevice
 */

typedef struct {
	guint32 class;
} FuPciDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuPciDevice, fu_pci_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_pci_device_get_instance_private(o))

typedef enum {
	FU_PCI_DEVICE_BASE_CLS_OLD,
	FU_PCI_DEVICE_BASE_CLS_MASS_STORAGE,
	FU_PCI_DEVICE_BASE_CLS_NETWORK,
	FU_PCI_DEVICE_BASE_CLS_DISPLAY,
	FU_PCI_DEVICE_BASE_CLS_MULTIMEDIA,
	FU_PCI_DEVICE_BASE_CLS_MEMORY,
	FU_PCI_DEVICE_BASE_CLS_BRIDGE,
	FU_PCI_DEVICE_BASE_CLS_SIMPLE_COMMUNICATION,
	FU_PCI_DEVICE_BASE_CLS_BASE,
	FU_PCI_DEVICE_BASE_CLS_INPUT,
	FU_PCI_DEVICE_BASE_CLS_DOCKING,
	FU_PCI_DEVICE_BASE_CLS_PROCESSORS,
	FU_PCI_DEVICE_BASE_CLS_SERIAL_BUS,
	FU_PCI_DEVICE_BASE_CLS_WIRELESS,
	FU_PCI_DEVICE_BASE_CLS_INTELLIGENT_IO,
	FU_PCI_DEVICE_BASE_CLS_SATELLITE,
	FU_PCI_DEVICE_BASE_CLS_ENCRYPTION,
	FU_PCI_DEVICE_BASE_CLS_SIGNAL_PROCESSING,
	FU_PCI_DEVICE_BASE_CLS_ACCELERATOR,
	FU_PCI_DEVICE_BASE_CLS_NON_ESSENTIAL,
	FU_PCI_DEVICE_BASE_CLS_UNDEFINED = 0xff
} FuPciBaseCls;

static void
fu_pci_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuPciDevice *self = FU_PCI_DEVICE(device);
	FuPciDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append_hex(str, idt, "Class", priv->class);
}

static void
fu_pci_device_to_incorporate(FuDevice *self, FuDevice *donor)
{
	FuPciDevice *uself = FU_PCI_DEVICE(self);
	FuPciDevice *udonor = FU_PCI_DEVICE(donor);
	FuPciDevicePrivate *priv = GET_PRIVATE(uself);
	FuPciDevicePrivate *priv_donor = GET_PRIVATE(udonor);

	g_return_if_fail(FU_IS_PCI_DEVICE(self));
	g_return_if_fail(FU_IS_PCI_DEVICE(donor));

	if (priv->class == 0x0)
		priv->class = priv_donor->class;
}

static gboolean
fu_pci_device_probe(FuDevice *device, GError **error)
{
	FuPciDevice *self = FU_PCI_DEVICE(device);
	FuPciDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *attr_class = NULL;
	g_autofree gchar *physical_id = NULL;
	g_autofree gchar *prop_slot = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_pci_device_parent_class)->probe(device, error))
		return FALSE;

	/* PCI class code */
	attr_class = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
					       "class",
					       FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					       NULL);
	if (attr_class != NULL) {
		guint64 class_u64 = 0;
		g_autoptr(GError) error_local = NULL;
		if (!fu_strtoull(attr_class,
				 &class_u64,
				 0,
				 G_MAXUINT32,
				 FU_INTEGER_BASE_AUTO,
				 &error_local)) {
			g_warning("reading class for %s was invalid: %s",
				  attr_class,
				  error_local->message);
		} else {
			priv->class = (guint32)class_u64;
		}
	}

	/* if the device is a GPU try to fetch it from vbios_version */
	if ((priv->class >> 16) == FU_PCI_DEVICE_BASE_CLS_DISPLAY &&
	    fu_device_get_version(device) == NULL) {
		g_autofree gchar *version = NULL;

		version = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
						    "vbios_version",
						    FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						    NULL);
		if (version != NULL) {
			fu_device_set_version(device, version);
			fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_add_icon(FU_DEVICE(self), "video-display");
		}
	}

	/* physical slot */
	prop_slot = fu_udev_device_read_property(FU_UDEV_DEVICE(self), "PCI_SLOT_NAME", error);
	if (prop_slot == NULL)
		return FALSE;
	physical_id = g_strdup_printf("PCI_SLOT_NAME=%s", prop_slot);
	fu_device_set_physical_id(device, physical_id);

	/* success */
	return TRUE;
}

static void
fu_pci_device_init(FuPciDevice *self)
{
}

static void
fu_pci_device_class_init(FuPciDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_pci_device_to_string;
	device_class->probe = fu_pci_device_probe;
	device_class->incorporate = fu_pci_device_to_incorporate;
}
