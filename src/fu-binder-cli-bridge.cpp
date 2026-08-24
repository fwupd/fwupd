/*
 * Copyright 2026 Harsha Muttavarapu <harshams@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <aidl/org/freedesktop/fwupd/BnFwupdEventListener.h>
#include <aidl/org/freedesktop/fwupd/IFwupd.h>

#include <android/binder_auto_utils.h>
#include <android/binder_process.h>

#include <dlfcn.h>
#include <unistd.h>
#include <vector>

#include "fu-binder-cli-bridge.h"
#include "fu-binder-common.h"

namespace aidl_fwupd = aidl::org::freedesktop::fwupd;

/* define the listener object: converts incoming Binder events back into
 * FwupdClient signals so they flow through the same event path as the D-Bus
 * client (device-added, device-removed, device-changed, device-request) */
class FwupdEventListenerImpl : public aidl_fwupd::BnFwupdEventListener
{
	FwupdClient *m_client;

      public:
	explicit FwupdEventListenerImpl(FwupdClient *client)
	    : m_client(FWUPD_CLIENT(g_object_ref(client)))
	{
	}
	~FwupdEventListenerImpl() override { g_object_unref(m_client); }

	::ndk::ScopedAStatus
	onChanged() override
	{
		g_debug("engine state changed");
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus
	onDeviceAdded(const aidl_fwupd::FwupdDevice &device) override
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(FwupdDevice) dev = fu_binder_device_from_aidl(device, &error);
		if (dev == NULL) {
			g_warning("failed to convert added device: %s", error->message);
			return ::ndk::ScopedAStatus::ok();
		}
		fwupd_client_emit_device_added(m_client, dev);
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus
	onDeviceRemoved(const aidl_fwupd::FwupdDevice &device) override
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(FwupdDevice) dev = fu_binder_device_from_aidl(device, &error);
		if (dev == NULL) {
			g_warning("failed to convert removed device: %s", error->message);
			return ::ndk::ScopedAStatus::ok();
		}
		fwupd_client_emit_device_removed(m_client, dev);
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus
	onDeviceChanged(const aidl_fwupd::FwupdDevice &device) override
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(FwupdDevice) dev = fu_binder_device_from_aidl(device, &error);
		if (dev == NULL) {
			g_warning("failed to convert changed device: %s", error->message);
			return ::ndk::ScopedAStatus::ok();
		}
		fwupd_client_emit_device_changed(m_client, dev);
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus
	onDeviceRequest(const aidl_fwupd::FwupdRequest &request) override
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(FwupdRequest) req = fu_binder_request_from_aidl(request, &error);
		if (req == NULL) {
			g_warning("failed to convert device request: %s", error->message);
			return ::ndk::ScopedAStatus::ok();
		}
		fwupd_client_emit_device_request(m_client, req);
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus
	onPropertiesChanged(const aidl_fwupd::FwupdProperties &properties) override
	{
		fwupd_client_set_status(m_client, (FwupdStatus)properties.status);
		fwupd_client_set_percentage(m_client, properties.percentage);
		return ::ndk::ScopedAStatus::ok();
	}
};

AIBinder *
fu_binder_cli_bridge_get_service_handle(GError **error)
{
	void *handle = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
	if (handle == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to dlopen libbinder_ndk.so");
		return NULL;
	}

	auto get_svc = (AIBinder * (*)(const char *)) dlsym(handle, "AServiceManager_getService");
	AIBinder *binder = get_svc ? get_svc("org.freedesktop.fwupd.IFwupd/default") : NULL;
	dlclose(handle);

	if (binder == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "AServiceManager could not find fwupd daemon");
		return NULL;
	}
	return binder;
}

GPtrArray *
fu_binder_cli_bridge_get_devices(AIBinder *binder, GError **error)
{
	AIBinder_incStrong(binder);
	::ndk::SpAIBinder spBinder;
	spBinder.set(binder);
	auto service = aidl_fwupd::IFwupd::fromBinder(spBinder);

	if (service == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to cast Binder to IFwupd interface");
		return NULL;
	}

	std::vector<aidl_fwupd::FwupdDevice> aidl_devs;
	auto status = service->getDevices(&aidl_devs);
	if (!status.isOk()) {
		if (status.getExceptionCode() == EX_SERVICE_SPECIFIC) {
			const char *msg = status.getMessage();
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    status.getServiceSpecificError(),
					    msg != NULL ? msg : "unknown daemon error");
			return NULL;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "binder transaction failed: %s",
			    status.getDescription().c_str());
		return NULL;
	}

	g_autoptr(GPtrArray) devices =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (const auto &dev : aidl_devs) {
		FwupdDevice *device = fu_binder_device_from_aidl(dev, error);
		if (device == NULL)
			return NULL;
		g_ptr_array_add(devices, device);
	}
	return g_steal_pointer(&devices);
}

