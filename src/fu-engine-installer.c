/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#include "fu-device-private.h"
#include "fu-engine-helper.h"
#include "fu-engine-installer.h"
#include "fu-engine-requirements.h"

struct _FuEngineInstaller {
	GObject parent_instance;
	FuEngine *engine;
	FuEngineRequest *request;
	GPtrArray *action_ids;
	GPtrArray *releases;
	FuCabinet *cabinet;
	GPtrArray *errors;
	gchar *remote_id;
};

G_DEFINE_TYPE(FuEngineInstaller, fu_engine_installer, G_TYPE_OBJECT)

/**
 * fu_engine_installer_set_request: (skip)
 * @self: a #FuEngineInstaller
 * @request: (nullable): optional #FuEngineRequest
 *
 * Sets the request to use for installing the cabinet archive.
 **/
void
fu_engine_installer_set_request(FuEngineInstaller *self, FuEngineRequest *request)
{
	g_return_if_fail(FU_IS_ENGINE_INSTALLER(self));
	g_return_if_fail(FU_IS_ENGINE_REQUEST(request));
	g_set_object(&self->request, request);
}

static gboolean
fu_engine_installer_has_action_id(FuEngineInstaller *self, const gchar *action_id)
{
	for (guint i = 0; i < self->action_ids->len; i++) {
		const gchar *action_id_tmp = g_ptr_array_index(self->action_ids, i);
		if (g_strcmp0(action_id_tmp, action_id) == 0)
			return TRUE;
	}
	return FALSE;
}

static void
fu_engine_installer_add_action_id(FuEngineInstaller *self, const gchar *action_id)
{
	if (fu_engine_installer_has_action_id(self, action_id))
		return;
	g_ptr_array_add(self->action_ids, g_strdup(action_id));
}

/**
 * fu_engine_installer_pop_action_id: (skip)
 * @self: a #FuEngineInstaller
 *
 * Pops an action ID from the list of actions to authenticate.
 *
 * Returns: (transfer full): The oldest remaining action ID, or %NULL for an empty array
 **/
gchar *
fu_engine_installer_pop_action_id(FuEngineInstaller *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_INSTALLER(self), NULL);
	if (self->action_ids->len == 0)
		return NULL;
	return g_ptr_array_steal_index(self->action_ids, 0);
}

/**
 * fu_engine_installer_get_releases: (skip)
 * @self: a #FuEngineInstaller
 *
 * Gets the releases to install. Each release will be tagged with the device the daemon should
 * install to.
 *
 * Returns: (transfer none) (element-type FuRelease): the releases to install
 **/
GPtrArray *
fu_engine_installer_get_releases(FuEngineInstaller *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_INSTALLER(self), NULL);
	return self->releases;
}

static gint
fu_engine_installer_release_sort_cb(gconstpointer a, gconstpointer b)
{
	FuRelease *release1 = *((FuRelease **)a);
	FuRelease *release2 = *((FuRelease **)b);
	return fu_release_compare(release1, release2);
}

