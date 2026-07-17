/*
 * Copyright 2026 Harsha Muttavarapu <harshams@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

 #include "config.h"

#include <aidl/org/freedesktop/fwupd/BnFwupdEventListener.h>
#include <aidl/org/freedesktop/fwupd/IFwupd.h>
#include <aidl/org/freedesktop/fwupd/FwupdDevice.h>
#include <aidl/org/freedesktop/fwupd/FwupdHwid.h>
#include <aidl/org/freedesktop/fwupd/FwupdInstallOptions.h>
#include <aidl/org/freedesktop/fwupd/FwupdInstallRequest.h>
#include <aidl/org/freedesktop/fwupd/FwupdProperties.h>
#include <aidl/org/freedesktop/fwupd/FwupdRemote.h>
#include <aidl/org/freedesktop/fwupd/FwupdRequest.h>
#include <aidl/org/freedesktop/fwupd/FwupdUpdate.h>

#include <android/binder_auto_utils.h>
#include <android/binder_ibinder.h>
#include <android/binder_process.h>
#include <dlfcn.h>
#include <fwupd-enums.h>
#include <glib.h>
#include <unistd.h>
#include <vector>
#include <fwupd-error.h>
#include "fu-binder-client-bridge.h"

namespace aidl_fwupd = aidl::org::freedesktop::fwupd;

extern "C" const gchar* fwupd_status_to_string(int status);

static GVariant* AidlDeviceToGVariant(const aidl_fwupd::FwupdDevice& d) {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);

  if (!d.deviceId.empty())
    g_variant_builder_add(&builder, "{sv}", "DeviceId",
                          g_variant_new_string(d.deviceId.c_str()));
  if (!d.name.empty())
    g_variant_builder_add(&builder, "{sv}", "Name",
                          g_variant_new_string(d.name.c_str()));
  if (!d.version.empty())
    g_variant_builder_add(&builder, "{sv}", "Version",
                          g_variant_new_string(d.version.c_str()));
  if (!d.plugin.empty())
    g_variant_builder_add(&builder, "{sv}", "Plugin",
                          g_variant_new_string(d.plugin.c_str()));

  if (d.summary.has_value() && !d.summary.value().empty()) {
    g_variant_builder_add(&builder, "{sv}", "Summary",
                          g_variant_new_string(d.summary.value().c_str()));
  }
  if (d.vendor.has_value() && !d.vendor.value().empty()) {
    g_variant_builder_add(&builder, "{sv}", "Vendor",
                          g_variant_new_string(d.vendor.value().c_str()));
  }

  if (d.flags != 0)
    g_variant_builder_add(&builder, "{sv}", "Flags",
                          g_variant_new_uint64(d.flags));
  if (d.trustFlags != 0)
    g_variant_builder_add(&builder, "{sv}", "TrustFlags",
                          g_variant_new_uint64(d.trustFlags));

  return g_variant_builder_end(&builder);
}

static GVariant* AidlUpdateToGVariant(const aidl_fwupd::FwupdUpdate& u) {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);

  if (!u.remoteId.empty())
    g_variant_builder_add(&builder, "{sv}", "RemoteId",
                          g_variant_new_string(u.remoteId.c_str()));
  if (!u.name.empty())
    g_variant_builder_add(&builder, "{sv}", "Name",
                          g_variant_new_string(u.name.c_str()));
  if (!u.version.empty())
    g_variant_builder_add(&builder, "{sv}", "Version",
                          g_variant_new_string(u.version.c_str()));
  if (!u.filename.empty())
    g_variant_builder_add(&builder, "{sv}", "Filename",
                          g_variant_new_string(u.filename.c_str()));

  if (u.checksum.has_value() && !u.checksum.value().empty()) {
    g_variant_builder_add(&builder, "{sv}", "Checksum",
                          g_variant_new_string(u.checksum.value().c_str()));
  }

  if (u.appstreamId.has_value() && !u.appstreamId.value().empty()) {
    g_variant_builder_add(&builder, "{sv}", "AppstreamId",
                          g_variant_new_string(u.appstreamId.value().c_str()));
  }

  if (u.summary.has_value() && !u.summary.value().empty()) {
    g_variant_builder_add(&builder, "{sv}", "Summary",
                          g_variant_new_string(u.summary.value().c_str()));
  }
  if (u.description.has_value() && !u.description.value().empty()) {
    g_variant_builder_add(&builder, "{sv}", "Description",
                          g_variant_new_string(u.description.value().c_str()));
  }

  if (u.locations.has_value() && !u.locations.value().empty()) {
    GVariantBuilder strv_builder;
    g_variant_builder_init(&strv_builder, G_VARIANT_TYPE_STRING_ARRAY);

    for (const auto& loc : u.locations.value()) {
      if (loc.has_value()) {
        g_variant_builder_add(&strv_builder, "s", loc.value().c_str());
      }
    }
    g_variant_builder_add(&builder, "{sv}", "Locations",
                          g_variant_builder_end(&strv_builder));
  }

  if (u.flags != 0)
    g_variant_builder_add(&builder, "{sv}", "Flags",
                          g_variant_new_uint64(u.flags));
  if (u.trustFlags != 0)
    g_variant_builder_add(&builder, "{sv}", "TrustFlags",
                          g_variant_new_uint64(u.trustFlags));
  if (u.size != 0)
    g_variant_builder_add(&builder, "{sv}", "Size",
                          g_variant_new_uint64(u.size));

  return g_variant_builder_end(&builder);
}

static GVariant* AidlRemoteToGVariant(const aidl_fwupd::FwupdRemote& r) {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);

  if (r.id.has_value() && !r.id.value().empty())
    g_variant_builder_add(&builder, "{sv}", "RemoteId",
                          g_variant_new_string(r.id.value().c_str()));
  if (r.title.has_value() && !r.title.value().empty())
    g_variant_builder_add(&builder, "{sv}", "Title",
                          g_variant_new_string(r.title.value().c_str()));
  if (r.metadataUri.has_value() && !r.metadataUri.value().empty())
    g_variant_builder_add(&builder, "{sv}", "MetadataURI",
                          g_variant_new_string(r.metadataUri.value().c_str()));

  g_variant_builder_add(&builder, "{sv}", "Enabled",
                        g_variant_new_boolean(r.enabled));

  if (r.flags != 0)
    g_variant_builder_add(&builder, "{sv}", "Flags",
                          g_variant_new_uint64(r.flags));

  return g_variant_builder_end(&builder);
}

/* --- 1. Define the Listener Object --- */
class FwupdEventListenerImpl : public aidl_fwupd::BnFwupdEventListener {
 public:
  ::ndk::ScopedAStatus onChanged() override {
    g_debug("Engine state changed.\n");
    return ::ndk::ScopedAStatus::ok();
  }

