/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd.h"

#include "fu-devlink-component.h"
#include "fu-devlink-device.h"

struct _FuDevlinkComponent {
	FuDevice parent_instance;
	gchar *component_name;
};

G_DEFINE_TYPE(FuDevlinkComponent, fu_devlink_component, FU_TYPE_DEVICE)

static void
fu_devlink_component_to_string(FuDevice *device, guint idt, GString *str)
{
	FuDevlinkComponent *self = FU_DEVLINK_COMPONENT(device);
	fwupd_codec_string_append(str, idt, "ComponentName", self->component_name);
}

/* Delegate firmware writing to parent with component specification */
static gboolean
fu_devlink_component_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuDevlinkComponent *self = FU_DEVLINK_COMPONENT(device);
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	gchar *component_name;

	g_return_val_if_fail(parent != NULL, FALSE);

	/* manually lock the parent device */
	locker = fu_device_locker_new(parent, error);
	if (!locker)
		return FALSE;

	g_debug("flashing component '%s' via parent device", self->component_name);

	component_name =
	    fu_device_has_private_flag(FU_DEVICE(self), FU_DEVLINK_DEVICE_FLAG_OMIT_COMPONENT_NAME)
		? NULL
		: self->component_name;

	return fu_devlink_device_write_firmware_component(FU_DEVLINK_DEVICE(parent),
							  component_name,
							  firmware,
							  progress,
							  flags,
							  error);
}

static gboolean
fu_devlink_component_reload(FuDevice *device, GError **error)
{
	FuDevlinkComponent *self = FU_DEVLINK_COMPONENT(device);
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	g_return_val_if_fail(parent != NULL, FALSE);

	/* manually lock the parent device */
	locker = fu_device_locker_new(parent, error);
	if (!locker)
		return FALSE;

	g_debug("reloading version for component '%s' via parent device", self->component_name);

	return fu_device_reload(parent, error);
}

/* Delegate firmware activation to parent device */
static gboolean
fu_devlink_component_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevlinkComponent *self = FU_DEVLINK_COMPONENT(device);
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	g_return_val_if_fail(parent != NULL, FALSE);

	/* manually lock the parent device */
	locker = fu_device_locker_new(parent, error);
	if (!locker)
		return FALSE;

	g_debug("activating firmware for component '%s' via parent device", self->component_name);

	return fu_device_activate(parent, progress, error);
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
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_DEVLINK_DEVICE_FLAG_OMIT_COMPONENT_NAME);
}

static void
fu_devlink_component_finalize(GObject *object)
{
	FuDevlinkComponent *self = FU_DEVLINK_COMPONENT(object);
	g_free(self->component_name);
	G_OBJECT_CLASS(fu_devlink_component_parent_class)->finalize(object);
}

static void
fu_devlink_component_class_init(FuDevlinkComponentClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	object_class->finalize = fu_devlink_component_finalize;
	device_class->to_string = fu_devlink_component_to_string;
	device_class->write_firmware = fu_devlink_component_write_firmware;
	device_class->reload = fu_devlink_component_reload;
	device_class->activate = fu_devlink_component_activate;
}

FuDevice *
fu_devlink_component_new(FuContext *ctx, const gchar *instance_id, const gchar *component_name)
{
	g_autofree gchar *device_id = NULL;
	FuDevlinkComponent *self;

	g_return_val_if_fail(component_name, NULL);

	self = g_object_new(FU_TYPE_DEVLINK_COMPONENT, "context", ctx, NULL);
	self->component_name = g_strdup(component_name);

	/* Set device properties */
	device_id = g_strdup_printf("component-%s", component_name);
	fu_device_set_logical_id(FU_DEVICE(self),
				 component_name); /* Use component name as logical ID */
	fu_device_set_name(FU_DEVICE(self), component_name);
	fu_device_add_instance_id(FU_DEVICE(self), instance_id);

	return FU_DEVICE(self);
}
