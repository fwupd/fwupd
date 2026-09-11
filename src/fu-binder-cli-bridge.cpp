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
fu_binder_cli_bridge_get_releases(AIBinder *binder, const char *device_id, GError **error)
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
	auto status = service->getReleases(std::string(device_id), &aidl_rels);
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

GPtrArray *
fu_binder_cli_bridge_get_plugins(AIBinder *binder, GError **error)
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

	std::vector<aidl_fwupd::FwupdPlugin> aidl_plugins;
	auto status = service->getPlugins(&aidl_plugins);
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

	g_autoptr(GPtrArray) plugins =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (const auto &p : aidl_plugins) {
		FwupdPlugin *plugin = fu_binder_plugin_from_aidl(p, error);
		if (plugin == NULL)
			return NULL;
		g_ptr_array_add(plugins, plugin);
	}
	return g_steal_pointer(&plugins);
}

GPtrArray *
fu_binder_cli_bridge_get_history(AIBinder *binder, GError **error)
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
	auto status = service->getHistory(&aidl_devs);
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
fu_binder_cli_bridge_set_feature_flags(AIBinder *binder,
				       FwupdFeatureFlags feature_flags,
				       GError **error)
{
	AIBinder_incStrong(binder);
	::ndk::SpAIBinder spBinder;
	spBinder.set(binder);
	std::shared_ptr<aidl_fwupd::IFwupd> service = aidl_fwupd::IFwupd::fromBinder(spBinder);

	if (service == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to cast Binder to IFwupd interface");
		return FALSE;
	}

	auto status = service->setFeatureFlags((int64_t)feature_flags);
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
			    "Binder transaction failed: %s",
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

gboolean
fu_binder_cli_bridge_update_metadata(AIBinder *binder,
				     const char *remote_id,
				     int metadata_fd,
				     int signature_fd,
				     GError **error)
{
	AIBinder_incStrong(binder);
	::ndk::SpAIBinder spBinder;
	spBinder.set(binder);
	std::shared_ptr<aidl_fwupd::IFwupd> service = aidl_fwupd::IFwupd::fromBinder(spBinder);

	if (service == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to cast Binder to IFwupd interface");
		return FALSE;
	}

	::ndk::ScopedFileDescriptor data_sfd(dup(metadata_fd));
	if (data_sfd.get() < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to duplicate metadata file descriptor for Binder");
		return FALSE;
	}
	::ndk::ScopedFileDescriptor sig_sfd(dup(signature_fd));
	if (sig_sfd.get() < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to duplicate signature file descriptor for Binder");
		return FALSE;
	}

	aidl_fwupd::FwupdMetadata metadata;
	metadata.remoteId = std::string(remote_id);
	metadata.dataFd = std::move(data_sfd);
	metadata.signatureFd = std::move(sig_sfd);

	auto status = service->updateMetadata(metadata);
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
			    "Binder transaction failed: %s",
			    status.getDescription().c_str());
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* map an unsuccessful AIDL status onto a #GError */
static gboolean
fu_binder_cli_bridge_propagate_status(const ::ndk::ScopedAStatus &status, GError **error)
{
	if (status.isOk())
		return TRUE;
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
		    "Binder transaction failed: %s",
		    status.getDescription().c_str());
	return FALSE;
}

/* obtain the IFwupd service proxy from an opaque binder handle */
static std::shared_ptr<aidl_fwupd::IFwupd>
fu_binder_cli_bridge_get_service(AIBinder *binder, GError **error)
{
	AIBinder_incStrong(binder);
	::ndk::SpAIBinder spBinder;
	spBinder.set(binder);
	std::shared_ptr<aidl_fwupd::IFwupd> service = aidl_fwupd::IFwupd::fromBinder(spBinder);
	if (service == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to cast Binder to IFwupd interface");
	}
	return service;
}

gboolean
fu_binder_cli_bridge_activate(AIBinder *binder, const char *device_id, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return FALSE;
	return fu_binder_cli_bridge_propagate_status(service->activate(std::string(device_id)),
						     error);
}

gboolean
fu_binder_cli_bridge_unlock(AIBinder *binder, const char *device_id, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return FALSE;
	return fu_binder_cli_bridge_propagate_status(service->unlock(std::string(device_id)),
						     error);
}

gboolean
fu_binder_cli_bridge_verify(AIBinder *binder, const char *device_id, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return FALSE;
	return fu_binder_cli_bridge_propagate_status(service->verify(std::string(device_id)),
						     error);
}

gboolean
fu_binder_cli_bridge_verify_update(AIBinder *binder, const char *device_id, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return FALSE;
	return fu_binder_cli_bridge_propagate_status(service->verifyUpdate(std::string(device_id)),
						     error);
}

gboolean
fu_binder_cli_bridge_modify_remote(AIBinder *binder,
				   const char *remote_id,
				   const char *key,
				   const char *value,
				   GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return FALSE;
	return fu_binder_cli_bridge_propagate_status(
	    service->modifyRemote(std::string(remote_id), std::string(key), std::string(value)),
	    error);
}

gboolean
fu_binder_cli_bridge_clean_remote(AIBinder *binder, const char *remote_id, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return FALSE;
	return fu_binder_cli_bridge_propagate_status(service->cleanRemote(std::string(remote_id)),
						     error);
}

