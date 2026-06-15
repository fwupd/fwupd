#include "fu-binder-daemon-bridge.h"

#include <aidl/org/freedesktop/fwupd/BnFwupd.h>
#include <aidl/org/freedesktop/fwupd/BnFwupdEventListener.h>
#include <aidl/org/freedesktop/fwupd/FwupdDevice.h>
#include <aidl/org/freedesktop/fwupd/FwupdHwid.h>
#include <aidl/org/freedesktop/fwupd/FwupdInstallOptions.h>
#include <aidl/org/freedesktop/fwupd/FwupdInstallRequest.h>
#include <aidl/org/freedesktop/fwupd/FwupdMetadata.h>
#include <aidl/org/freedesktop/fwupd/FwupdProperties.h>
#include <aidl/org/freedesktop/fwupd/FwupdRemote.h>
#include <aidl/org/freedesktop/fwupd/FwupdRequest.h>
#include <aidl/org/freedesktop/fwupd/FwupdUpdate.h>
#include <android/binder_auto_utils.h>
#include <android/binder_manager.h>
#include <dlfcn.h>
#include <glib.h>
#include <unistd.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>

namespace aidl_fwupd = aidl::org::freedesktop::fwupd;

static std::vector<std::shared_ptr<aidl_fwupd::IFwupdEventListener>>
    g_listeners;
static std::mutex g_listeners_mutex;

/* --- Safe GVariant Extraction Helpers --- */
static std::string ExtractString(GVariant* dict, const char* key) {
  std::string ret = "";
  GVariant* val = g_variant_lookup_value(dict, key, G_VARIANT_TYPE_STRING);
  if (val) {
    ret = g_variant_get_string(val, NULL);
    g_variant_unref(val);
  }
  return ret;
}

// For @nullable String in AIDL
static std::optional<std::string> ExtractStringOpt(GVariant* dict,
                                                   const char* key) {
  GVariant* val = g_variant_lookup_value(dict, key, G_VARIANT_TYPE_STRING);
  if (val) {
    std::string ret = g_variant_get_string(val, NULL);
    g_variant_unref(val);
    return ret;
  }
  return std::nullopt;
}

static int64_t ExtractLong(GVariant* dict, const char* key) {
  int64_t ret = 0;
  GVariant* val = g_variant_lookup_value(dict, key, G_VARIANT_TYPE_UINT64);
  if (val) {
    ret = static_cast<int64_t>(g_variant_get_uint64(val));
    g_variant_unref(val);
    return ret;
  }

  val = g_variant_lookup_value(dict, key, G_VARIANT_TYPE_INT64);
  if (val) {
    ret = g_variant_get_int64(val);
    g_variant_unref(val);
  }
  return ret;
}

static int32_t ExtractInt(GVariant* dict, const char* key) {
  int32_t ret = 0;
  GVariant* val = g_variant_lookup_value(dict, key, G_VARIANT_TYPE_UINT32);
  if (val) {
    ret = static_cast<int32_t>(g_variant_get_uint32(val));
    g_variant_unref(val);
    return ret;
  }

  val = g_variant_lookup_value(dict, key, G_VARIANT_TYPE_INT32);
  if (val) {
    ret = g_variant_get_int32(val);
    g_variant_unref(val);
  }
  return ret;
}

// For @nullable String[] in AIDL (Nested Optionals)
static std::optional<std::vector<std::optional<std::string>>>
ExtractStringArrayOpt(GVariant* dict, const char* key) {
  GVariant* val =
      g_variant_lookup_value(dict, key, G_VARIANT_TYPE_STRING_ARRAY);
  if (val) {
    std::vector<std::optional<std::string>> vec;
    const gchar** strv = g_variant_get_strv(val, NULL);
    if (strv) {
      for (int i = 0; strv[i] != NULL; i++) {
        vec.push_back(std::string(strv[i]));
      }
      g_free(strv);
    }
    g_variant_unref(val);
    return vec;
  }
  return std::nullopt;
}