GPtrArray *
fu_binder_cli_bridge_get_upgrades(AIBinder *binder, const char *device_id, GError **error)
{
	AIBinder_incStrong(binder);
	::ndk::SpAIBinder spBinder;
	spBinder.set(binder);
	auto service = aidl_fwupd::IFwupd::fromBinder(spBinder);

	if (service == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to cast Binder to IFwupd interface");
		return NULL;
	}

	std::vector<aidl_fwupd::FwupdRelease> aidl_rels;
	auto status = service->getUpdates(std::string(device_id), &aidl_rels);
	if (!status.isOk()) {
		if (status.getExceptionCode() == EX_SERVICE_SPECIFIC) {
			const char *msg = status.getMessage();
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    status.getServiceSpecificError(),
					    msg != NULL ? msg : "unknown daemon error");
			return NULL;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    status.getStatus(),
			    "Binder transaction failed: %s",
			    status.getDescription().c_str());
		return NULL;
	}

	g_autoptr(GPtrArray) rels = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (const auto &rel : aidl_rels) {
		FwupdRelease *release = fu_binder_release_from_aidl(rel, error);
		if (release == NULL)
			return NULL;
		g_ptr_array_add(rels, release);
	}
	return g_steal_pointer(&rels);
}

GPtrArray *
fu_binder_cli_bridge_get_remotes(AIBinder *binder, GError **error)
{
	AIBinder_incStrong(binder);
	::ndk::SpAIBinder spBinder;
	spBinder.set(binder);
	auto service = aidl_fwupd::IFwupd::fromBinder(spBinder);

	if (service == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to cast Binder to IFwupd interface");
		return NULL;
	}

	std::vector<aidl_fwupd::FwupdRemote> aidl_remotes;
	auto status = service->getRemotes(&aidl_remotes);
	if (!status.isOk()) {
		if (status.getExceptionCode() == EX_SERVICE_SPECIFIC) {
			const char *msg = status.getMessage();
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    status.getServiceSpecificError(),
					    msg != NULL ? msg : "unknown daemon error");
			return NULL;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    status.getStatus(),
			    "Binder transaction failed: %s",
			    status.getDescription().c_str());
		return NULL;
	}

	g_autoptr(GPtrArray) remotes =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (const auto &r : aidl_remotes) {
		FwupdRemote *remote = fu_binder_remote_from_aidl(r, error);
		if (remote == NULL)
			return NULL;
		g_ptr_array_add(remotes, remote);
	}
	return g_steal_pointer(&remotes);
}

gboolean
fu_binder_cli_bridge_connect_client(AIBinder *binder, FwupdClient *client, GError **error)
{
	AIBinder_incStrong(binder);
	::ndk::SpAIBinder spBinder;
	spBinder.set(binder);
	std::shared_ptr<aidl_fwupd::IFwupd> proxy = aidl_fwupd::IFwupd::fromBinder(spBinder);

	if (proxy == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to create IFwupd proxy");
		return FALSE;
	}

	std::vector<aidl_fwupd::FwupdHwid> aidl_hwids;
	ndk::ScopedAStatus status = proxy->getHwids(&aidl_hwids);

	if (!status.isOk()) {
		if (status.getExceptionCode() == EX_SERVICE_SPECIFIC) {
			const char *msg = status.getMessage();
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    status.getServiceSpecificError(),
					    msg != NULL ? msg : "unknown daemon error");
			return FALSE;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    status.getStatus(),
			    "getHwids AIDL call failed: %s",
			    status.getDescription().c_str());
		return FALSE;
	}
	for (const auto &hwid : aidl_hwids)
		fwupd_client_add_hwid(client, hwid.key.c_str(), hwid.value.c_str());

	/* success */
	return TRUE;
}

gboolean
fu_binder_cli_bridge_setup_listener(AIBinder *binder_handle, FwupdClient *client, GError **error)
{
	AIBinder_incStrong(binder_handle);
	::ndk::SpAIBinder spBinder;
	spBinder.set(binder_handle);
	std::shared_ptr<aidl_fwupd::IFwupd> service = aidl_fwupd::IFwupd::fromBinder(spBinder);

	if (service == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "failed to cast Binder to IFwupd interface for listener");
		return FALSE;
	}

	std::shared_ptr<FwupdEventListenerImpl> listener =
	    ::ndk::SharedRefBase::make<FwupdEventListenerImpl>(client);

	ABinderProcess_startThreadPool();
	auto status = service->addEventListener(listener);
	if (!status.isOk()) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "failed to register AIDL listener with daemon: %s",
			    status.getDescription().c_str());
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_binder_cli_bridge_install(AIBinder *binder_handle,
			     const char *id,
			     int fd,
			     FwupdInstallFlags install_flags,
			     GError **error)
{
	AIBinder_incStrong(binder_handle);
	::ndk::SpAIBinder spBinder;
	spBinder.set(binder_handle);
	std::shared_ptr<aidl_fwupd::IFwupd> service = aidl_fwupd::IFwupd::fromBinder(spBinder);

	if (service == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to cast Binder to IFwupd");
		return FALSE;
	}

	::ndk::ScopedFileDescriptor sfd(dup(fd));

	if (sfd.get() < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to duplicate file descriptor for Binder");
		return FALSE;
	}

	aidl_fwupd::FwupdInstallRequest req;
	req.id = std::string(id);
	req.firmwareFd = std::move(sfd);
	req.flags = install_flags;

	auto status = service->install(req);

	if (!status.isOk()) {
		if (status.getExceptionCode() == EX_SERVICE_SPECIFIC) {
			const char *msg = status.getMessage();
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    status.getServiceSpecificError(),
					    msg != NULL ? msg : "unknown daemon error");
			return FALSE;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    status.getStatus(),
			    "Install failed: %s",
			    status.getDescription().c_str());
		return FALSE;
	}

	/* success */
	return TRUE;
}
