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

#include <algorithm>
#include <condition_variable>
#include <dlfcn.h>
#include <functional>
#include <gio/gunixoutputstream.h>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <unistd.h>
#include <vector>

#include "fu-binder-common.h"
#include "fu-binder-daemon-bridge.h"
#include "fu-binder-daemon.h"
#include "fu-context-private.h"

namespace aidl_fwupd = aidl::org::freedesktop::fwupd;

/* qdata key used to attach the (single) bridge instance to the daemon object */
#define FU_BINDER_BRIDGE_QDATA_KEY "fu-binder-bridge"

/* bound the queue so a stuck or slow listener cannot make it grow without limit */
#define FU_BINDER_EVENT_QUEUE_MAX 1024

static aidl_fwupd::FwupdProperties
FwupdProperties_to_AIDL(FuBinderDaemon *self)
{
	FuEngine *engine = fu_daemon_get_engine(FU_DAEMON(self));
	FuContext *ctx = fu_engine_get_context(engine);
	aidl_fwupd::FwupdProperties p;
	const gchar *host_vendor = fu_engine_get_host_vendor(engine);
	const gchar *host_product = fu_engine_get_host_product(engine);
	const gchar *host_machine_id = fu_engine_get_host_machine_id(engine);
	g_autofree gchar *host_security_id = fu_engine_get_host_security_id(engine, NULL);
	g_autofree gchar *host_bkc = fu_context_get_config_str(ctx, "HostBkc");
	p.daemonVersion = PACKAGE_VERSION;
	if (host_bkc != NULL)
		p.hostBkc = host_bkc;
	if (host_vendor != NULL)
		p.hostVendor = host_vendor;
	if (host_product != NULL)
		p.hostProduct = host_product;
	if (host_machine_id != NULL)
		p.hostMachineId = host_machine_id;
	if (host_security_id != NULL)
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

	/* registered client listeners, and the lock protecting them */
	std::vector<std::shared_ptr<aidl_fwupd::IFwupdEventListener>> m_listeners;
	std::mutex m_listeners_mutex;

	/* background worker used to deliver events to listeners without blocking
	 * the caller (typically the engine running on the main loop) */
	std::queue<std::function<void()>> m_event_queue;
	std::mutex m_event_mutex;
	std::condition_variable m_event_cv;
	std::thread m_event_thread;
	bool m_event_thread_started = false;
	bool m_event_stop = false;

	void
	event_worker_loop()
	{
		while (true) {
			std::function<void()> task;
			{
				std::unique_lock<std::mutex> lock(m_event_mutex);
				m_event_cv.wait(lock, [this] {
					return !m_event_queue.empty() || m_event_stop;
				});
				/* drain anything left, then exit once asked to stop */
				if (m_event_queue.empty() && m_event_stop)
					break;
				task = std::move(m_event_queue.front());
				m_event_queue.pop();
			}
			task();
		}
	}

	void
	enqueue_event(std::function<void()> task)
	{
		{
			std::lock_guard<std::mutex> lock(m_event_mutex);
			if (m_event_stop)
				return;
			if (!m_event_thread_started) {
				m_event_thread =
				    std::thread(&FwupdBinderBridge::event_worker_loop, this);
				m_event_thread_started = true;
			}
			/* drop the oldest event to keep the most recent state */
			if (m_event_queue.size() >= FU_BINDER_EVENT_QUEUE_MAX) {
				m_event_queue.pop();
				g_warning("Binder event queue full, dropping oldest event");
			}
			m_event_queue.push(std::move(task));
		}
		m_event_cv.notify_one();
	}

	template <typename Func>
	void
	broadcast_to_listeners(Func callback)
	{
		std::vector<std::shared_ptr<aidl_fwupd::IFwupdEventListener>> listeners;
		std::vector<std::shared_ptr<aidl_fwupd::IFwupdEventListener>> dead;

		/* take a snapshot so the (potentially blocking) Binder calls are made
		 * without the lock held */
		{
			std::lock_guard<std::mutex> lock(m_listeners_mutex);
			listeners = m_listeners;
		}

		/* perform the Binder calls outside the lock */
		for (auto &listener : listeners) {
			if (!listener)
				continue;
			auto status = callback(listener);
			if (!status.isOk() && status.getStatus() == STATUS_DEAD_OBJECT)
				dead.push_back(listener);
		}

		/* drop any listeners that died, under a second short lock */
		if (!dead.empty()) {
			std::lock_guard<std::mutex> lock(m_listeners_mutex);
			for (auto &listener : dead) {
				m_listeners.erase(
				    std::remove(m_listeners.begin(), m_listeners.end(), listener),
				    m_listeners.end());
			}
		}
	}

      public:
	explicit FwupdBinderBridge(FuBinderDaemon *daemon) : m_daemon(daemon) {}
	~FwupdBinderBridge() override { shutdown(); }

	/* stop and join the worker thread; safe to call more than once */
	void
	shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(m_event_mutex);
			if (!m_event_thread_started)
				return;
			m_event_stop = true;
		}
		m_event_cv.notify_all();
		if (m_event_thread.joinable())
			m_event_thread.join();
		m_event_thread_started = false;
	}

	binder_status_t
	dump(int fd, const char ** /*args*/, uint32_t /*numArgs*/) override
	{
		g_autofree gchar *json = fwupd_codec_to_json_string(FWUPD_CODEC(m_daemon),
								    FWUPD_CODEC_FLAG_TRUSTED,
								    NULL);
		if (json != NULL) {
			g_autoptr(GOutputStream) stream = g_unix_output_stream_new(fd, FALSE);
			g_autoptr(GError) error = NULL;
			if (!g_output_stream_write_all(stream,
						       json,
						       strlen(json),
						       NULL,
						       NULL,
						       &error))
				g_warning("failed to write dumpsys output: %s", error->message);
		}
		return STATUS_OK;
	}

	void
	emit_device_added(FwupdDevice *device)
	{
		aidl_fwupd::FwupdDevice d = fu_binder_device_to_aidl(device);
		enqueue_event([this, d]() {
			broadcast_to_listeners(
			    [d](auto &listener) { return listener->onDeviceAdded(d); });
		});
	}

	void
	emit_device_removed(FwupdDevice *device)
	{
		aidl_fwupd::FwupdDevice d = fu_binder_device_to_aidl(device);
		enqueue_event([this, d]() {
			broadcast_to_listeners(
			    [d](auto &listener) { return listener->onDeviceRemoved(d); });
		});
	}

	void
	emit_device_changed(FwupdDevice *device)
	{
		aidl_fwupd::FwupdDevice d = fu_binder_device_to_aidl(device);
		enqueue_event([this, d]() {
			broadcast_to_listeners(
			    [d](auto &listener) { return listener->onDeviceChanged(d); });
		});
	}

	void
	emit_device_request(FwupdRequest *request)
	{
		aidl_fwupd::FwupdRequest r = fu_binder_request_to_aidl(request);
		enqueue_event([this, r]() {
			broadcast_to_listeners(
			    [r](auto &listener) { return listener->onDeviceRequest(r); });
		});
	}

	void
	emit_changed()
	{
		aidl_fwupd::FwupdProperties p = FwupdProperties_to_AIDL(m_daemon);
		enqueue_event([this, p]() {
			broadcast_to_listeners(
			    [p](auto &listener) { return listener->onPropertiesChanged(p); });
		});
	}

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

		int engine_fd = in_request.firmwareFd.get();
		if (engine_fd < 0) {
			return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid file descriptor received");
		}

		g_autoptr(GError) error = NULL;
		if (!fu_binder_daemon_perform_install_bridge(m_daemon,
							     in_request.id.c_str(),
							     engine_fd,
							     in_request.flags,
							     &error)) {
			std::string err_msg = error ? error->message : "Unknown engine error";
			int err_code = error ? error->code : -1;
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
			std::lock_guard<std::mutex> lock(m_listeners_mutex);
			m_listeners.push_back(in_listener);
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
	getProperties(aidl_fwupd::FwupdProperties *_aidl_return) override
	{
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
			int err_code = error ? error->code : -1;
			return ::ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(
			    err_code,
			    err_msg.c_str());
		}
		return ::ndk::ScopedAStatus::ok();
	}
};