static bool ExtractBool(GVariant* dict, const char* key) {
  bool ret = false;
  GVariant* val = g_variant_lookup_value(dict, key, G_VARIANT_TYPE_BOOLEAN);
  if (val) {
    ret = g_variant_get_boolean(val);
    g_variant_unref(val);
  }
  return ret;
}

/* --- Map Parsers --- */
static aidl_fwupd::FwupdDevice GPVariantToFwupdDevice(GVariant* dict) {
  aidl_fwupd::FwupdDevice d;
  // Core (Mandatory std::string)
  d.deviceId = ExtractString(dict, "DeviceId");
  d.name = ExtractString(dict, "Name");
  d.version = ExtractString(dict, "Version");
  d.plugin = ExtractString(dict, "Plugin");

  // Arrays & Strings (Optional std::optional)
  d.summary = ExtractStringOpt(dict, "Summary");
  d.vendor = ExtractStringOpt(dict, "Vendor");
  d.protocols = ExtractStringArrayOpt(dict, "Protocols");
  d.icons = ExtractStringArrayOpt(dict, "Icons");

  // Primitives
  d.flags = ExtractLong(dict, "Flags");
  d.trustFlags = ExtractLong(dict, "TrustFlags");
  d.percentage = ExtractInt(dict, "Percentage");
  d.status = ExtractInt(dict, "Status");

  return d;
}

static aidl_fwupd::FwupdUpdate GPVariantToFwupdUpdate(GVariant* dict) {
  aidl_fwupd::FwupdUpdate u;
  // Mandatory
  u.remoteId = ExtractString(dict, "RemoteId");
  u.name = ExtractString(dict, "Name");
  u.version = ExtractString(dict, "Version");
  u.filename = ExtractString(dict, "Filename");

  // Optional
  u.summary = ExtractStringOpt(dict, "Summary");
  u.description = ExtractStringOpt(dict, "Description");
  u.locations = ExtractStringArrayOpt(dict, "Locations");
  u.checksum = ExtractStringOpt(dict, "Checksum");
  u.appstreamId = ExtractStringOpt(dict, "AppstreamId");

  // Primitives
  u.size = ExtractLong(dict, "Size");
  u.flags = ExtractLong(dict, "Flags");
  u.trustFlags = ExtractLong(dict, "TrustFlags");
  u.urgency = ExtractInt(dict, "Urgency");
  return u;
}

static aidl_fwupd::FwupdRemote GPVariantToFwupdRemote(GVariant* dict) {
  aidl_fwupd::FwupdRemote r;
  r.id = ExtractStringOpt(dict, "RemoteId");
  r.title = ExtractStringOpt(dict, "Title");
  r.metadataUri = ExtractStringOpt(dict, "MetadataURI");
  r.enabled = ExtractBool(dict, "Enabled");
  r.flags = ExtractLong(dict, "Flags");
  return r;
}

static aidl_fwupd::FwupdProperties GPVariantToFwupdProperties(GVariant* dict) {
  aidl_fwupd::FwupdProperties p;

  p.daemonVersion = ExtractStringOpt(dict, "DaemonVersion");
  p.hostBkc = ExtractStringOpt(dict, "HostBkc");
  p.hostVendor = ExtractStringOpt(dict, "HostVendor");
  p.hostProduct = ExtractStringOpt(dict, "HostProduct");
  p.hostMachineId = ExtractStringOpt(dict, "HostMachineId");
  p.hostSecurityId = ExtractStringOpt(dict, "HostSecurityId");

  // Booleans (Maps to boolean)
  p.tainted = ExtractBool(dict, "Tainted");
  p.interactive = ExtractBool(dict, "Interactive");
  p.onlyTrusted = ExtractBool(dict, "OnlyTrusted");

  // Integers (Maps to int)
  p.status = ExtractInt(dict, "Status");
  p.percentage = ExtractInt(dict, "Percentage");
  p.batteryLevel = ExtractInt(dict, "BatteryLevel");
  p.batteryThreshold = ExtractInt(dict, "BatteryThreshold");

  return p;
}

