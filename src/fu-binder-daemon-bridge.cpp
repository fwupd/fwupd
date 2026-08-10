/*
 * Copyright 2026 Harsha Muttavarapu <harshams@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <aidl/org/freedesktop/fwupd/BnFwupd.h>
#include <aidl/org/freedesktop/fwupd/BnFwupdEventListener.h>

#include <android/binder_auto_utils.h>
#include <android/binder_manager.h>

#include <condition_variable>
#include <dlfcn.h>
#include <functional>
#include <glib.h>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <unistd.h>

#include "fu-binder-common.h"
#include "fu-binder-daemon-bridge.h"
#include "fu-binder-daemon.h"
#include "fu-context-private.h"

namespace aidl_fwupd = aidl::org::freedesktop::fwupd;

static std::vector<std::shared_ptr<aidl_fwupd::IFwupdEventListener>> g_listeners;
static std::mutex g_listeners_mutex;

static aidl_fwupd::FwupdProperties
FwupdProperties_to_AIDL(FuBinderDaemon *self)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	FuContext *ctx = fu_engine_get_context(engine);
	aidl_fwupd::FwupdProperties p;
	g_autofree gchar *host_security_id = fu_engine_get_host_security_id(engine, NULL);
	g_autofree gchar *host_bkc = fu_context_get_config_str(ctx, "HostBkc");
	p.daemonVersion = PACKAGE_VERSION;
	p.hostBkc = host_bkc;
	p.hostVendor = fu_engine_get_host_vendor(engine);
	p.hostProduct = fu_engine_get_host_product(engine);
	p.hostMachineId = fu_engine_get_host_machine_id(engine);
	p.hostSecurityId = host_security_id;
	p.tainted = FALSE;
	p.interactive = FALSE;
	p.onlyTrusted = fu_context_get_config_bool(ctx, "OnlyTrusted");
	p.status = fu_daemon_get_status(FU_DAEMON(self));
	p.percentage = fu_daemon_get_percentage(FU_DAEMON(self));
	p.batteryLevel = fu_context_get_battery_level(ctx);
	p.batteryThreshold = fu_context_get_battery_threshold(ctx);
	return p;
}

static std::vector<aidl_fwupd::FwupdHwid>
fu_binder_daemon_get_hwids_as_AIDL(FuBinderDaemon *self)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	FuContext *ctx = fu_engine_get_context(engine);
	FuHwids *hwids = fu_context_get_hwids(ctx);
	g_autoptr(GPtrArray) chid_keys = fu_hwids_get_chid_keys(hwids);
	g_autoptr(GPtrArray) hwid_keys = fu_hwids_get_keys(hwids);
	std::vector<aidl_fwupd::FwupdHwid> vec;

	for (guint i = 0; i < hwid_keys->len; i++) {
		const gchar *hwid_key = (const gchar *)g_ptr_array_index(hwid_keys, i);
		const gchar *value = fu_hwids_get_value(hwids, hwid_key);
		if (value == NULL)
			continue;
		aidl_fwupd::FwupdHwid hwid;
		hwid.key = hwid_key;
		hwid.value = value;
		vec.push_back(hwid);
	}
	for (guint i = 0; i < chid_keys->len; i++) {
		const gchar *key = (const gchar *)g_ptr_array_index(chid_keys, i);
		const gchar *keys = NULL;
		g_autofree gchar *guid = NULL;

		/* get the GUID */
		keys = fu_hwids_get_replace_keys(hwids, key);
		if (keys == NULL)
			continue;
		guid = fu_hwids_get_guid(hwids, key, NULL);
		if (guid == NULL)
			continue;
		aidl_fwupd::FwupdHwid hwid;
		hwid.key = keys;
		hwid.value = guid;
		vec.push_back(hwid);
	}
	return vec;
}

class FwupdBinderBridge : public aidl_fwupd::BnFwupd
{
	FuBinderDaemon *m_daemon;

