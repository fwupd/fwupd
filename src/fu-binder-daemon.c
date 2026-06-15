/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 * Copyright 2024 Colin Kinloch <colin.kinloch@collabora.com>
 * Updated for Structured AIDL 2026
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include <android/binder_ibinder.h>
#include <android/binder_ibinder_platform.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_process.h>
#include <android/binder_status.h>
#include <dlfcn.h>
#include <fwupdplugin.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "config.h"
#include "fu-binder-daemon-bridge.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-engine-helper.h"
#include "fu-engine-request.h"
#include "fu-engine-requirements.h"
#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"

#ifdef HAVE_GIO_UNIX
#include "fu-unix-seekable-input-stream.h"
#endif

#include "fu-binder-daemon.h"

struct _FuBinderDaemon {
  FuDaemon parent_instance;
  gboolean async;
  gulong presence_id;
  gint binder_fd;
  FwupdStatus status; /* last emitted */
  gdouble percentage; /* last emitted */
  GPtrArray* event_listener_binders;
  AIBinder_Class* listener_binder_class;
};

G_DEFINE_TYPE(FuBinderDaemon, fu_binder_daemon, FU_TYPE_DAEMON)

/* --- Install Helper Struct --- */
typedef struct {
  FuBinderDaemon* self;
  FuEngineRequest* request;
  FuProgress* progress;
  gchar* device_id;
  guint64 flags;
  GInputStream* stream;
  FuCabinet* cabinet;
  GPtrArray* action_ids;
  GPtrArray* releases;
  GPtrArray* errors;
  gchar* remote_id;
} FuMainAuthHelper;

static void fu_main_auth_helper_free(FuMainAuthHelper* helper) {
  if (helper->request) g_object_unref(helper->request);
  if (helper->progress) g_object_unref(helper->progress);
  if (helper->device_id) g_free(helper->device_id);
  if (helper->stream) g_object_unref(helper->stream);
  if (helper->cabinet) g_object_unref(helper->cabinet);
  if (helper->action_ids) g_ptr_array_unref(helper->action_ids);
  if (helper->releases) g_ptr_array_unref(helper->releases);
  if (helper->errors) g_ptr_array_unref(helper->errors);
  if (helper->remote_id) g_free(helper->remote_id);
  g_free(helper);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuMainAuthHelper, fu_main_auth_helper_free)

/* --- GSource for Polling Binder --- */

typedef struct _FuBinderFdSource {
  GSource source;
  gpointer fd_tag;
} FuBinderFdSource;

static gboolean binder_fd_source_check(GSource* source) {
  FuBinderFdSource* binder_fd_source = (FuBinderFdSource*)source;
  return g_source_query_unix_fd(source, binder_fd_source->fd_tag) & G_IO_IN;
}

static gboolean binder_fd_source_dispatch(GSource* source, GSourceFunc callback,
                                          gpointer user_data) {
  (void)ABinderProcess_handlePolledCommands();
  return G_SOURCE_CONTINUE;
}

static GSourceFuncs binder_fd_source_funcs = {
    NULL,
    binder_fd_source_check,
    binder_fd_source_dispatch,
};

static GSource* fu_binder_fd_source_new(gint fd) {
  GSource* source =
      g_source_new(&binder_fd_source_funcs, sizeof(FuBinderFdSource));
  FuBinderFdSource* binder_fd_source = (FuBinderFdSource*)source;
  binder_fd_source->fd_tag =
      g_source_add_unix_fd(source, fd, G_IO_IN | G_IO_ERR);
  return source;
}

/* --- Core Logic Helpers --- */

static void fu_binder_daemon_set_status(FuBinderDaemon* self,
                                        FwupdStatus status) {
  if (self->status == status) return;
  self->status = status;

  g_debug("Status changed to: %s", fwupd_status_to_string(status));
}
static void fu_binder_daemon_progress_percentage_changed_cb(
    FuProgress* progress, gdouble percentage, FuBinderDaemon* self) {
  gboolean notify = fwupd_percentage_delta_notify(self->percentage, percentage);
  self->percentage = percentage;

  if (notify) {
    GVariantBuilder builder;
    g_autoptr(GVariant) dict = NULL;

    g_debug("Emitting PropertyChanged('Percentage'='%.1f%%')", percentage);

    /* Build the dictionary expected by the AIDL bridge */
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(
        &builder, "{sv}", "Percentage",
        g_variant_new_uint32(
            fwupd_percentage_is_valid(percentage) ? (guint32)percentage : 0));

    /* Sink the floating reference so it frees automatically when out of scope
     */
    dict = g_variant_ref_sink(g_variant_builder_end(&builder));

    /* Send across the C++ bridge */
    fu_binder_bridge_emit_properties_changed(dict);
  }
}