  ::ndk::ScopedAStatus onDeviceAdded(
      const aidl_fwupd::FwupdDevice& /*device*/) override {
    g_print("Device added.\n");
    return ::ndk::ScopedAStatus::ok();
  }

  ::ndk::ScopedAStatus onDeviceRemoved(
      const aidl_fwupd::FwupdDevice& /*device*/) override {
    g_print("Device removed.\n");
    return ::ndk::ScopedAStatus::ok();
  }

  ::ndk::ScopedAStatus onDeviceChanged(
      const aidl_fwupd::FwupdDevice& /*device*/) override {
    g_print("Device changed.\n");
    return ::ndk::ScopedAStatus::ok();
  }

  ::ndk::ScopedAStatus onDeviceRequest(
      const aidl_fwupd::FwupdRequest& /*request*/) override {
    g_print("Device request received.\n");
    return ::ndk::ScopedAStatus::ok();
  }

  ::ndk::ScopedAStatus onPropertiesChanged(
      const aidl_fwupd::FwupdProperties& properties) override {
    if (properties.status != 0) {
      g_print("daemon status is %s\n",
              fwupd_status_to_string(properties.status));
    }
    if (properties.percentage > 0) {
      g_print("install progress is %d%%\n", properties.percentage);
    }
    return ::ndk::ScopedAStatus::ok();
  }
};

AIBinder* fu_binder_client_get_service_handle_aidl(GError** error) {
  void* handle = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "Failed to dlopen libbinder_ndk.so");
    return nullptr;
  }

  auto get_svc =
      (AIBinder * (*)(const char*)) dlsym(handle, "AServiceManager_getService");
  AIBinder* binder =
      get_svc ? get_svc("org.freedesktop.fwupd.IFwupd/default") : nullptr;
  dlclose(handle);

  if (!binder) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "AServiceManager could not find fwupd daemon");
  }
  return binder;
}