static aidl_fwupd::FwupdRequest GPVariantToFwupdRequest(GVariant* dict) {
  aidl_fwupd::FwupdRequest r;
  r.id = ExtractString(dict, "AppstreamId");
  r.kind = ExtractInt(dict, "RequestKind");
  r.message = ExtractStringOpt(dict, "Message");
  return r;
}

class FwupdBinderBridge : public aidl_fwupd::BnFwupd {
  FuBinderDaemon* m_daemon;

 public:
  explicit FwupdBinderBridge(FuBinderDaemon* daemon) : m_daemon(daemon) {}

  ::ndk::ScopedAStatus getDevices(
      std::vector<aidl_fwupd::FwupdDevice>* _aidl_return) override {
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) val =
        fu_binder_daemon_get_devices_as_variant(m_daemon, &error);

    if (val == NULL) {
      if (error != NULL) {
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
            error->code, error->message);
      }
      return ::ndk::ScopedAStatus::ok();
    }

    GVariant* device_array = val;
    if (g_variant_is_of_type(val, G_VARIANT_TYPE_TUPLE)) {
      device_array = g_variant_get_child_value(val, 0);
    }

    size_t n_children = g_variant_n_children(device_array);
    for (size_t i = 0; i < n_children; i++) {
      GVariant* child = g_variant_get_child_value(device_array, i);
      _aidl_return->push_back(GPVariantToFwupdDevice(child));
      g_variant_unref(child);
    }
    return ::ndk::ScopedAStatus::ok();
  }

  // FIX 3: Updated signature to accept FwupdInstallRequest struct
  ::ndk::ScopedAStatus install(
      const aidl_fwupd::FwupdInstallRequest& in_request) override {
    g_printerr("DAEMON [BINDER]: Received install request for device %s\n",
               in_request.id.c_str());

    int engine_fd = dup(in_request.firmwareFd.get());
    if (engine_fd < 0) {
      return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
          -1, "Invalid File Descriptor received");
    }

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    if (in_request.options.force)
      g_variant_builder_add(&builder, "{sv}", "force",
                            g_variant_new_boolean(TRUE));
    if (in_request.options.allowOlder)
      g_variant_builder_add(&builder, "{sv}", "allow-older",
                            g_variant_new_boolean(TRUE));
    if (in_request.options.allowReinstall)
      g_variant_builder_add(&builder, "{sv}", "allow-reinstall",
                            g_variant_new_boolean(TRUE));
    if (in_request.options.allowBranchSwitch)
      g_variant_builder_add(&builder, "{sv}", "allow-branch-switch",
                            g_variant_new_boolean(TRUE));

    GVariant* dict = g_variant_builder_end(&builder);
    GError* error = nullptr;

    if (!fu_binder_daemon_perform_install_bridge(
            m_daemon, in_request.id.c_str(), engine_fd, dict, &error)) {
      std::string err_msg = error ? error->message : "Unknown engine error";
      int err_code = error ? error->code : -1;

      if (error) g_error_free(error);
      close(engine_fd);
      return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
          err_code, err_msg.c_str());
    }

    return ::ndk::ScopedAStatus::ok();
  }

  ::ndk::ScopedAStatus addEventListener(
      const std::shared_ptr<aidl_fwupd::IFwupdEventListener>& in_listener)
      override {
    if (in_listener) {
      std::lock_guard<std::mutex> lock(g_listeners_mutex);
      g_listeners.push_back(in_listener);
      g_printerr(
          "DAEMON [BINDER]: Successfully registered a new client listener.\n");
    }
    return ::ndk::ScopedAStatus::ok();
  }

  ::ndk::ScopedAStatus getUpdates(
      const std::string& in_deviceId,
      std::vector<aidl_fwupd::FwupdUpdate>* _aidl_return) override {
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) val = fu_binder_daemon_get_upgrades_as_variant(
        m_daemon, in_deviceId.c_str(), &error);

    if (val == NULL) {
      if (error != NULL) {
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
            error->code, error->message);
      }
      return ::ndk::ScopedAStatus::ok();
    }

    GVariant* release_array = val;
    if (g_variant_is_of_type(val, G_VARIANT_TYPE_TUPLE)) {
      release_array = g_variant_get_child_value(val, 0);
    }

    size_t n_children = g_variant_n_children(release_array);
    for (size_t i = 0; i < n_children; i++) {
      GVariant* child = g_variant_get_child_value(release_array, i);
      _aidl_return->push_back(GPVariantToFwupdUpdate(child));
      g_variant_unref(child);
    }
    return ::ndk::ScopedAStatus::ok();
  }

  ::ndk::ScopedAStatus getRemotes(
      std::vector<aidl_fwupd::FwupdRemote>* _aidl_return) override {
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) val =
        fu_binder_daemon_get_remotes_as_variant(m_daemon, &error);

    if (val == NULL) {
      if (error != NULL)
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
            error->code, error->message);
      return ::ndk::ScopedAStatus::ok();
    }

    GVariant* array_val = val;
    if (g_variant_is_of_type(val, G_VARIANT_TYPE_TUPLE)) {
      array_val = g_variant_get_child_value(val, 0);
    }

    size_t n_children = g_variant_n_children(array_val);
    for (size_t i = 0; i < n_children; i++) {
      GVariant* child = g_variant_get_child_value(array_val, i);
      _aidl_return->push_back(GPVariantToFwupdRemote(child));
      g_variant_unref(child);
    }

    if (array_val != val) g_variant_unref(array_val);
    return ::ndk::ScopedAStatus::ok();
  }

  ::ndk::ScopedAStatus getHwids(
      std::vector<aidl_fwupd::FwupdHwid>* _aidl_return) override {
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) val =
        fu_binder_daemon_get_hwids_as_variant(m_daemon, &error);

    if (val == NULL) {
      if (error != NULL)
        return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
            error->code, error->message);
      return ::ndk::ScopedAStatus::ok();
    }

    GVariant* dict_val = val;
    if (g_variant_is_of_type(val, G_VARIANT_TYPE_TUPLE)) {
      dict_val = g_variant_get_child_value(val, 0);
    }

    GVariantIter iter;
    const gchar* key;
    GVariant* child_val;
    g_variant_iter_init(&iter, dict_val);
    while (g_variant_iter_next(&iter, "{&sv}", &key, &child_val)) {
      aidl_fwupd::FwupdHwid hwid;
      hwid.key = key;
      hwid.value = g_variant_get_string(child_val, NULL);
      _aidl_return->push_back(std::move(hwid));
      g_variant_unref(child_val);
    }

    if (dict_val != val) g_variant_unref(dict_val);
    return ::ndk::ScopedAStatus::ok();
  }

  ::ndk::ScopedAStatus getProperties(
      const std::vector<std::string>& in_properties,
      aidl_fwupd::FwupdProperties* _aidl_return) override {
    std::vector<const gchar*> c_props;
    for (const auto& prop : in_properties) {
      c_props.push_back(prop.c_str());
    }
    c_props.push_back(nullptr);

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) val = fu_binder_daemon_get_properties_as_variant(
        m_daemon, c_props.data(), &error);

    if (val == NULL) return ::ndk::ScopedAStatus::ok();

    GVariant* dict_val = val;
    if (g_variant_is_of_type(val, G_VARIANT_TYPE_TUPLE)) {
      dict_val = g_variant_get_child_value(val, 0);
    }

    *_aidl_return = GPVariantToFwupdProperties(dict_val);

    if (dict_val != val) g_variant_unref(dict_val);
    return ::ndk::ScopedAStatus::ok();
  }

  // FIX 3: Updated signature to accept FwupdMetadata struct
  ::ndk::ScopedAStatus updateMetadata(
      const aidl_fwupd::FwupdMetadata& in_metadata) override {
    GError* error = nullptr;
    bool success = fu_binder_daemon_update_metadata_bridge(
        m_daemon, in_metadata.remoteId.c_str(), in_metadata.dataFd.get(),
        in_metadata.signatureFd.get(), &error);

    if (!success) {
      std::string err_msg =
          error ? error->message : "Unknown error updating metadata";
      if (error) g_error_free(error);
      return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
          1, err_msg.c_str());
    }
    return ::ndk::ScopedAStatus::ok();
  }
};