static FuEngineRequest* fu_binder_daemon_create_request(FuBinderDaemon* self) {
  FuEngine* engine = fu_daemon_get_engine(FU_DAEMON(self));
  uid_t uid = AIBinder_getCallingUid();
  pid_t pid = AIBinder_getCallingPid();
  g_autofree gchar* sender = g_strdup_printf("%u:%u", (guint)uid, (guint)pid);
  g_autoptr(FuEngineRequest) request = fu_engine_request_new(sender);

  fu_engine_request_set_converter_flags(request, FWUPD_CODEC_FLAG_TRUSTED);

  return g_steal_pointer(&request);
}

/* --- Production Data Getters (Used by the Bridge) --- */

GVariant* fu_binder_daemon_get_devices_as_variant(FuBinderDaemon* self,
                                                  GError** error) {
  FuEngine* engine = fu_daemon_get_engine(FU_DAEMON(self));

  FuContext* ctx = fu_engine_get_context(engine);

  g_autoptr(GPtrArray) devices = NULL;
  g_autoptr(FuEngineRequest) request = fu_binder_daemon_create_request(self);

  devices = fu_engine_get_devices(engine, error);
  if (devices == NULL) return NULL;

  FwupdCodecFlags flags = fu_engine_request_get_converter_flags(request);

  /* Now ctx exists and can be safely passed to the config checker */
  if (fu_context_get_config_bool(ctx, "ShowDevicePrivate")) {
    flags |= FWUPD_CODEC_FLAG_TRUSTED;
  }

  return fwupd_codec_array_to_variant(devices, flags);
}

GVariant* fu_binder_daemon_get_upgrades_as_variant(FuBinderDaemon* self,
                                                   const gchar* device_id,
                                                   GError** error) {
  FuEngine* engine = fu_daemon_get_engine(FU_DAEMON(self));
  g_autoptr(GPtrArray) releases = NULL;
  g_autoptr(FuEngineRequest) request = fu_binder_daemon_create_request(self);

  /* Validate the ID exactly like the manual IPC did */
  if (!fu_daemon_device_id_valid(device_id, error)) return NULL;

  releases = fu_engine_get_upgrades(engine, request, device_id, error);
  if (releases == NULL) return NULL;

  /* Pack the release array into a variant using the standard codec */
  return fwupd_codec_array_to_variant(releases, FWUPD_CODEC_FLAG_NONE);
}

GVariant* fu_binder_daemon_get_remotes_as_variant(FuBinderDaemon* self,
                                                  GError** error) {
  FuEngine* engine = fu_daemon_get_engine(FU_DAEMON(self));
  g_autoptr(GPtrArray) remotes = fu_engine_get_remotes(engine, error);

  if (remotes == NULL) return NULL;

  return fwupd_codec_array_to_variant(remotes, FWUPD_CODEC_FLAG_NONE);
}

gboolean fu_binder_daemon_update_metadata_bridge(void* daemon_instance,
                                                 const char* remote_id,
                                                 int fd_data, int fd_sig,
                                                 GError** error) {
#ifdef HAVE_GIO_UNIX
  FuBinderDaemon* self = FU_BINDER_DAEMON(daemon_instance);
  FuEngine* engine = fu_daemon_get_engine(FU_DAEMON(self));

  /* Pass the FDs to the engine to parse the new LVFS metadata */
  if (!fu_engine_update_metadata(engine, remote_id, fd_data, fd_sig, error)) {
    return FALSE;
  }
  return TRUE;
#else
  g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
                      "unsupported feature");
  return FALSE;
#endif
}

