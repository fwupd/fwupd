/*
 * Copyright 2026 Harsha Muttavarapu <harshams@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct _FuBinderDaemon FuBinderDaemon;
void
fu_binder_setup_aidl_service(FuBinderDaemon *self);
void
fu_binder_bridge_emit_changed(void);
void
fu_binder_bridge_emit_device_added(GVariant *dict);
void
fu_binder_bridge_emit_device_removed(GVariant *dict);
void
fu_binder_bridge_emit_device_changed(GVariant *dict);
void
fu_binder_bridge_emit_device_request(GVariant *dict);
void
fu_binder_bridge_emit_properties_changed(GVariant *dict);

GVariant *
fu_binder_daemon_get_devices_as_variant(FuBinderDaemon *self, GError **error);
GVariant *
fu_binder_daemon_get_upgrades_as_variant(FuBinderDaemon *self,
					 const char *device_id,
					 GError **error);
GVariant *
fu_binder_daemon_get_remotes_as_variant(FuBinderDaemon *self, GError **error);
GVariant *
fu_binder_daemon_get_hwids_as_variant(FuBinderDaemon *self, GError **error);
GVariant *
fu_binder_daemon_get_properties_as_variant(FuBinderDaemon *self,
					   const gchar **property_names,
					   GError **error);

gboolean
fu_binder_daemon_update_metadata_bridge(void *daemon_instance,
					const char *remote_id,
					int fd_data,
					int fd_sig,
					GError **error);
gboolean
fu_binder_daemon_perform_install_bridge(void *daemon_instance,
					const char *device_id,
					int fd_handle,
					GVariant *options,
					GError **error);

G_END_DECLS