/* the bridge instance is owned by the daemon object via qdata */
static std::shared_ptr<FwupdBinderBridge>
fu_binder_bridge_get(FuBinderDaemon *self)
{
	auto *ptr = static_cast<std::shared_ptr<FwupdBinderBridge> *>(
	    g_object_get_data(G_OBJECT(self), FU_BINDER_BRIDGE_QDATA_KEY));
	return ptr != nullptr ? *ptr : nullptr;
}

gboolean
fu_binder_daemon_setup_aidl_service(FuBinderDaemon *daemon, GError **error)
{
	auto bridge = fu_binder_bridge_get(daemon);
	if (bridge == nullptr) {
		bridge = ::ndk::SharedRefBase::make<FwupdBinderBridge>(daemon);
		g_object_set_data_full(
		    G_OBJECT(daemon),
		    FU_BINDER_BRIDGE_QDATA_KEY,
		    new std::shared_ptr<FwupdBinderBridge>(bridge),
		    [](gpointer p) {
			    delete static_cast<std::shared_ptr<FwupdBinderBridge> *>(p);
		    });
	}

	binder_status_t rc = AServiceManager_addService(bridge->asBinder().get(),
							"org.freedesktop.fwupd.IFwupd/default");
	if (rc != STATUS_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to register binder service: %d",
			    (int)rc);
		return FALSE;
	}

	/* success */
	return TRUE;
}