GVariant* fu_binder_daemon_get_hwids_as_variant(FuBinderDaemon* self,
                                                GError** error) {
  FuEngine* engine = fu_daemon_get_engine(FU_DAEMON(self));
  FuContext* ctx = fu_engine_get_context(engine);
  FuHwids* hwids = fu_context_get_hwids(ctx);
  g_autoptr(GPtrArray) chid_keys = fu_hwids_get_chid_keys(hwids);
  g_autoptr(GPtrArray) hwid_keys = fu_hwids_get_keys(hwids);
  g_auto(GVariantBuilder) builder;

  g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

  /* Add standard Hardware IDs */
  for (guint i = 0; i < hwid_keys->len; i++) {
    const gchar* hwid_key = g_ptr_array_index(hwid_keys, i);
    const gchar* value = fu_hwids_get_value(hwids, hwid_key);
    if (value == NULL) continue;
    g_variant_builder_add(&builder, "{sv}", hwid_key,
                          g_variant_new_string(value));
  }

  /* Add Computer Hardware IDs (CHIDs) */
  for (guint i = 0; i < chid_keys->len; i++) {
    const gchar* key = g_ptr_array_index(chid_keys, i);
    const gchar* keys = fu_hwids_get_replace_keys(hwids, key);
    if (keys == NULL) continue;

    g_autofree gchar* guid = fu_hwids_get_guid(hwids, key, NULL);
    if (guid == NULL) continue;

    g_variant_builder_add(&builder, "{sv}", keys, g_variant_new_string(guid));
  }

  return g_variant_builder_end(&builder);
}

GVariant* fu_binder_daemon_get_properties_as_variant(
    FuBinderDaemon* self, const gchar** property_names, GError** error) {
  FuEngine* engine = fu_daemon_get_engine(FU_DAEMON(self));
  g_auto(GVariantDict) vardict = G_VARIANT_DICT_INIT(NULL);

  for (guint i = 0; property_names[i] != NULL; i++) {
    const gchar* key = property_names[i];
    g_autoptr(GVariant) val = NULL;

    if (g_strcmp0(key, "DaemonVersion") == 0) {
      val = g_variant_new_string(PACKAGE_VERSION);
    } else if (g_strcmp0(key, "HostBkc") == 0) {
      FuContext* ctx = fu_engine_get_context(engine);
      g_autofree gchar* host_bkc = fu_context_get_config_str(ctx, "HostBkc");
      val = g_variant_new_string(host_bkc != NULL ? host_bkc : "");
    } else if (g_strcmp0(key, "Tainted") == 0) {
      val = g_variant_new_boolean(FALSE);
    } else if (g_strcmp0(key, "Status") == 0) {
      val = g_variant_new_uint32(self->status);
    } else if (g_strcmp0(key, "Percentage") == 0) {
      val = g_variant_new_uint32(self->percentage);
    } else if (g_strcmp0(key, FWUPD_RESULT_KEY_BATTERY_LEVEL) == 0) {
      FuContext* ctx = fu_engine_get_context(engine);
      val = g_variant_new_uint32(fu_context_get_battery_level(ctx));
    } else if (g_strcmp0(key, FWUPD_RESULT_KEY_BATTERY_THRESHOLD) == 0) {
      FuContext* ctx = fu_engine_get_context(engine);
      val = g_variant_new_uint32(fu_context_get_battery_threshold(ctx));
    } else if (g_strcmp0(key, "HostVendor") == 0) {
      val = g_variant_new_string(fu_engine_get_host_vendor(engine));
    } else if (g_strcmp0(key, "HostProduct") == 0) {
      val = g_variant_new_string(fu_engine_get_host_product(engine));
    } else if (g_strcmp0(key, "HostMachineId") == 0) {
      const gchar* tmp = fu_engine_get_host_machine_id(engine);
      if (tmp == NULL) {
        g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
                    "failed to get daemon property %s", key);
        return NULL;
      }
      val = g_variant_new_string(tmp);
    } else if (g_strcmp0(key, "HostSecurityId") == 0) {
#ifdef HAVE_HSI
      g_autofree gchar* tmp = fu_engine_get_host_security_id(engine, NULL);
      val = g_variant_new_string(tmp);
#else
      g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
                  "failed to get daemon property %s", key);
      return NULL;
#endif
    } else if (g_strcmp0(key, "Interactive") == 0) {
      val = g_variant_new_boolean(isatty(fileno(stdout)) != 0);
    } else if (g_strcmp0(key, "OnlyTrusted") == 0) {
      FuContext* ctx = fu_engine_get_context(engine);
      val =
          g_variant_new_boolean(fu_context_get_config_bool(ctx, "OnlyTrusted"));
    } else {
      g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
                  "unknown daemon property %s", key);
      return NULL;
    }

    if (val) {
      g_variant_dict_insert_value(&vardict, key, g_steal_pointer(&val));
    }
  }

  return g_variant_dict_end(&vardict);
}