      public:
	explicit FwupdBinderBridge(FuBinderDaemon *daemon) : m_daemon(daemon) {}

	::ndk::ScopedAStatus
	getDevices(std::vector<aidl_fwupd::FwupdDevice> *_aidl_return) override
	{
		FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(m_daemon));
		g_autoptr(GError) error = NULL;
		g_autoptr(GPtrArray) devices = NULL;

		devices = fu_engine_get_devices(engine, &error);
		if (devices == NULL) {
			return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
			    error->code,
			    error->message);
		}
		for (size_t i = 0; i < devices->len; i++) {
			FwupdDevice *device = FWUPD_DEVICE(g_ptr_array_index(devices, i));
			_aidl_return->push_back(fu_binder_device_to_aidl(device));
		}
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus
	install(const aidl_fwupd::FwupdInstallRequest &in_request) override
	{
		g_info("received install request for device %s", in_request.id.c_str());

		int engine_fd = dup(in_request.firmwareFd.get());
		if (engine_fd < 0) {
			return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
			    -1,
			    "Invalid File Descriptor received");
		}

		g_autoptr(GError) error = NULL;
		if (!fu_binder_daemon_perform_install_bridge(m_daemon,
							     in_request.id.c_str(),
							     engine_fd,
							     in_request.flags,
							     &error)) {
			std::string err_msg = error ? error->message : "Unknown engine error";
			int err_code = error ? error->code : -1;
			close(engine_fd);
			return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
			    err_code,
			    err_msg.c_str());
		}

		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus
	addEventListener(
	    const std::shared_ptr<aidl_fwupd::IFwupdEventListener> &in_listener) override
	{
		if (in_listener) {
			std::lock_guard<std::mutex> lock(g_listeners_mutex);
			g_listeners.push_back(in_listener);
			g_info("successfully registered a new client listener");
		}
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus
	getUpdates(const std::string &in_deviceId,
		   std::vector<aidl_fwupd::FwupdRelease> *_aidl_return) override
	{
		FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(m_daemon));
		g_autoptr(FuEngineRequest) request = fu_binder_daemon_create_request(m_daemon);
		g_autoptr(GError) error = NULL;
		g_autoptr(GPtrArray) releases = NULL;

		releases = fu_engine_get_upgrades(engine, request, in_deviceId.c_str(), &error);
		if (releases == NULL) {
			return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
			    error->code,
			    error->message);
		}
		for (guint i = 0; i < releases->len; i++) {
			FwupdRelease *release = FWUPD_RELEASE(g_ptr_array_index(releases, i));
			_aidl_return->push_back(fu_binder_release_to_aidl(release));
		}
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus
	getRemotes(std::vector<aidl_fwupd::FwupdRemote> *_aidl_return) override
	{
		FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(m_daemon));
		g_autoptr(GError) error = NULL;
		g_autoptr(GPtrArray) remotes = NULL;

		remotes = fu_engine_get_remotes(engine, &error);
		if (remotes == NULL) {
			return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
			    error->code,
			    error->message);
		}
		for (size_t i = 0; i < remotes->len; i++) {
			FwupdRemote *remote = FWUPD_REMOTE(g_ptr_array_index(remotes, i));
			_aidl_return->push_back(fu_binder_remote_to_aidl(remote));
		}
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus
	getHwids(std::vector<aidl_fwupd::FwupdHwid> *_aidl_return) override
	{
		*_aidl_return = fu_binder_daemon_get_hwids_as_AIDL(m_daemon);
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus
	getProperties(const std::vector<std::string> &in_properties,
		      aidl_fwupd::FwupdProperties *_aidl_return) override
	{
		std::vector<const gchar *> c_props;
		for (const auto &prop : in_properties) {
			c_props.push_back(prop.c_str());
		}
		c_props.push_back(nullptr);

		*_aidl_return = FwupdProperties_to_AIDL(m_daemon);
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus
	updateMetadata(const aidl_fwupd::FwupdMetadata &in_metadata) override
	{
		FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(m_daemon));
		gboolean ret;
		g_autoptr(GError) error = NULL;

		ret = fu_engine_update_metadata(engine,
						in_metadata.remoteId.c_str(),
						in_metadata.dataFd.get(),
						in_metadata.signatureFd.get(),
						&error);
		if (!ret) {
			std::string err_msg = error ? error->message
						    : "Unknown error updating metadata";
			return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
			    1,
			    err_msg.c_str());
		}
		return ::ndk::ScopedAStatus::ok();
	}
};