void
fu_binder_bridge_shutdown(FuBinderDaemon *self)
{
	g_return_if_fail(FU_IS_BINDER_DAEMON(self));
	auto bridge = fu_binder_bridge_get(self);
	if (bridge != nullptr)
		bridge->shutdown();
}

void
fu_binder_bridge_emit_device_added(FuBinderDaemon *self, FwupdDevice *device)
{
	g_return_if_fail(FU_IS_BINDER_DAEMON(self));
	g_return_if_fail(FWUPD_IS_DEVICE(device));
	auto bridge = fu_binder_bridge_get(self);
	if (bridge != nullptr)
		bridge->emit_device_added(device);
}

void
fu_binder_bridge_emit_device_removed(FuBinderDaemon *self, FwupdDevice *device)
{
	g_return_if_fail(FU_IS_BINDER_DAEMON(self));
	g_return_if_fail(FWUPD_IS_DEVICE(device));
	auto bridge = fu_binder_bridge_get(self);
	if (bridge != nullptr)
		bridge->emit_device_removed(device);
}

void
fu_binder_bridge_emit_device_changed(FuBinderDaemon *self, FwupdDevice *device)
{
	g_return_if_fail(FU_IS_BINDER_DAEMON(self));
	g_return_if_fail(FWUPD_IS_DEVICE(device));
	auto bridge = fu_binder_bridge_get(self);
	if (bridge != nullptr)
		bridge->emit_device_changed(device);
}

void
fu_binder_bridge_emit_device_request(FuBinderDaemon *self, FwupdRequest *request)
{
	g_return_if_fail(FU_IS_BINDER_DAEMON(self));
	g_return_if_fail(FWUPD_IS_REQUEST(request));
	auto bridge = fu_binder_bridge_get(self);
	if (bridge != nullptr)
		bridge->emit_device_request(request);
}

void
fu_binder_bridge_emit_changed(FuBinderDaemon *self)
{
	g_return_if_fail(FU_IS_BINDER_DAEMON(self));
	auto bridge = fu_binder_bridge_get(self);
	if (bridge != nullptr)
		bridge->emit_changed();
}