/* --- AIDL Installation Logic --- */

#ifdef HAVE_GIO_UNIX

static gint fu_binder_daemon_release_sort_cb(gconstpointer a, gconstpointer b) {
  FuRelease* release1 = *((FuRelease**)a);
  FuRelease* release2 = *((FuRelease**)b);
  return fu_release_compare(release1, release2);
}

static void fu_binder_daemon_progress_status_changed_cb(FuProgress* progress,
                                                        FwupdStatus status,
                                                        FuBinderDaemon* self) {
  fu_binder_daemon_set_status(self, status);

  /* Broadcast to AIDL listeners */
  g_autoptr(GError) error = NULL;
  const gchar* props[] = {"Status", NULL};
  g_autoptr(GVariant) val =
      fu_binder_daemon_get_properties_as_variant(self, props, &error);
  fu_binder_bridge_emit_properties_changed(val);
}

static gboolean fu_dbus_daemon_install_with_helper_device(
    FuMainAuthHelper* helper, XbNode* component, FuDevice* device,
    GError** error) {
  FuBinderDaemon* self = helper->self;
  FuEngine* engine = fu_daemon_get_engine(FU_DAEMON(self));
  g_autoptr(FuRelease) release = fu_release_new();
  g_autoptr(GError) error_local = NULL;
  g_autoptr(GPtrArray) releases = NULL;

  fu_release_set_device(release, device);
  fu_release_set_request(release, helper->request);
  if (helper->remote_id != NULL) {
    fu_release_set_remote(
        release, fu_engine_get_remote_by_id(engine, helper->remote_id, NULL));
  }
  if (!fu_release_load(release, helper->cabinet, component, NULL,
                       helper->flags | FWUPD_INSTALL_FLAG_FORCE,
                       &error_local)) {
    g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
    return TRUE;
  }
  if (!fu_engine_requirements_check(
          engine, release,
          helper->flags | FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS,
          &error_local)) {
    g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
    return TRUE;
  }

  fu_device_ensure_from_component(device, component);
  fu_device_incorporate_from_component(device, component);

  if (!fu_release_check_version(release, component, helper->flags,
                                &error_local)) {
    g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
    return TRUE;
  }

  releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
  if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES)) {
    g_autoptr(GPtrArray) rels = NULL;
    g_autoptr(XbQuery) query = NULL;

    g_ptr_array_add(releases, g_object_ref(release));

    query = xb_query_new_full(xb_node_get_silo(component), "releases/release",
                              XB_QUERY_FLAG_FORCE_NODE_CACHE, error);
    if (query == NULL) return FALSE;

    rels = xb_node_query_full(component, query, NULL);
    for (guint i = 1; i < rels->len; i++) {
      XbNode* rel = g_ptr_array_index(rels, i);
      g_autoptr(FuRelease) release2 = fu_release_new();
      g_autoptr(GError) error_loop = NULL;
      fu_release_set_device(release2, device);
      fu_release_set_request(release2, helper->request);
      if (!fu_release_load(release2, helper->cabinet, component, rel,
                           helper->flags, &error_loop)) {
        g_ptr_array_add(helper->errors, g_steal_pointer(&error_loop));
        continue;
      }
      g_ptr_array_add(releases, g_object_ref(release2));
    }
  } else {
    g_ptr_array_add(releases, g_object_ref(release));
  }

  for (guint i = 0; i < releases->len; i++) {
    FuRelease* release_tmp = g_ptr_array_index(releases, i);
    if (!fu_engine_requirements_check(engine, release_tmp, helper->flags,
                                      &error_local)) {
      g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
      continue;
    }
    if (!fu_engine_check_trust(engine, release_tmp, &error_local)) {
      g_ptr_array_add(helper->errors, g_steal_pointer(&error_local));
      continue;
    }

    if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED)) {
      const gchar* action_id = fu_release_get_action_id(release_tmp);
      if (!g_ptr_array_find(helper->action_ids, action_id, NULL))
        g_ptr_array_add(helper->action_ids, g_strdup(action_id));
    }
    g_ptr_array_add(helper->releases, g_object_ref(release_tmp));
  }

  return TRUE;
}

