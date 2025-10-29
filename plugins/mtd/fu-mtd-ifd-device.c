/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mtd-ifd-device.h"

struct _FuMtdIfdDevice {
	FuDevice parent_instance;
	FuIfdImage *img;
};

G_DEFINE_TYPE(FuMtdIfdDevice, fu_mtd_ifd_device, FU_TYPE_DEVICE)

static void
fu_mtd_ifd_device_set_bios_fallback_version(FuDevice *device)
{
	FuContext *ctx = fu_device_get_context(device);
	const gchar *version;
	const gchar *version_major;
	const gchar *version_minor;

	/* try SMBIOS BIOS version first */
	version = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VERSION);
	if (version != NULL) {
		/* some Lenovo hardware requires a specific prefix for the EC,
		 * so strip it before we use ensure-semver */
		if (strlen(version) > 9 && g_str_has_prefix(version, "CBET"))
			version += 9;
		fu_device_set_version(device, version);
		if (fu_device_get_version(device) != NULL)
			return;
	}

	/* try major/minor components */
	version_major = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE);
	version_minor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_MINOR_RELEASE);
	if (version_major != NULL && version_minor != NULL) {
		g_autofree gchar *tmp = g_strdup_printf("%s.%s.0", version_major, version_minor);
		fu_device_set_version(device, tmp);
	}
}