GVariant* fu_binder_client_get_devices_aidl(AIBinder* binder, GError** error) {
  if (binder == nullptr) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "Binder handle is null");
    return nullptr;
  }

  AIBinder_incStrong(binder);
  ::ndk::SpAIBinder spBinder;
  spBinder.set(binder);
  auto service = aidl_fwupd::IFwupd::fromBinder(spBinder);

  if (!service) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "Failed to cast Binder to IFwupd interface");
    return nullptr;
  }

  std::vector<aidl_fwupd::FwupdDevice> aidl_devs;
  auto status = service->getDevices(&aidl_devs);

  if (status.isOk()) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("aa{sv}"));
    for (const auto& dev : aidl_devs) {
      g_variant_builder_add_value(&builder, AidlDeviceToGVariant(dev));
    }
    return g_variant_builder_end(&builder);

  } else {
    if (status.getExceptionCode() == EX_SERVICE_SPECIFIC) {
      g_set_error_literal(error, FWUPD_ERROR, status.getServiceSpecificError(),
                          status.getMessage());
    } else {
      g_set_error(error, g_quark_from_string("FwupdBinder"), status.getStatus(),
                  "Binder transaction failed: %s",
                  status.getDescription().c_str());
    }
    return nullptr;
  }
}

GVariant* fu_binder_client_get_upgrades_aidl(AIBinder* binder,
                                             const char* device_id,
                                             GError** error) {
  if (binder == nullptr) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "Binder handle is null");
    return nullptr;
  }

  AIBinder_incStrong(binder);
  ::ndk::SpAIBinder spBinder;
  spBinder.set(binder);
  auto service = aidl_fwupd::IFwupd::fromBinder(spBinder);

  if (!service) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "Failed to cast Binder to IFwupd interface");
    return nullptr;
  }

  std::vector<aidl_fwupd::FwupdUpdate> aidl_rels;
  auto status = service->getUpdates(std::string(device_id), &aidl_rels);

  if (status.isOk()) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("aa{sv}"));
    for (const auto& rel : aidl_rels) {
      g_variant_builder_add_value(&builder, AidlUpdateToGVariant(rel));
    }
    return g_variant_builder_end(&builder);
  } else {
    if (status.getExceptionCode() == EX_SERVICE_SPECIFIC) {
      g_set_error_literal(error, FWUPD_ERROR, status.getServiceSpecificError(),
                          status.getMessage());
    } else {
      g_set_error(error, g_quark_from_string("FwupdBinder"), status.getStatus(),
                  "Binder transaction failed: %s",
                  status.getDescription().c_str());
    }
    return nullptr;
  }
}

extern "C" GVariant* fu_binder_client_get_remotes_aidl(AIBinder* binder, GError** error) {
  if (binder == nullptr) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "Binder handle is null");
    return nullptr;
  }

  AIBinder_incStrong(binder);
  ::ndk::SpAIBinder spBinder;
  spBinder.set(binder);
  auto service = aidl_fwupd::IFwupd::fromBinder(spBinder);

  if (!service) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "Failed to cast Binder to IFwupd interface");
    return nullptr;
  }

  std::vector<aidl_fwupd::FwupdRemote> aidl_remotes;
  auto status = service->getRemotes(&aidl_remotes);

  if (status.isOk()) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("aa{sv}"));
    for (const auto& r : aidl_remotes) {
      g_variant_builder_add_value(&builder, AidlRemoteToGVariant(r));
    }
    return g_variant_builder_end(&builder);
  } else {
    if (status.getExceptionCode() == EX_SERVICE_SPECIFIC) {
      g_set_error_literal(error, FWUPD_ERROR, status.getServiceSpecificError(),
                          status.getMessage());
    } else {
      g_set_error(error, g_quark_from_string("FwupdBinder"), status.getStatus(),
                  "Binder transaction failed: %s",
                  status.getDescription().c_str());
    }
    return nullptr;
  }
}