static gboolean fu_dbus_daemon_install_with_helper(FuMainAuthHelper* helper,
                                                   GError** error) {
  g_autoptr(GPtrArray) components = NULL;
  g_autoptr(GPtrArray) devices_possible = NULL;
  FuEngine* engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

  if (g_strcmp0(helper->device_id, FWUPD_DEVICE_ID_ANY) == 0) {
    devices_possible = fu_engine_get_devices(engine, error);
    if (devices_possible == NULL) return FALSE;
  } else {
    g_autoptr(FuDevice) device = NULL;
    device = fu_engine_get_device(engine, helper->device_id, error);
    if (device == NULL) return FALSE;
    devices_possible = fu_engine_get_devices_by_composite_id(
        engine, fu_device_get_composite_id(device), error);
    if (devices_possible == NULL) return FALSE;
  }

  helper->cabinet =
      fu_engine_build_cabinet_from_stream(engine, helper->stream, error);
  if (helper->cabinet == NULL) return FALSE;

  components = fu_cabinet_get_components(helper->cabinet, error);
  if (components == NULL) return FALSE;

  helper->action_ids = g_ptr_array_new_with_free_func(g_free);
  helper->releases =
      g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
  helper->errors = g_ptr_array_new_with_free_func((GDestroyNotify)g_error_free);
  helper->remote_id =
      fu_engine_get_remote_id_for_stream(engine, helper->stream);

  for (guint i = 0; i < components->len; i++) {
    XbNode* component = g_ptr_array_index(components, i);
    for (guint j = 0; j < devices_possible->len; j++) {
      FuDevice* device = g_ptr_array_index(devices_possible, j);
      if (!fu_dbus_daemon_install_with_helper_device(helper, component, device,
                                                     error))
        return FALSE;
    }
  }

  g_ptr_array_sort(helper->releases, fu_binder_daemon_release_sort_cb);

  if (helper->releases->len == 0) {
    GError* error_tmp = fu_engine_error_array_get_best(helper->errors);
    g_propagate_error(error, error_tmp);
    return FALSE;
  }

  return TRUE;
}

static gboolean fu_dbus_daemon_authorize_install_queue(
    FuMainAuthHelper* helper_ref, GError** error) {
  FuBinderDaemon* self = helper_ref->self;
  g_autoptr(FuMainAuthHelper) helper = helper_ref;
  gboolean ret;
  FuEngine* engine = fu_daemon_get_engine(FU_DAEMON(helper->self));

  fu_progress_set_profile(helper->progress, g_getenv("FWUPD_VERBOSE") != NULL);
  g_signal_connect(FU_PROGRESS(helper->progress), "percentage-changed",
                   G_CALLBACK(fu_binder_daemon_progress_percentage_changed_cb),
                   helper->self);
  g_signal_connect(FU_PROGRESS(helper->progress), "status-changed",
                   G_CALLBACK(fu_binder_daemon_progress_status_changed_cb),
                   helper->self);

  fu_daemon_set_update_in_progress(FU_DAEMON(self), TRUE);

  ret = fu_engine_install_releases(engine, helper->request, helper->releases,
                                   helper->cabinet, helper->progress,
                                   helper->flags, error);

  fu_daemon_set_update_in_progress(FU_DAEMON(self), FALSE);

  if (fu_daemon_get_pending_stop(FU_DAEMON(self))) {
    g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
                        "daemon was stopped");
    return FALSE;
  }

  return ret;
}

