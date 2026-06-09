/*
 * Copyright 2026 Sean Rhodes <sean@starlabs.systems>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-starlabs-coreboot-device.h"

struct _FuStarlabsCorebootDevice {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuStarlabsCorebootDevice, fu_starlabs_coreboot_device, FU_TYPE_DEVICE)

static void
fu_starlabs_coreboot_device_init(FuStarlabsCorebootDevice *self)
{
	g_autoptr(GString) msg = g_string_new(NULL);

	fu_device_set_id(FU_DEVICE(self), "star-labs-coreboot-manual-update");
	fu_device_set_vendor(FU_DEVICE(self), "Star Labs");
	fu_device_set_name(FU_DEVICE(self), "Coreboot");
	fu_device_set_summary(FU_DEVICE(self), "Manual update required");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ENSURE_SEMVER);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_COMPUTER);
	fu_device_set_details_url(FU_DEVICE(self), FU_STARLABS_COREBOOT_SUPPORT_URL);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE);

	g_string_append_printf(msg,
			       "This Star Labs coreboot firmware must be updated manually before "
			       "it can be updated by fwupd. Follow: %s",
			       FU_STARLABS_COREBOOT_SUPPORT_URL);
	fu_device_set_update_error(FU_DEVICE(self), msg->str);
}

static void
fu_starlabs_coreboot_device_constructed(GObject *obj)
{
	FuStarlabsCorebootDevice *self = FU_STARLABS_COREBOOT_DEVICE(obj);
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	GPtrArray *hwids = fu_context_get_hwid_guids(ctx);
	const gchar *version = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VERSION);

	fu_device_set_vendor(FU_DEVICE(self),
			     fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_MANUFACTURER));
	if (version != NULL) {
		if (strlen(version) > 9 && g_str_has_prefix(version, "CBET"))
			version += 9;
		fu_device_set_version(FU_DEVICE(self), version);
	}
	for (guint i = 0; i < hwids->len; i++) {
		const gchar *hwid = g_ptr_array_index(hwids, i);
		fu_device_add_instance_id(FU_DEVICE(self), hwid);
	}

	/* chain up to parent */
	G_OBJECT_CLASS(fu_starlabs_coreboot_device_parent_class)->constructed(obj);
}

static void
fu_starlabs_coreboot_device_class_init(FuStarlabsCorebootDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_starlabs_coreboot_device_constructed;
}