void fu_binder_setup_aidl_service(FuBinderDaemon* daemon) {
  static std::shared_ptr<FwupdBinderBridge> s_bridge;
  if (s_bridge == nullptr) {
    s_bridge = ::ndk::SharedRefBase::make<FwupdBinderBridge>(daemon);
  }

  void* handle = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
  if (handle) {
    typedef binder_status_t (*add_fn)(AIBinder*, const char*);
    auto add_service = (add_fn)dlsym(handle, "AServiceManager_addService");
    if (add_service) {
      add_service(s_bridge->asBinder().get(),
                  "org.freedesktop.fwupd.IFwupd/default");
    }
    dlclose(handle);
  }
}

template <typename Func>
void broadcast_to_listeners(Func callback) {
  std::lock_guard<std::mutex> lock(g_listeners_mutex);
  for (auto it = g_listeners.begin(); it != g_listeners.end();) {
    if (*it) {
      auto status = callback(*it);
      if (!status.isOk() && status.getStatus() == STATUS_DEAD_OBJECT) {
        it = g_listeners.erase(it);
        continue;
      }
    }
    ++it;
  }
}

static std::queue<std::function<void()>> g_event_queue;
static std::mutex g_event_mutex;
static std::condition_variable g_event_cv;
static bool g_event_thread_started = false;

