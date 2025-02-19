/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-context.h"
#include "fu-plugin.h"
#include "fu-security-attrs.h"

FuPlugin *
fu_plugin_new(FuContext *ctx);
FuPlugin *
fu_plugin_new_from_gtype(GType gtype, FuContext *ctx) G_GNUC_NON_NULL(2);
void
fu_plugin_set_context(FuPlugin *self, FuContext *ctx) G_GNUC_NON_NULL(1);
gboolean
fu_plugin_is_open(FuPlugin *self) G_GNUC_NON_NULL(1);
guint
fu_plugin_get_order(FuPlugin *self) G_GNUC_NON_NULL(1);
void
fu_plugin_set_order(FuPlugin *self, guint order) G_GNUC_NON_NULL(1);
guint
fu_plugin_get_priority(FuPlugin *self) G_GNUC_NON_NULL(1);
void
fu_plugin_set_priority(FuPlugin *self, guint priority) G_GNUC_NON_NULL(1);
GArray *
fu_plugin_get_device_gtypes(FuPlugin *self) G_GNUC_NON_NULL(1);
gchar *
fu_plugin_to_string(FuPlugin *self) G_GNUC_NON_NULL(1);
void
fu_plugin_add_string(FuPlugin *self, guint idt, GString *str) G_GNUC_NON_NULL(1);
GPtrArray *
fu_plugin_get_rules(FuPlugin *self, FuPluginRule rule) G_GNUC_NON_NULL(1);
GHashTable *
fu_plugin_get_report_metadata(FuPlugin *self) G_GNUC_NON_NULL(1);
gboolean
fu_plugin_open(FuPlugin *self, const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_reset_config_values(FuPlugin *self, GError **error) G_GNUC_NON_NULL(1);
void
fu_plugin_runner_init(FuPlugin *self) G_GNUC_NON_NULL(1);
gboolean
fu_plugin_runner_startup(FuPlugin *self,
			 FuProgress *progress,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_ready(FuPlugin *self,
		       FuProgress *progress,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_coldplug(FuPlugin *self,
			  FuProgress *progress,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_prepare(FuPlugin *self,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_plugin_runner_cleanup(FuPlugin *self,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_plugin_runner_composite_prepare(FuPlugin *self,
				   GPtrArray *devices,
				   GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_composite_cleanup(FuPlugin *self,
				   GPtrArray *devices,
				   GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_attach(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_plugin_runner_detach(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_plugin_runner_reload(FuPlugin *self, FuDevice *device, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_backend_device_added(FuPlugin *self,
				      FuDevice *device,
				      FuProgress *progress,
				      GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_plugin_runner_backend_device_changed(FuPlugin *self,
					FuDevice *device,
					GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_device_created(FuPlugin *self,
				FuDevice *device,
				GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
void
fu_plugin_runner_device_added(FuPlugin *self, FuDevice *device) G_GNUC_NON_NULL(1, 2);
void
fu_plugin_runner_device_removed(FuPlugin *self, FuDevice *device) G_GNUC_NON_NULL(1, 2);
void
fu_plugin_runner_device_register(FuPlugin *self, FuDevice *device) G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_write_firmware(FuPlugin *self,
				FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2, 3, 4);
gboolean
fu_plugin_runner_verify(FuPlugin *self,
			FuDevice *device,
			FuProgress *progress,
			FuPluginVerifyFlags flags,
			GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_plugin_runner_activate(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error)
    G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_plugin_runner_unlock(FuPlugin *self, FuDevice *device, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_clear_results(FuPlugin *self,
			       FuDevice *device,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_get_results(FuPlugin *self,
			     FuDevice *device,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_fix_host_security_attr(FuPlugin *self,
					FwupdSecurityAttr *attr,
					GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_undo_host_security_attr(FuPlugin *self,
					 FwupdSecurityAttr *attr,
					 GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_reboot_cleanup(FuPlugin *self,
				FuDevice *device,
				GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
void
fu_plugin_runner_add_security_attrs(FuPlugin *self, FuSecurityAttrs *attrs) G_GNUC_NON_NULL(1, 2);
gboolean
fu_plugin_runner_modify_config(FuPlugin *self, const gchar *key, const gchar *value, GError **error)
    G_GNUC_NON_NULL(1, 2, 3);
gint
fu_plugin_name_compare(FuPlugin *plugin1, FuPlugin *plugin2) G_GNUC_NON_NULL(1, 2);
gint
fu_plugin_order_compare(FuPlugin *plugin1, FuPlugin *plugin2) G_GNUC_NON_NULL(1, 2);

/* utils */
gchar *
fu_plugin_guess_name_from_fn(const gchar *filename) G_GNUC_NON_NULL(1);