GVariant* fu_binder_client_get_hwids_aidl(AIBinder* binder, GError** error) {
  if (!binder) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "Binder handle is null");
    return nullptr;
  }

  std::shared_ptr<aidl_fwupd::IFwupd> proxy =
      aidl_fwupd::IFwupd::fromBinder(ndk::SpAIBinder(binder));

  if (!proxy) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "Failed to create IFwupd proxy");
    return nullptr;
  }

  std::vector<aidl_fwupd::FwupdHwid> aidl_hwids;
  ndk::ScopedAStatus status = proxy->getHwids(&aidl_hwids);

  if (!status.isOk()) {
    if (status.getExceptionCode() == EX_SERVICE_SPECIFIC) {
      g_set_error_literal(error, FWUPD_ERROR, status.getServiceSpecificError(),
                          status.getMessage());
    } else {
      g_set_error(error, g_quark_from_string("FwupdBinder"), status.getStatus(),
                  "getHwids AIDL call failed: %s",
                  status.getDescription().c_str());
    }
    return nullptr;
  }

  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
  for (const auto& hwid : aidl_hwids) {
    g_variant_builder_add(&builder, "{sv}", hwid.key.c_str(),
                          g_variant_new_string(hwid.value.c_str()));
  }
  return g_variant_builder_end(&builder);
}

bool fu_binder_client_setup_listener_aidl(AIBinder* binder_handle) {
  if (!binder_handle) return false;

  AIBinder_incStrong(binder_handle);
  ::ndk::SpAIBinder spBinder;
  spBinder.set(binder_handle);
  std::shared_ptr<aidl_fwupd::IFwupd> service =
      aidl_fwupd::IFwupd::fromBinder(spBinder);

  if (!service) {
    g_printerr("Failed to cast Binder to IFwupd interface for listener\n");
    return false;
  }

  std::shared_ptr<FwupdEventListenerImpl> listener =
      ::ndk::SharedRefBase::make<FwupdEventListenerImpl>();

  auto status = service->addEventListener(listener);
  if (!status.isOk()) {
    g_printerr("Failed to register AIDL listener with Daemon: %s\n",
               status.getDescription().c_str());
    return false;
  }

  void* handle = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
  if (handle) {
    auto start_thread_pool =
        (void (*)())dlsym(handle, "ABinderProcess_startThreadPool");
    if (start_thread_pool) {
      start_thread_pool();
    } else {
      g_printerr(
          "WARNING: Could not find ABinderProcess_startThreadPool in "
          "libbinder_ndk.so\n");
    }
    dlclose(handle);
  } else {
    g_printerr("WARNING: Failed to dlopen libbinder_ndk.so for thread pool.\n");
  }

  return true;
}

bool fu_binder_client_install_aidl(AIBinder* binder_handle, const char* id,
                                   int fd, GVariant* options, GError** error) {
  if (!binder_handle) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "Binder handle is null");
    return false;
  }

  AIBinder_incStrong(binder_handle);
  ::ndk::SpAIBinder spBinder;
  spBinder.set(binder_handle);
  std::shared_ptr<aidl_fwupd::IFwupd> service =
      aidl_fwupd::IFwupd::fromBinder(spBinder);

  if (!service) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "Failed to cast Binder to IFwupd");
    return false;
  }

  aidl_fwupd::FwupdInstallOptions strict_options;
  strict_options.force = false;
  strict_options.allowOlder = false;
  strict_options.allowReinstall = false;
  strict_options.allowBranchSwitch = false;

  if (options) {
    GVariant* val =
        g_variant_lookup_value(options, "install-flags", G_VARIANT_TYPE_UINT64);
    if (val) {
      uint64_t flags = g_variant_get_uint64(val);
      if (flags & FWUPD_INSTALL_FLAG_FORCE) strict_options.force = true;
      if (flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER)
        strict_options.allowOlder = true;
      if (flags & FWUPD_INSTALL_FLAG_ALLOW_REINSTALL)
        strict_options.allowReinstall = true;
      if (flags & FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH)
        strict_options.allowBranchSwitch = true;
      g_variant_unref(val);
    }
  }

  ::ndk::ScopedFileDescriptor sfd(dup(fd));

  if (sfd.get() < 0) {
    g_set_error(error, g_quark_from_string("FwupdBinder"), 0,
                "Failed to duplicate file descriptor for Binder");
    return false;
  }

  aidl_fwupd::FwupdInstallRequest req;
  req.id = std::string(id);
  req.firmwareFd = std::move(sfd);
  req.options = strict_options;

  auto status = service->install(req);

  if (!status.isOk()) {
    if (status.getExceptionCode() == EX_SERVICE_SPECIFIC) {
      g_set_error_literal(error, FWUPD_ERROR, status.getServiceSpecificError(),
                          status.getMessage());
    } else {
      g_set_error(error, g_quark_from_string("FwupdBinder"), status.getStatus(),
                  "Install failed: %s", status.getDescription().c_str());
    }
    return false;
  }

  return true;
}
