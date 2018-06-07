/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-device-private.h"
#include "fu-install-task.h"
#include "fu-keyring-utils.h"

struct _FuInstallTask
{
	GObject			 parent_instance;
	FuDevice		*device;
	AsApp			*app;
	FwupdTrustFlags		 trust_flags;
	gboolean		 is_downgrade;
};

G_DEFINE_TYPE (FuInstallTask, fu_install_task, G_TYPE_OBJECT)

/**
 * fu_install_task_get_device:
 * @self: A #FuInstallTask
 *
 * Gets the device for this task.
 *
 * Returns: (transfer none): the device
 **/
FuDevice *
fu_install_task_get_device (FuInstallTask *self)
{
	g_return_val_if_fail (FU_IS_INSTALL_TASK (self), NULL);
	return self->device;
}

/**
 * fu_install_task_get_app:
 * @self: A #FuInstallTask
 *
 * Gets the component for this task.
 *
 * Returns: (transfer none): the component
 **/
AsApp *
fu_install_task_get_app (FuInstallTask *self)
{
	g_return_val_if_fail (FU_IS_INSTALL_TASK (self), NULL);
	return self->app;
}

/**
 * fu_install_task_get_trust_flags:
 * @self: A #FuInstallTask
 *
 * Gets the trust flags for this task.
 *
 * NOTE: This is only set after fu_install_task_check_requirements() has been
 * called successfully.
 *
 * Returns: the #FwupdTrustFlags, e.g. #FWUPD_TRUST_FLAG_PAYLOAD
 **/
FwupdTrustFlags
fu_install_task_get_trust_flags (FuInstallTask *self)
{
	g_return_val_if_fail (FU_IS_INSTALL_TASK (self), FALSE);
	return self->trust_flags;
}

/**
 * fu_install_task_get_is_downgrade:
 * @self: A #FuInstallTask
 *
 * Gets if this task is to downgrade firmware.
 *
 * NOTE: This is only set after fu_install_task_check_requirements() has been
 * called successfully.
 *
 * Returns: %TRUE if versions numbers are going backwards
 **/
gboolean
fu_install_task_get_is_downgrade (FuInstallTask *self)
{
	g_return_val_if_fail (FU_IS_INSTALL_TASK (self), FALSE);
	return self->is_downgrade;
}

/**
 * fu_install_task_check_requirements:
 * @self: A #FuInstallTask
 * @flags: A #FwupdInstallFlags, e.g. #FWUPD_INSTALL_FLAG_ALLOW_OLDER
 * @error: A #GError, or %NULL
 *
 * Checks any requirements of this task. This will typically involve checking
 * that the device can accept the component (the GUIDs match) and that the
 * device can be upgraded with this firmware version.
 *
 * Returns: %TRUE if the requirements passed
 **/
gboolean
fu_install_task_check_requirements (FuInstallTask *self,
				    FwupdInstallFlags flags,
				    GError **error)
{
	AsRelease *release;
	GPtrArray *provides;
	const gchar *version;
	const gchar *version_release;
	const gchar *version_lowest;
	gboolean matches_guid = FALSE;
	gint vercmp;

	g_return_val_if_fail (FU_IS_INSTALL_TASK (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* does this app provide a GUID the device has */
	provides = as_app_get_provides (self->app);
	for (guint i = 0; i < provides->len; i++) {
		AsProvide *provide = g_ptr_array_index (provides, i);
		if (as_provide_get_kind (provide) != AS_PROVIDE_KIND_FIRMWARE_FLASHED)
			continue;
		if (fu_device_has_guid (self->device, as_provide_get_value (provide))) {
			matches_guid = TRUE;
			break;
		}
	}
	if (!matches_guid) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "No supported devices found");
		return FALSE;
	}

	/* check the device is not locked */
	if (fu_device_has_flag (self->device, FWUPD_DEVICE_FLAG_LOCKED)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Device %s [%s] is locked",
			     fu_device_get_name (self->device),
			     fu_device_get_id (self->device));
		return FALSE;
	}

	/* no update abilities */
	if (!fu_device_has_flag (self->device, FWUPD_DEVICE_FLAG_UPDATABLE)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Device %s [%s] does not currently allow updates",
			     fu_device_get_name (self->device),
			     fu_device_get_id (self->device));
		return FALSE;
	}

	/* called with online update, test if device is supposed to allow this */
	if ((flags & FWUPD_INSTALL_FLAG_OFFLINE) == 0 &&
	    fu_device_has_flag (self->device, FWUPD_DEVICE_FLAG_ONLY_OFFLINE)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Device %s [%s] only allows offline updates",
			     fu_device_get_name (self->device),
			     fu_device_get_id (self->device));
		return FALSE;
	}

	/* get device */
	version = fu_device_get_version (self->device);
	if (version == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Device %s [%s] has no firmware version",
			     fu_device_get_name (self->device),
			     fu_device_get_id (self->device));
		return FALSE;
	}

	/* get latest release */
	release = as_app_get_release_default (self->app);
	if (release == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "%s [%s] has no firmware update metadata",
			     fu_device_get_name (self->device),
			     fu_device_get_id (self->device));
		return FALSE;
	}

	/* is this a downgrade or re-install */
	version_release = as_release_get_version (release);
	if (version_release == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Release has no firmware version");
		return FALSE;
	}

	/* compare to the lowest supported version, if it exists */
	version_lowest = fu_device_get_version_lowest (self->device);
	if (version_lowest != NULL && as_utils_vercmp (version_lowest, version) > 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_NEWER,
			     "Specified firmware is older than the minimum "
			     "required version '%s < %s'", version_lowest, version);
		return FALSE;
	}

	/* check semver */
	vercmp = as_utils_vercmp (version, version_release);
	if (vercmp == 0 && (flags & FWUPD_INSTALL_FLAG_ALLOW_REINSTALL) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_SAME,
			     "Specified firmware is already installed '%s'",
			     version_release);
		return FALSE;
	}
	self->is_downgrade = vercmp > 0;
	if (self->is_downgrade && (flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_VERSION_NEWER,
			     "Specified firmware is older than installed '%s < %s'",
			     version_release, version);
		return FALSE;
	}

	/* verify */
	return fu_keyring_get_release_trust_flags (release, &self->trust_flags, error);
}