static gboolean
fu_engine_installer_build_device(FuEngineInstaller *self,
				 XbNode *component,
				 FuDevice *device,
				 FwupdInstallFlags install_flags,
				 GError **error)
{
	g_autoptr(FuRelease) release = fu_release_new();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) releases = NULL;

	/* is this component valid for the device */
	fu_release_set_device(release, device);
	if (self->request != NULL)
		fu_release_set_request(release, self->request);
	if (self->remote_id != NULL) {
		fu_release_set_remote(
		    release,
		    fu_engine_get_remote_by_id(self->engine, self->remote_id, NULL));
	}
	if (!fu_release_load(release,
			     self->cabinet,
			     component,
			     NULL,
			     install_flags | FWUPD_INSTALL_FLAG_FORCE,
			     &error_local)) {
		g_ptr_array_add(self->errors, g_steal_pointer(&error_local));
		return TRUE;
	}
	if (!fu_engine_requirements_check(self->engine,
					  release,
					  install_flags | FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS,
					  &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("first pass requirement on %s:%s failed: %s",
				fu_device_get_id(device),
				xb_node_query_text(component, "id", NULL),
				error_local->message);
		}
		g_ptr_array_add(self->errors, g_steal_pointer(&error_local));
		return TRUE;
	}

	/* sync update message from CAB, but only if the metadata is trusted */
	if (fu_release_has_flag(release, FWUPD_RELEASE_FLAG_TRUSTED_METADATA)) {
		fu_device_ensure_from_component(device, component);
		fu_device_incorporate_from_component(device, component);
	} else {
		g_debug("not using untrusted metadata");
	}

	/* post-ensure checks */
	if (!fu_release_check_version(release, component, install_flags, &error_local)) {
		g_ptr_array_add(self->errors, g_steal_pointer(&error_local));
		return TRUE;
	}

	/* install each intermediate release */
	releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES)) {
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(XbQuery) query = NULL;

		/* we get this one "for free" */
		g_ptr_array_add(releases, g_object_ref(release));

		query = xb_query_new_full(xb_node_get_silo(component),
					  "releases/release",
					  XB_QUERY_FLAG_FORCE_NODE_CACHE,
					  error);
		if (query == NULL)
			return FALSE;
		rels = xb_node_query_full(component, query, NULL);
		/* add all but the first entry */
		for (guint i = 1; rels != NULL && i < rels->len; i++) {
			XbNode *rel = g_ptr_array_index(rels, i);
			g_autoptr(FuRelease) release2 = fu_release_new();
			g_autoptr(GError) error_loop = NULL;
			fu_release_set_device(release2, device);
			if (self->request != NULL)
				fu_release_set_request(release2, self->request);
			if (!fu_release_load(release2,
					     self->cabinet,
					     component,
					     rel,
					     install_flags,
					     &error_loop)) {
				g_ptr_array_add(self->errors, g_steal_pointer(&error_loop));
				continue;
			}
			g_ptr_array_add(releases, g_object_ref(release2));
		}
	} else {
		g_ptr_array_add(releases, g_object_ref(release));
	}

	/* make a second pass */
	for (guint i = 0; i < releases->len; i++) {
		FuRelease *release_tmp = g_ptr_array_index(releases, i);
		const gchar *action_id;
		if (!fu_engine_requirements_check(self->engine,
						  release_tmp,
						  install_flags,
						  &error_local)) {
			g_debug("second pass requirement on %s:%s failed: %s",
				fu_device_get_id(device),
				xb_node_query_text(component, "id", NULL),
				error_local->message);
			g_ptr_array_add(self->errors, g_steal_pointer(&error_local));
			continue;
		}
		if (!fu_engine_check_trust(self->engine, release_tmp, &error_local)) {
			g_ptr_array_add(self->errors, g_steal_pointer(&error_local));
			continue;
		}

		/* get the action IDs for the valid device */
		action_id = fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)
				? "org.freedesktop.fwupd.device-emulate"
				: fu_release_get_action_id(release_tmp);
		fu_engine_installer_add_action_id(self, action_id);
		g_ptr_array_add(self->releases, g_object_ref(release_tmp));
	}

	/* success */
	return TRUE;
}