gboolean fu_binder_daemon_perform_install_bridge(void* daemon_instance,
                                                 const char* device_id,
                                                 int fd_handle,
                                                 GVariant* options,
                                                 GError** error) {
  FuBinderDaemon* self = FU_BINDER_DAEMON(daemon_instance);
  FuEngine* engine = fu_daemon_get_engine(FU_DAEMON(self));
  g_autoptr(FuMainAuthHelper) helper = NULL;
  g_autoptr(GVariantIter) iter = NULL;
  const gchar* prop_key;
  GVariant* prop_value;

  g_debug("Starting install via AIDL bridge for device: %s", device_id);

  helper = g_new0(FuMainAuthHelper, 1);
  helper->request = fu_binder_daemon_create_request(self);
  helper->progress = fu_progress_new(G_STRLOC);
  helper->device_id = g_strdup(device_id);
  helper->self = self;

  if (options != NULL) {
    iter = g_variant_iter_new(options);
    while (g_variant_iter_next(iter, "{&sv}", &prop_key, &prop_value)) {
      if (g_strcmp0(prop_key, "install-flags") == 0)
        helper->flags = g_variant_get_uint64(prop_value);

      if (g_strcmp0(prop_key, "allow-older") == 0 &&
          g_variant_get_boolean(prop_value))
        helper->flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
      if (g_strcmp0(prop_key, "allow-reinstall") == 0 &&
          g_variant_get_boolean(prop_value))
        helper->flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
      if (g_strcmp0(prop_key, "allow-branch-switch") == 0 &&
          g_variant_get_boolean(prop_value))
        helper->flags |= FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH;

      g_variant_unref(prop_value);
    }
  }

  helper->stream = fu_unix_seekable_input_stream_new(fd_handle, TRUE, error);
  if (helper->stream == NULL) {
    g_prefix_error(error, "invalid stream: ");
    return FALSE;
  }

  FuContext* ctx = fu_engine_get_context(engine);
  if (fu_context_get_config_bool(ctx, "IgnoreRequirements")) {
    helper->flags |= FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS;
  }

  if (!fu_dbus_daemon_install_with_helper(helper, error)) {
    return FALSE;
  }

  if (!fu_dbus_daemon_authorize_install_queue(g_steal_pointer(&helper),
                                              error)) {
    return FALSE;
  }

  return TRUE;
}
#else
gboolean fu_binder_daemon_perform_install_bridge(void* daemon_instance,
                                                 const char* device_id,
                                                 int fd_handle,
                                                 GVariant* options,
                                                 GError** error) {
  g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
                      "unsupported feature (HAVE_GIO_UNIX missing)");
  return FALSE;
}
#endif

/* --- Signal Definitions --- */

