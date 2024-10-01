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
	guint8 revision;
	guint32 class;
	guint16 subsystem_vid;
	guint16 subsystem_pid;
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
	fwupd_codec_string_append_hex(str, idt, "Revision", priv->revision);
	fwupd_codec_string_append_hex(str, idt, "Class", priv->class);
	fwupd_codec_string_append_hex(str, idt, "SubsystemVendor", priv->subsystem_vid);
	fwupd_codec_string_append_hex(str, idt, "SubsystemModel", priv->subsystem_pid);
}

/**
 * fu_pci_device_set_subsystem_vid:
 * @self: a #FuPciDevice
 * @subsystem_vid: integer
 *
 * Sets the device subsystem vendor code.
 *
 * Since: 2.0.0
 **/
void
fu_pci_device_set_subsystem_vid(FuPciDevice *self, guint16 subsystem_vid)
{
	FuPciDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PCI_DEVICE(self));
	priv->subsystem_vid = subsystem_vid;
}

/**
 * fu_pci_device_get_subsystem_vid:
 * @self: a #FuPciDevice
 *
 * Gets the device subsystem vendor code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 2.0.0
 **/
guint16
fu_pci_device_get_subsystem_vid(FuPciDevice *self)
{
	FuPciDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PCI_DEVICE(self), 0x0000);
	return priv->subsystem_vid;
}

/**
 * fu_pci_device_set_subsystem_pid:
 * @self: a #FuPciDevice
 * @subsystem_pid: integer
 *
 * Sets the device subsystem model code.
 *
 * Since: 2.0.0
 **/
void
fu_pci_device_set_subsystem_pid(FuPciDevice *self, guint16 subsystem_pid)
{
	FuPciDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PCI_DEVICE(self));
	priv->subsystem_pid = subsystem_pid;
}

/**
 * fu_pci_device_get_subsystem_pid:
 * @self: a #FuPciDevice
 *
 * Gets the device subsystem model code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.5.0
 **/
guint16
fu_pci_device_get_subsystem_pid(FuPciDevice *self)
{
	FuPciDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PCI_DEVICE(self), 0x0000);
	return priv->subsystem_pid;
}

/**
 * fu_pci_device_set_revision:
 * @self: a #FuPciDevice
 * @revision: integer
 *
 * Sets the device revision.
 *
 * Since: 2.0.0
 **/
void
fu_pci_device_set_revision(FuPciDevice *self, guint8 revision)
{
	FuPciDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PCI_DEVICE(self));
	priv->revision = revision;
}

/**
 * fu_pci_device_get_revision:
 * @self: a #FuPciDevice
 *
 * Gets the device revision.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint8
fu_pci_device_get_revision(FuPciDevice *self)
{
	FuPciDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PCI_DEVICE(self), 0x00);
	return priv->revision;
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
	if (priv->subsystem_vid == 0x0) {
		fu_pci_device_set_subsystem_vid(uself, fu_pci_device_get_subsystem_vid(udonor));
	}
	if (priv->subsystem_pid == 0x0) {
		fu_pci_device_set_subsystem_pid(uself, fu_pci_device_get_subsystem_pid(udonor));
	}
	if (priv->revision == 0x0)
		fu_pci_device_set_revision(uself, fu_pci_device_get_revision(udonor));
}

static gboolean
fu_pci_device_probe(FuDevice *device, GError **error)
{
	FuPciDevice *self = FU_PCI_DEVICE(device);
	FuPciDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *attr_class = NULL;
	g_autofree gchar *attr_revision = NULL;
	g_autofree gchar *attr_subsystem_device = NULL;
	g_autofree gchar *attr_subsystem_vendor = NULL;
	g_autofree gchar *physical_id = NULL;
	g_autofree gchar *prop_slot = NULL;
	g_autofree gchar *subsystem = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_pci_device_parent_class)->probe(device, error))
		return FALSE;

	/* needed for instance IDs further down */
	subsystem = g_ascii_strup(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(self)), -1);

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

	/* set the version if the revision has been set */
	attr_revision = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
						  "revision",
						  FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						  NULL);
	if (attr_revision != NULL) {
		guint64 tmp64 = 0;
		if (!fu_strtoull(attr_revision, &tmp64, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		priv->revision = (guint8)tmp64;
	}
	if (fu_device_get_version(device) == NULL &&
	    fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		if (priv->revision != 0x00 && priv->revision != 0xFF) {
			g_autofree gchar *version = g_strdup_printf("%02x", priv->revision);
			fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_set_version(device, version);
		}
	}
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV) &&
	    priv->revision != 0xFF) {
		fu_device_add_instance_u8(device, "REV", priv->revision);
		if (fu_device_has_private_flag(device,
					       FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV)) {
			fu_device_build_instance_id_full(device,
							 FU_DEVICE_INSTANCE_FLAG_GENERIC |
							     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
							     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
							 NULL,
							 subsystem,
							 "VEN",
							 "DEV",
							 "REV",
							 NULL);
		}
	}

	/* subsystem IDs */
	attr_subsystem_vendor = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
							  "subsystem_vendor",
							  FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
							  NULL);
	if (attr_subsystem_vendor != NULL) {
		guint64 tmp64 = 0;
		if (!fu_strtoull(attr_subsystem_vendor,
				 &tmp64,
				 0,
				 G_MAXUINT16,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
		priv->subsystem_vid = (guint16)tmp64;
	}
	attr_subsystem_device = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
							  "subsystem_device",
							  FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
							  NULL);
	if (attr_subsystem_device != NULL) {
		guint64 tmp64 = 0;
		if (!fu_strtoull(attr_subsystem_device,
				 &tmp64,
				 0,
				 G_MAXUINT16,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
		priv->subsystem_pid = (guint16)tmp64;
	}
	if (priv->subsystem_vid != 0x0000 || priv->subsystem_pid != 0x0000) {
		g_autofree gchar *subsys =
		    g_strdup_printf("%04X%04X", priv->subsystem_vid, priv->subsystem_pid);
		fu_device_add_instance_str(device, "SUBSYS", subsys);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem,
						 "VEN",
						 "DEV",
						 "SUBSYS",
						 NULL);
		if (fu_device_has_private_flag(device,
					       FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV)) {
			fu_device_build_instance_id_full(device,
							 FU_DEVICE_INSTANCE_FLAG_GENERIC |
							     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
							     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
							 NULL,
							 subsystem,
							 "VEN",
							 "DEV",
							 "SUBSYS",
							 "REV",
							 NULL);
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