void
fu_binder_daemon_setup_aidl_service(FuBinderDaemon *daemon)
{
	static std::shared_ptr<FwupdBinderBridge> s_bridge;
	if (s_bridge == nullptr) {
		s_bridge = ::ndk::SharedRefBase::make<FwupdBinderBridge>(daemon);
	}

	void *handle = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
	if (handle) {
		typedef binder_status_t (*add_fn)(AIBinder *, const gchar *);
		auto add_service = (add_fn)dlsym(handle, "AServiceManager_addService");
		if (add_service) {
			add_service(s_bridge->asBinder().get(),
				    "org.freedesktop.fwupd.IFwupd/default");
		}
		dlclose(handle);
	}
}

template <typename Func>
void
broadcast_to_listeners(Func callback)
{
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

static void
event_worker_loop()
{
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

static void
enqueue_event(std::function<void()> task)
{
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

void
fu_binder_bridge_emit_device_added(FuBinderDaemon *self, FwupdDevice *device)
{
	aidl_fwupd::FwupdDevice d = fu_binder_device_to_aidl(device);
	g_return_if_fail(FU_IS_BINDER_DAEMON(self));
	g_return_if_fail(FWUPD_IS_DEVICE(device));
	enqueue_event([d]() {
		broadcast_to_listeners([d](auto &listener) { return listener->onDeviceAdded(d); });
	});
}

void
fu_binder_bridge_emit_device_removed(FuBinderDaemon *self, FwupdDevice *device)
{
	aidl_fwupd::FwupdDevice d = fu_binder_device_to_aidl(device);
	g_return_if_fail(FU_IS_BINDER_DAEMON(self));
	g_return_if_fail(FWUPD_IS_DEVICE(device));
	enqueue_event([d]() {
		broadcast_to_listeners(
		    [d](auto &listener) { return listener->onDeviceRemoved(d); });
	});
}

void
fu_binder_bridge_emit_device_changed(FuBinderDaemon *self, FwupdDevice *device)
{
	aidl_fwupd::FwupdDevice d = fu_binder_device_to_aidl(device);
	g_return_if_fail(FU_IS_BINDER_DAEMON(self));
	g_return_if_fail(FWUPD_IS_DEVICE(device));
	enqueue_event([d]() {
		broadcast_to_listeners(
		    [d](auto &listener) { return listener->onDeviceChanged(d); });
	});
}

void
fu_binder_bridge_emit_device_request(FuBinderDaemon *self, FwupdRequest *request)
{
	aidl_fwupd::FwupdRequest r = fu_binder_request_to_aidl(request);
	g_return_if_fail(FU_IS_BINDER_DAEMON(self));
	g_return_if_fail(FWUPD_IS_REQUEST(request));
	enqueue_event([r]() {
		broadcast_to_listeners(
		    [r](auto &listener) { return listener->onDeviceRequest(r); });
	});
}

void
fu_binder_bridge_emit_changed(FuBinderDaemon *self)
{
	aidl_fwupd::FwupdProperties p = FwupdProperties_to_AIDL(self);
	enqueue_event([p]() {
		broadcast_to_listeners(
		    [p](auto &listener) { return listener->onPropertiesChanged(p); });
	});
}
