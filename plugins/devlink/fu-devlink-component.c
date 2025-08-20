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
};

G_DEFINE_TYPE(FuDevlinkComponent, fu_devlink_component, FU_TYPE_DEVICE)

/* delegate firmware writing to parent with component specification */
static gboolean
fu_devlink_component_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuDevlinkComponent *self = FU_DEVLINK_COMPONENT(device);
	FuDevice *proxy = fu_device_get_proxy(device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	const gchar *component_name;

	g_return_val_if_fail(proxy != NULL, FALSE);

	/* manually lock the proxy device */
	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;

	g_debug("flashing component '%s' via proxy device", fu_device_get_logical_id(device));

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

static gboolean
fu_devlink_component_reload(FuDevice *device, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	g_return_val_if_fail(proxy != NULL, FALSE);

	/* manually lock the proxy device */
	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;

	g_debug("reloading version for component '%s' via proxy device",
		fu_device_get_logical_id(device));
	return fu_device_reload(proxy, error);
}

/* delegate firmware activation to proxy device */
static gboolean
fu_devlink_component_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	g_return_val_if_fail(proxy != NULL, FALSE);

	/* manually lock the proxy device */
	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;

	g_debug("activating firmware for component '%s' via proxy device",
		fu_device_get_logical_id(device));
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

typedef struct {
	FuDevice *device;
	GStrvBuilder *keys_builder;
} FuDevlinkComponentInstanceIdHelper;

static void
fu_devlink_component_instance_id_cb(gpointer key, gpointer value, gpointer user_data)
{
	FuDevlinkVersionInfo *version_info = value;
	FuDevlinkComponentInstanceIdHelper *helper = user_data;

	if (version_info->fixed == NULL)
		return;

	fu_device_add_instance_str(helper->device, key, version_info->fixed);
	g_strv_builder_add(helper->keys_builder, key);
}

void
fu_devlink_component_build_instance_id(FuDevice *device, FuDevice *proxy, GHashTable *version_table)
{
	g_auto(GStrv) keys = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GStrvBuilder) keys_builder = g_strv_builder_new();
	FuDevlinkComponentInstanceIdHelper helper = {
	    .device = device,
	    .keys_builder = keys_builder,
	};

	/* append fixed versions to instance id */
	g_strv_builder_add(keys_builder, "VEN");
	g_strv_builder_add(keys_builder, "DEV");
	g_hash_table_foreach(version_table, fu_devlink_component_instance_id_cb, &helper);

	g_strv_builder_add(keys_builder, "COMPONENT");
	fu_device_add_instance_str(device, "COMPONENT", fu_device_get_logical_id(device));

	keys = g_strv_builder_end(keys_builder);
	if (g_strv_length(keys) == 0) {
		g_debug("no instance id items found, skipping building instance id");
		return;
	}
	if (!fu_device_build_instance_id_strv(device, "PCI", keys, &error_local)) {
		g_debug("failed to build devlink info based instance id for component %s: %s",
			fu_device_get_logical_id(device),
			error_local->message);
	}

	/* build additional instance ID based on vid and pid if available */
	if (fu_device_get_vid(proxy) != 0x0 && fu_device_get_pid(proxy) != 0x0) {
		/* add PCI vendor and product IDs from proxy device */
		fu_device_add_instance_u16(device, "VEN", fu_device_get_vid(proxy));
		fu_device_add_instance_u16(device, "DEV", fu_device_get_pid(proxy));

		if (!fu_device_build_instance_id(device,
						 &error_local,
						 "PCI",
						 "VEN",
						 "DEV",
						 "COMPONENT",
						 NULL)) {
			g_debug("failed to build vid/pid instance id for component %s: %s",
				fu_device_get_logical_id(device),
				error_local->message);
		}
	}
}

FuDevice *
fu_devlink_component_new(FuDevice *proxy, const gchar *component_name)
{
	g_autoptr(FuDevlinkComponent) self = NULL;

	g_return_val_if_fail(component_name, NULL);

	self = g_object_new(FU_TYPE_DEVLINK_COMPONENT,
			    "proxy",
			    proxy,
			    "logical-id",
			    component_name,
			    NULL);
	return FU_DEVICE(g_steal_pointer(&self));
}

static void
fu_devlink_component_init(FuDevlinkComponent *self)
{
	fu_device_set_summary(FU_DEVICE(self), "Devlink component");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_protocol(FU_DEVICE(self), "org.kernel.devlink");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DEVLINK_DEVICE_FLAG_OMIT_COMPONENT_NAME);
}

static void
fu_devlink_component_class_init(FuDevlinkComponentClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_devlink_component_write_firmware;
	device_class->reload = fu_devlink_component_reload;
	device_class->activate = fu_devlink_component_activate;
	device_class->prepare = fu_devlink_component_prepare;
	device_class->cleanup = fu_devlink_component_cleanup;
}