static void fu_binder_daemon_engine_changed_cb(FuEngine* engine,
                                               FuBinderDaemon* self) {
  g_debug("Emitting: changed");
  fu_binder_bridge_emit_changed();
  fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void fu_binder_daemon_engine_device_added_cb(FuEngine* engine,
                                                    FuDevice* device,
                                                    FuBinderDaemon* self) {
  g_debug("Emitting: device_added");
  g_autoptr(GVariant) val =
      fwupd_codec_to_variant(FWUPD_CODEC(device), FWUPD_CODEC_FLAG_NONE);
  fu_binder_bridge_emit_device_added(val);
  fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void fu_binder_daemon_engine_device_removed_cb(FuEngine* engine,
                                                      FuDevice* device,
                                                      FuBinderDaemon* self) {
  g_debug("Emitting: device_removed");
  g_autoptr(GVariant) val =
      fwupd_codec_to_variant(FWUPD_CODEC(device), FWUPD_CODEC_FLAG_NONE);
  fu_binder_bridge_emit_device_removed(val);
  fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void fu_binder_daemon_engine_device_changed_cb(FuEngine* engine,
                                                      FuDevice* device,
                                                      FuBinderDaemon* self) {
  g_debug("Emitting: device_changed");
  g_autoptr(GVariant) val =
      fwupd_codec_to_variant(FWUPD_CODEC(device), FWUPD_CODEC_FLAG_NONE);
  fu_binder_bridge_emit_device_changed(val);
  fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void fu_binder_daemon_engine_device_request_cb(FuEngine* engine,
                                                      FwupdRequest* request,
                                                      FuBinderDaemon* self) {
  g_debug("Emitting: device_request");
  g_autoptr(GVariant) val =
      fwupd_codec_to_variant(FWUPD_CODEC(request), FWUPD_CODEC_FLAG_NONE);
  fu_binder_bridge_emit_device_request(val);
  fu_daemon_schedule_housekeeping(FU_DAEMON(self));
}

static void fu_binder_daemon_engine_status_changed_cb(FuEngine* engine,
                                                      FwupdStatus status,
                                                      FuBinderDaemon* self) {
  fu_binder_daemon_set_status(self, status);

  g_autoptr(GError) error = NULL;
  const gchar* props[] = {"Status", "Percentage", NULL};
  g_autoptr(GVariant) val =
      fu_binder_daemon_get_properties_as_variant(self, props, &error);
  fu_binder_bridge_emit_properties_changed(val);

  if (status == FWUPD_STATUS_SHUTDOWN) fu_daemon_stop(FU_DAEMON(self), NULL);
}

/* --- Initialization --- */

static void fu_binder_daemon_init(FuBinderDaemon* self) {
  self->event_listener_binders = g_ptr_array_new_with_free_func(g_object_unref);
}

/* Global tracker to pass the context into the C++ bridge */
void* g_daemon_instance = NULL;

static gboolean fu_binder_daemon_setup(FuDaemon* daemon, const gchar* address,
                                       FuProgress* progress, GError** error) {
  FuBinderDaemon* self = FU_BINDER_DAEMON(daemon);
  FuEngine* engine = fu_daemon_get_engine(daemon);
  g_autoptr(GSource) source = NULL;

  /* Save the daemon context so C++ can trigger install logic */
  g_daemon_instance = self;

  fu_progress_set_id(progress, G_STRLOC);
  fu_progress_set_profile(progress, g_getenv("FWUPD_VERBOSE") != NULL);
  fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 100, "load-engine");

  if (!fu_engine_load(
          engine,
          FU_ENGINE_LOAD_FLAG_COLDPLUG | FU_ENGINE_LOAD_FLAG_HWINFO |
              FU_ENGINE_LOAD_FLAG_REMOTES |
              FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS |
              FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS |
              FU_ENGINE_LOAD_FLAG_ENSURE_CLIENT_CERT |
              FU_ENGINE_LOAD_FLAG_PATH_STORE_DEFAULTS |
              FU_ENGINE_LOAD_FLAG_DEVICE_HOTPLUG | FU_ENGINE_LOAD_FLAG_HISTORY,
          fu_progress_get_child(progress), error)) {
    return FALSE;
  }
  fu_progress_step_done(progress);

  g_signal_connect(engine, "changed",
                   G_CALLBACK(fu_binder_daemon_engine_changed_cb), self);
  g_signal_connect(engine, "device-added",
                   G_CALLBACK(fu_binder_daemon_engine_device_added_cb), self);
  g_signal_connect(engine, "device-removed",
                   G_CALLBACK(fu_binder_daemon_engine_device_removed_cb), self);
  g_signal_connect(engine, "device-changed",
                   G_CALLBACK(fu_binder_daemon_engine_device_changed_cb), self);
  g_signal_connect(engine, "device-request",
                   G_CALLBACK(fu_binder_daemon_engine_device_request_cb), self);
  g_signal_connect(engine, "status-changed",
                   G_CALLBACK(fu_binder_daemon_engine_status_changed_cb), self);

  fu_binder_setup_aidl_service(self);

  ABinderProcess_setupPolling(&self->binder_fd);
  source = fu_binder_fd_source_new(self->binder_fd);
  g_source_attach(source, NULL);

  return TRUE;
}

static void fu_binder_daemon_finalize(GObject* obj) {
  FuBinderDaemon* self = FU_BINDER_DAEMON(obj);
  if (self->event_listener_binders != NULL)
    g_ptr_array_unref(self->event_listener_binders);
  G_OBJECT_CLASS(fu_binder_daemon_parent_class)->finalize(obj);
}

static void fu_binder_daemon_class_init(FuBinderDaemonClass* klass) {
  GObjectClass* object_class = G_OBJECT_CLASS(klass);
  FuDaemonClass* daemon_class = FU_DAEMON_CLASS(klass);
  object_class->finalize = fu_binder_daemon_finalize;
  daemon_class->setup = fu_binder_daemon_setup;
}

FuDaemon* fu_daemon_new(void) {
  return FU_DAEMON(g_object_new(FU_TYPE_BINDER_DAEMON, NULL));
}
