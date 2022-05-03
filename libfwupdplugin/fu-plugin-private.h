/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-context.h"
#include "fu-plugin.h"
#include "fu-security-attrs.h"

FuPlugin *
fu_plugin_new(FuContext *ctx);
gboolean
fu_plugin_is_open(FuPlugin *self);
guint
fu_plugin_get_order(FuPlugin *self);
void
fu_plugin_set_order(FuPlugin *self, guint order);
guint
fu_plugin_get_priority(FuPlugin *self);
void
fu_plugin_set_priority(FuPlugin *self, guint priority);
void
fu_plugin_set_name(FuPlugin *self, const gchar *name);
const gchar *
fu_plugin_get_build_hash(FuPlugin *self);
GPtrArray *
fu_plugin_get_rules(FuPlugin *self, FuPluginRule rule);
gboolean
fu_plugin_has_rule(FuPlugin *self, FuPluginRule rule, const gchar *name);
GHashTable *
fu_plugin_get_report_metadata(FuPlugin *self);
gboolean
fu_plugin_open(FuPlugin *self, const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_plugin_runner_init(FuPlugin *self);
gboolean
fu_plugin_runner_startup(FuPlugin *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_coldplug(FuPlugin *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_prepare(FuPlugin *self,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_cleanup(FuPlugin *self,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_composite_prepare(FuPlugin *self,
				   GPtrArray *devices,
				   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_composite_cleanup(FuPlugin *self,
				   GPtrArray *devices,
				   GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_attach(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_detach(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_reload(FuPlugin *self, FuDevice *device, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_backend_device_added(FuPlugin *self,
				      FuDevice *device,
				      GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_backend_device_changed(FuPlugin *self,
					FuDevice *device,
					GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_device_created(FuPlugin *self,
				FuDevice *device,
				GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_plugin_runner_device_added(FuPlugin *self, FuDevice *device);
void
fu_plugin_runner_device_removed(FuPlugin *self, FuDevice *device);
void
fu_plugin_runner_device_register(FuPlugin *self, FuDevice *device);
gboolean
fu_plugin_runner_write_firmware(FuPlugin *self,
				FuDevice *device,
				GBytes *blob_fw,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_verify(FuPlugin *self,
			FuDevice *device,
			FuProgress *progress,
			FuPluginVerifyFlags flags,
			GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_activate(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error);
gboolean
fu_plugin_runner_unlock(FuPlugin *self, FuDevice *device, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_clear_results(FuPlugin *self,
			       FuDevice *device,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_plugin_runner_get_results(FuPlugin *self,
			     FuDevice *device,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_plugin_runner_add_security_attrs(FuPlugin *self, FuSecurityAttrs *attrs);
gint
fu_plugin_name_compare(FuPlugin *plugin1, FuPlugin *plugin2);
gint
fu_plugin_order_compare(FuPlugin *plugin1, FuPlugin *plugin2);

/* utils */
gchar *
fu_plugin_guess_name_from_fn(const gchar *filename);