static void
fu_mtd_ifd_device_add_security_attr_desc(FuMtdIfdDevice *self, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	FuIfdAccess ifd_access_global = FALSE;
	FuIfdRegion regions[] = {FU_IFD_REGION_BIOS, FU_IFD_REGION_ME, FU_IFD_REGION_EC};

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attrs_append(attrs, attr);

	/* check each */
	for (guint i = 0; i < G_N_ELEMENTS(regions); i++) {
		FuIfdAccess ifd_access = fu_ifd_image_get_access(self->img, regions[i]);
		g_autofree gchar *ifd_accessstr = fu_ifd_access_to_string(ifd_access);
		fwupd_security_attr_add_metadata(attr,
						 fu_ifd_region_to_string(regions[i]),
						 ifd_accessstr);
		ifd_access_global |= ifd_access;
	}
	if (ifd_access_global & FU_IFD_ACCESS_WRITE) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_mtd_ifd_device_add_security_attrs(FuDevice *device, FuSecurityAttrs *attrs)
{
	FuMtdIfdDevice *self = FU_MTD_IFD_DEVICE(device);

	if (self->img == NULL)
		return;
	if (fu_firmware_get_idx(FU_FIRMWARE(self->img)) == FU_IFD_REGION_DESC)
		fu_mtd_ifd_device_add_security_attr_desc(self, attrs);
}

static const gchar *
fu_mtd_ifd_device_region_to_name(FuIfdRegion region)
{
	if (region == FU_IFD_REGION_DESC)
		return "IFD descriptor region";
	if (region == FU_IFD_REGION_BIOS)
		return "BIOS";
	if (region == FU_IFD_REGION_ME)
		return "Intel Management Engine";
	if (region == FU_IFD_REGION_GBE)
		return "Gigabit Ethernet";
	if (region == FU_IFD_REGION_PLATFORM)
		return "Platform firmware";
	if (region == FU_IFD_REGION_DEVEXP)
		return "Device Firmware";
	if (region == FU_IFD_REGION_BIOS2)
		return "BIOS Backup";
	if (region == FU_IFD_REGION_EC)
		return "Embedded Controller";
	if (region == FU_IFD_REGION_IE)
		return "Innovation Engine";
	if (region == FU_IFD_REGION_10GBE)
		return "10 Gigabit Ethernet";
	return NULL;
}

static gboolean
fu_mtd_ifd_device_probe(FuDevice *device, GError **error)
{
	FuMtdIfdDevice *self = FU_MTD_IFD_DEVICE(device);

	if (self->img != NULL) {
		FuIfdRegion region = fu_firmware_get_idx(FU_FIRMWARE(self->img));
		g_autofree gchar *name = g_strdup(fu_mtd_ifd_device_region_to_name(region));
		g_autofree gchar *region_str = g_strdup(fu_ifd_region_to_string(region));

		/* fallback to including the index */
		if (name == NULL)
			name = g_strdup_printf("Region %u", region);
		if (region_str == NULL)
			region_str = g_strdup_printf("%u", region);

		/* always valid */
		fu_device_set_name(device, name);
		fu_device_set_logical_id(device, region_str);
		fu_device_add_instance_str(device, "REGION", region_str);

		/* Region is updatable via the parent MTD device if the BIOS master
		 * (host CPU) has write permission for this region. */
		{
			/* Note: FU_IFD_REGION_BIOS here refers to the BIOS master access bits,
			 * not the BIOS flash region. */
			FuIfdAccess acc = fu_ifd_image_get_access(self->img, FU_IFD_REGION_BIOS);
			if ((acc & FU_IFD_ACCESS_WRITE) != 0) {
				fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
				fu_device_add_protocol(device, "org.infradead.mtd");
			}
		}

	}
	if (!fu_device_build_instance_id(device, error, "IFD", "REGION", NULL))
		return FALSE;

	/* ensure a usable version for comparison if metadata is absent */
	if (fu_device_get_version(device) == NULL) {
		FuDevice *proxy = fu_device_get_proxy_with_fallback(device);

		/* Prefer parent (MTD) version first */
		if (proxy != NULL) {
			const gchar *ver_parent = fu_device_get_version(proxy);
			if (ver_parent != NULL) {
				fu_device_set_version_format(device, fu_device_get_version_format(proxy));
				fu_device_set_version(device, ver_parent);
			} else {
				/* if parent only has a raw numeric version, convert to a
				 * string for the child */
				guint64 ver_raw = fu_device_get_version_raw(proxy);
				if (ver_raw != G_MAXUINT64) {
					g_autofree gchar *ver_str = g_strdup_printf("%" G_GUINT64_FORMAT, ver_raw);
					fu_device_set_version_format(device, fu_device_get_version_format(proxy));
					if (fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN)
						fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_NUMBER);
					fu_device_set_version(device, ver_str);
				}
			}
		}

		/* Fall back to SMBIOS DMI-derived version if nothing else available */
		if (fu_device_get_version(device) == NULL)
			fu_mtd_ifd_device_set_bios_fallback_version(device);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_ifd_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuMtdIfdDevice *self = FU_MTD_IFD_DEVICE(device);
	FuIfdRegion region;
	FuDevice *proxy;
	gsize addr;
	g_autoptr(FuFirmware) img_to_write = NULL;
	g_autoptr(GInputStream) stream = NULL;
	gsize streamsz = 0;
	gsize regionsz = 0;

	if (self->img == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "no IFD image");
		return FALSE;
	}

	/* write is performed on the parent MTD device */
	proxy = fu_device_get_proxy_with_fallback(device);
	if (proxy == NULL || !FU_IS_MTD_DEVICE(proxy)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no parent MTD device");
		return FALSE;
	}

	region = fu_firmware_get_idx(FU_FIRMWARE(self->img));
	/* ensure the BIOS master (host CPU) has write permission to this region */
	if ((fu_ifd_image_get_access(self->img, FU_IFD_REGION_BIOS) & FU_IFD_ACCESS_WRITE) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "region not writable by BIOS master");
		return FALSE;
	}
	addr = fu_firmware_get_addr(FU_FIRMWARE(self->img));
	regionsz = fu_firmware_get_size(FU_FIRMWARE(self->img));

	/* pick the correct sub-image from the provided firmware when possible */
	if (FU_IS_IFD_FIRMWARE(firmware)) {
		img_to_write = fu_firmware_get_image_by_idx(firmware, region, NULL);
		if (img_to_write == NULL) {
			/* fall back to writing the entire provided blob */
			img_to_write = g_object_ref(firmware);
		}
	} else {
		img_to_write = g_object_ref(firmware);
	}

	/* size sanity: avoid writing past region limit */
	stream = fu_firmware_get_stream(img_to_write, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (regionsz > 0 && streamsz > regionsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware (0x%x) larger than region (0x%x)",
			    (guint)streamsz,
			    (guint)regionsz);
		return FALSE;
	}

	/* delegate to the parent MTD device writer at the correct offset
	 * by setting the address on the image and calling the parent vfunc */
	fu_firmware_set_addr(img_to_write, addr);
	return fu_device_write_firmware(proxy, img_to_write, progress, flags, error);
}

static void
fu_mtd_ifd_device_init(FuMtdIfdDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_COMPUTER);
	/* default to plain string format; parent overrides when known */
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FALLBACK);
}

static void
fu_mtd_ifd_device_class_init(FuMtdIfdDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_mtd_ifd_device_probe;
	device_class->add_security_attrs = fu_mtd_ifd_device_add_security_attrs;
	device_class->write_firmware = fu_mtd_ifd_device_write_firmware;
}

FuMtdIfdDevice *
fu_mtd_ifd_device_new(FuDevice *parent, FuIfdImage *img)
{
	FuMtdIfdDevice *self =
	    g_object_new(FU_TYPE_MTD_IFD_DEVICE, "parent", parent, "proxy", parent, NULL);
	self->img = g_object_ref(img);
	return self;
}