/**
 * fu_install_task_get_action_id:
 * @self: A #FuEngine
 *
 * Gets the PolicyKit action ID to use for the install operation.
 *
 * Returns: string, e.g. `org.freedesktop.fwupd.update-internal-trusted`
 **/
const gchar *
fu_install_task_get_action_id (FuInstallTask *self)
{
	/* relax authentication checks for removable devices */
	if (!fu_device_has_flag (self->device, FWUPD_DEVICE_FLAG_INTERNAL)) {
		if (self->is_downgrade)
			return "org.freedesktop.fwupd.downgrade-hotplug";
		if (self->trust_flags & FWUPD_TRUST_FLAG_PAYLOAD)
			return "org.freedesktop.fwupd.update-hotplug-trusted";
		return "org.freedesktop.fwupd.update-hotplug";
	}

	/* internal device */
	if (self->is_downgrade)
		return "org.freedesktop.fwupd.downgrade-internal";
	if (self->trust_flags & FWUPD_TRUST_FLAG_PAYLOAD)
		return "org.freedesktop.fwupd.update-internal-trusted";
	return "org.freedesktop.fwupd.update-internal";
}

static void
fu_install_task_init (FuInstallTask *self)
{
	self->trust_flags = FWUPD_TRUST_FLAG_NONE;
}

static void
fu_install_task_finalize (GObject *object)
{
	FuInstallTask *self = FU_INSTALL_TASK (object);

	g_object_unref (self->app);
	if (self->device != NULL)
		g_object_unref (self->device);

	G_OBJECT_CLASS (fu_install_task_parent_class)->finalize (object);
}

static void
fu_install_task_class_init (FuInstallTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_install_task_finalize;
}

/**
 * fu_install_task_compare:
 * @task1: first #FuInstallTask to compare.
 * @task2: second #FuInstallTask to compare.
 *
 * Compares two install tasks.
 *
 * Returns: 1, 0 or -1 if @task1 is greater, equal, or less than @task2, respectively.
 **/
gint
fu_install_task_compare (FuInstallTask *task1, FuInstallTask *task2)
{
	FuDevice *device1 = fu_install_task_get_device (task1);
	FuDevice *device2 = fu_install_task_get_device (task2);
	if (fu_device_get_order (device1) < fu_device_get_order (device2))
		return -1;
	if (fu_device_get_order (device1) > fu_device_get_order (device2))
		return 1;
	return 0;
}

/**
 * fu_install_task_new:
 * @device: A #FuDevice
 * @app: a #AsApp
 *
 * Creates a new install task that may or may not be valid.
 *
 * Returns: (transfer full): the #FuInstallTask
 **/
FuInstallTask *
fu_install_task_new (FuDevice *device, AsApp *app)
{
	FuInstallTask *self;
	self = g_object_new (FU_TYPE_TASK, NULL);
	self->app = g_object_ref (app);
	if (device != NULL)
		self->device = g_object_ref (device);
	return FU_INSTALL_TASK (self);
}