static void event_worker_loop() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(g_event_mutex);
      g_event_cv.wait(lock, [] { return !g_event_queue.empty(); });
      task = g_event_queue.front();
      g_event_queue.pop();
    }
    task();
  }
}

static void enqueue_event(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(g_event_mutex);
    if (!g_event_thread_started) {
      std::thread(event_worker_loop).detach();
      g_event_thread_started = true;
    }
    g_event_queue.push(task);
  }
  g_event_cv.notify_one();
}


void fu_binder_bridge_emit_changed() {
  enqueue_event([]() {
    broadcast_to_listeners(
        [](auto& listener) { return listener->onChanged(); });
  });
}

void fu_binder_bridge_emit_device_added(GVariant* dict) {
  aidl_fwupd::FwupdDevice d;
  if (dict) d = GPVariantToFwupdDevice(dict);
  enqueue_event([d]() {
    broadcast_to_listeners(
        [d](auto& listener) { return listener->onDeviceAdded(d); });
  });
}

void fu_binder_bridge_emit_device_removed(GVariant* dict) {
  aidl_fwupd::FwupdDevice d;
  if (dict) d = GPVariantToFwupdDevice(dict);
  enqueue_event([d]() {
    broadcast_to_listeners(
        [d](auto& listener) { return listener->onDeviceRemoved(d); });
  });
}

void fu_binder_bridge_emit_device_changed(GVariant* dict) {
  aidl_fwupd::FwupdDevice d;
  if (dict) d = GPVariantToFwupdDevice(dict);
  enqueue_event([d]() {
    broadcast_to_listeners(
        [d](auto& listener) { return listener->onDeviceChanged(d); });
  });
}

void fu_binder_bridge_emit_device_request(GVariant* dict) {
  aidl_fwupd::FwupdRequest r;
  if (dict) r = GPVariantToFwupdRequest(dict);
  enqueue_event([r]() {
    broadcast_to_listeners(
        [r](auto& listener) { return listener->onDeviceRequest(r); });
  });
}

void fu_binder_bridge_emit_properties_changed(GVariant* dict) {
  aidl_fwupd::FwupdProperties p;
  if (dict) p = GPVariantToFwupdProperties(dict);
  enqueue_event([p]() {
    broadcast_to_listeners(
        [p](auto& listener) { return listener->onPropertiesChanged(p); });
  });
}
