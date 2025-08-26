/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-devlink-component.h"
#include "fu-devlink-device.h"

/**
 * FU_DEVLINK_DEVICE_FLAG_OMIT_COMPONENT_NAME:
 *
 * Do not set the DEVLINK_ATTR_FLASH_UPDATE_COMPONENT attribute when flashing firmware.
 * This allows for firmware updates without specifying a specific component name.
 *
 * Since: 2.0.14
 */
#define FU_DEVLINK_DEVICE_FLAG_OMIT_COMPONENT_NAME "omit-component-name"

struct _FuDevlinkComponent {
	FuDevice parent_instance;
	GPtrArray *instance_keys;
};

G_DEFINE_TYPE(FuDevlinkComponent, fu_devlink_component, FU_TYPE_DEVICE)

static gboolean
fu_devlink_component_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuDevlinkComponent *self = FU_DEVLINK_COMPONENT(device);
	FuDevice *proxy = fu_device_get_proxy(device);
	const gchar *component_name;

	component_name =
	    fu_device_has_private_flag(FU_DEVICE(self), FU_DEVLINK_DEVICE_FLAG_OMIT_COMPONENT_NAME)
		? NULL
		: fu_device_get_logical_id(device);
	return fu_devlink_device_write_firmware_component(FU_DEVLINK_DEVICE(proxy),
							  component_name,
							  firmware,
							  progress,
							  flags,
							  error);
}

void
fu_devlink_component_add_instance_keys(FuDevice *device, gchar **keys)
{
	FuDevlinkComponent *self = FU_DEVLINK_COMPONENT(device);

	if (self->instance_keys == NULL)
		self->instance_keys = g_ptr_array_new_with_free_func((GDestroyNotify)g_strfreev);
	g_ptr_array_add(self->instance_keys, keys);
}

static gboolean
fu_devlink_component_probe(FuDevice *device, GError **error)
{
	FuDevlinkComponent *self = FU_DEVLINK_COMPONENT(device);
	FuDevice *proxy = fu_device_get_proxy(device);
	g_autofree gchar *subsystem =
	    g_ascii_strup(fu_devlink_device_get_bus_name(FU_DEVLINK_DEVICE(proxy)), -1);

	/* build instance id just for component name */
	if (!fu_device_build_instance_id(device, error, subsystem, "VEN", "DEV", "COMPONENT", NULL))
		return FALSE;

	if (self->instance_keys == NULL)
		return TRUE;

	/* Build instance id for each fixed versions array from quirk file for which
	   kernel provides all fixed version values. */
	for (guint i = 0; i < self->instance_keys->len; i++) {
		g_autoptr(GStrvBuilder) keys_builder = g_strv_builder_new();
		g_auto(GStrv) keys = NULL;

		g_strv_builder_add(keys_builder, "VEN");
		g_strv_builder_add(keys_builder, "DEV");
		g_strv_builder_add(keys_builder, "COMPONENT");
		g_strv_builder_addv(keys_builder, g_ptr_array_index(self->instance_keys, i));
		keys = g_strv_builder_end(keys_builder);
		if (!fu_device_build_instance_id_strv(device, subsystem, keys, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_devlink_component_reload(FuDevice *device, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	return fu_device_reload(proxy, error);
}

static gboolean
fu_devlink_component_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	return fu_device_activate(proxy, progress, error);
}

static gboolean
fu_devlink_component_prepare(FuDevice *device,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	return fu_device_prepare(proxy, progress, flags, error);
}

static gboolean
fu_devlink_component_cleanup(FuDevice *device,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	return fu_device_cleanup(proxy, progress, flags, error);
}

FuDevice *
fu_devlink_component_new(FuDevice *proxy, const gchar *logical_id)
{
	g_autoptr(FuDevlinkComponent) self = NULL;

	g_return_val_if_fail(logical_id, NULL);

	self =
	    g_object_new(FU_TYPE_DEVLINK_COMPONENT, "proxy", proxy, "logical-id", logical_id, NULL);
	fu_device_add_instance_str(FU_DEVICE(self), "COMPONENT", logical_id);
	return FU_DEVICE(g_steal_pointer(&self));
}

static void
fu_devlink_component_init(FuDevlinkComponent *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_add_protocol(FU_DEVICE(self), "org.kernel.devlink");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DEVLINK_DEVICE_FLAG_OMIT_COMPONENT_NAME);
}

static void
fu_devlink_component_finalize(GObject *object)
{
	FuDevlinkComponent *self = FU_DEVLINK_COMPONENT(object);

	if (self->instance_keys != NULL)
		g_ptr_array_unref(self->instance_keys);

	G_OBJECT_CLASS(fu_devlink_component_parent_class)->finalize(object);
}

static void
fu_devlink_component_class_init(FuDevlinkComponentClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	object_class->finalize = fu_devlink_component_finalize;
	device_class->write_firmware = fu_devlink_component_write_firmware;
	device_class->probe = fu_devlink_component_probe;
	device_class->reload = fu_devlink_component_reload;
	device_class->activate = fu_devlink_component_activate;
	device_class->prepare = fu_devlink_component_prepare;
	device_class->cleanup = fu_devlink_component_cleanup;
}