/**
 * fu_engine_installer_build: (skip)
 * @self: a #FuEngineInstaller
 * @device_id: (not nullable): device ID, which can be `*`
 * @stream: (not nullable): the input stream for the *archive*
 * @install_flags: the #FwupdInstallFlags
 * @error: (nullable): optional return location for an error
 *
 * Parses the cabinet archive from @stream and builds the list of releases to install for the
 * devices that match @device_id.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_engine_installer_build(FuEngineInstaller *self,
			  const gchar *device_id,
			  FuInputStream *stream,
			  FwupdInstallFlags install_flags,
			  GError **error)
{
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GPtrArray) devices_possible = NULL;

	g_return_val_if_fail(FU_IS_ENGINE_INSTALLER(self), FALSE);
	g_return_val_if_fail(device_id != NULL, FALSE);
	g_return_val_if_fail(FU_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* cannot be reused */
	g_return_val_if_fail(self->cabinet == NULL, FALSE);

	/* get a list of devices that in some way match the device_id */
	if (g_strcmp0(device_id, FWUPD_DEVICE_ID_ANY) == 0) {
		devices_possible = fu_engine_get_devices(self->engine, error);
		if (devices_possible == NULL)
			return FALSE;
	} else {
		g_autoptr(FuDevice) device = NULL;
		device = fu_engine_get_device(self->engine, device_id, error);
		if (device == NULL)
			return FALSE;
		devices_possible =
		    fu_engine_get_devices_by_composite_id(self->engine,
							  fu_device_get_composite_id(device),
							  error);
		if (devices_possible == NULL)
			return FALSE;
	}

	/* parse silo */
	self->cabinet = fu_engine_build_cabinet_from_stream(self->engine, stream, error);
	if (self->cabinet == NULL)
		return FALSE;

	/* for each component in the silo */
	components = fu_cabinet_get_components(self->cabinet, error);
	if (components == NULL)
		return FALSE;
	self->remote_id = fu_engine_get_remote_id_for_stream(self->engine, stream);

	/* do any devices pass the requirements */
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index(components, i);
		for (guint j = 0; j < devices_possible->len; j++) {
			FuDevice *device = g_ptr_array_index(devices_possible, j);

			/* emulating */
			if ((install_flags & FWUPD_INSTALL_FLAG_ONLY_EMULATED) &&
			    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
				g_debug("skipping non-emulated %s", fu_device_get_id(device));
				continue;
			}

			g_debug("testing device %u [%s] with component %u",
				j,
				fu_device_get_id(device),
				i);
			if (!fu_engine_installer_build_device(self,
							      component,
							      device,
							      install_flags,
							      error))
				return FALSE;
		}
	}

	/* order the install tasks by the device priority */
	g_ptr_array_sort(self->releases, fu_engine_installer_release_sort_cb);

	/* nothing suitable */
	if (self->releases->len == 0) {
		GError *error_tmp = fu_engine_error_array_get_best(self->errors);
		g_propagate_error(error, error_tmp);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_engine_installer_init(FuEngineInstaller *self)
{
	self->action_ids = g_ptr_array_new_with_free_func(g_free);
	self->releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	self->errors = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);
}

static void
fu_engine_installer_finalize(GObject *obj)
{
	FuEngineInstaller *self = FU_ENGINE_INSTALLER(obj);
	g_clear_object(&self->cabinet);
	g_clear_object(&self->engine);
	g_clear_object(&self->request);
	g_free(self->remote_id);
	g_ptr_array_unref(self->action_ids);
	g_ptr_array_unref(self->releases);
	g_ptr_array_unref(self->errors);
	G_OBJECT_CLASS(fu_engine_installer_parent_class)->finalize(obj);
}

static void
fu_engine_installer_class_init(FuEngineInstallerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_engine_installer_finalize;
}

/**
 * fu_engine_installer_new: (skip)
 * @engine: (not nullable): a #FuEngine
 *
 * Creates a new helper object that can be used to match an archive to multiple devices.
 *
 * Returns: (transfer full): a #FuEngineInstaller
 **/
FuEngineInstaller *
fu_engine_installer_new(FuEngine *engine)
{
	g_autoptr(FuEngineInstaller) self = g_object_new(FU_TYPE_ENGINE_INSTALLER, NULL);
	g_return_val_if_fail(FU_IS_ENGINE(engine), NULL);
	self->engine = g_object_ref(engine);
	return g_steal_pointer(&self);
}