gboolean
fu_binder_cli_bridge_modify_device(AIBinder *binder,
				   const char *device_id,
				   const char *key,
				   const char *value,
				   GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return FALSE;
	return fu_binder_cli_bridge_propagate_status(
	    service->modifyDevice(std::string(device_id), std::string(key), std::string(value)),
	    error);
}

gboolean
fu_binder_cli_bridge_modify_config(AIBinder *binder,
				   const char *section,
				   const char *key,
				   const char *value,
				   GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return FALSE;
	return fu_binder_cli_bridge_propagate_status(
	    service->modifyConfig(std::string(section), std::string(key), std::string(value)),
	    error);
}

gboolean
fu_binder_cli_bridge_reset_config(AIBinder *binder, const char *section, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return FALSE;
	return fu_binder_cli_bridge_propagate_status(service->resetConfig(std::string(section)),
						     error);
}

gboolean
fu_binder_cli_bridge_clear_results(AIBinder *binder, const char *device_id, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return FALSE;
	return fu_binder_cli_bridge_propagate_status(service->clearResults(std::string(device_id)),
						     error);
}

GPtrArray *
fu_binder_cli_bridge_get_details(AIBinder *binder, int fd, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return NULL;

	::ndk::ScopedFileDescriptor sfd(dup(fd));
	if (sfd.get() < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to duplicate file descriptor for Binder");
		return NULL;
	}

	std::vector<aidl_fwupd::FwupdDevice> aidl_devs;
	if (!fu_binder_cli_bridge_propagate_status(service->getDetails(sfd, &aidl_devs), error))
		return NULL;

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

GHashTable *
fu_binder_cli_bridge_get_report_metadata(AIBinder *binder, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return NULL;

	std::vector<aidl_fwupd::FwupdKeyValue> aidl_kvs;
	if (!fu_binder_cli_bridge_propagate_status(service->getReportMetadata(&aidl_kvs), error))
		return NULL;

	g_autoptr(GHashTable) metadata =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	for (const auto &kv : aidl_kvs) {
		if (!kv.key.has_value())
			continue;
		g_hash_table_insert(metadata,
				    g_strdup(kv.key.value().c_str()),
				    g_strdup(kv.value.has_value() ? kv.value.value().c_str() : ""));
	}
	return g_steal_pointer(&metadata);
}

GPtrArray *
fu_binder_cli_bridge_get_bios_settings(AIBinder *binder, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return NULL;

	std::vector<aidl_fwupd::FwupdBiosSetting> aidl_settings;
	if (!fu_binder_cli_bridge_propagate_status(service->getBiosSettings(&aidl_settings), error))
		return NULL;

	g_autoptr(GPtrArray) settings =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (const auto &s : aidl_settings) {
		FwupdBiosSetting *setting = fu_binder_bios_setting_from_aidl(s, error);
		if (setting == NULL)
			return NULL;
		g_ptr_array_add(settings, setting);
	}
	return g_steal_pointer(&settings);
}

gboolean
fu_binder_cli_bridge_modify_bios_settings(AIBinder *binder, GHashTable *settings, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	GHashTableIter iter;
	gpointer key, value;

	if (service == NULL)
		return FALSE;

	std::vector<aidl_fwupd::FwupdKeyValue> aidl_kvs;
	g_hash_table_iter_init(&iter, settings);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		aidl_fwupd::FwupdKeyValue kv;
		kv.key = std::string((const gchar *)key);
		kv.value = std::string((const gchar *)value);
		aidl_kvs.push_back(kv);
	}
	return fu_binder_cli_bridge_propagate_status(service->setBiosSettings(aidl_kvs), error);
}

GPtrArray *
fu_binder_cli_bridge_get_host_security_attrs(AIBinder *binder, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return NULL;

	std::vector<aidl_fwupd::FwupdSecurityAttr> aidl_attrs;
	if (!fu_binder_cli_bridge_propagate_status(service->getHostSecurityAttrs(&aidl_attrs),
						   error))
		return NULL;

	g_autoptr(GPtrArray) attrs = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (const auto &a : aidl_attrs) {
		FwupdSecurityAttr *attr = fu_binder_security_attr_from_aidl(a, error);
		if (attr == NULL)
			return NULL;
		g_ptr_array_add(attrs, attr);
	}
	return g_steal_pointer(&attrs);
}

GPtrArray *
fu_binder_cli_bridge_get_host_security_events(AIBinder *binder, guint limit, GError **error)
{
	std::shared_ptr<aidl_fwupd::IFwupd> service =
	    fu_binder_cli_bridge_get_service(binder, error);
	if (service == NULL)
		return NULL;

	std::vector<aidl_fwupd::FwupdSecurityAttr> aidl_attrs;
	if (!fu_binder_cli_bridge_propagate_status(
		service->getHostSecurityEvents((int32_t)limit, &aidl_attrs),
		error))
		return NULL;

	g_autoptr(GPtrArray) attrs = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (const auto &a : aidl_attrs) {
		FwupdSecurityAttr *attr = fu_binder_security_attr_from_aidl(a, error);
		if (attr == NULL)
			return NULL;
		g_ptr_array_add(attrs, attr);
	}
	return g_steal_pointer(&attrs);
}
