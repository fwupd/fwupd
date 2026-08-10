/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBinder"

#include "config.h"

#include "fwupd-remote-private.h"

#include "fu-binder-common.h"

aidl_fwupd::FwupdRemote
fu_binder_remote_to_aidl(FwupdRemote *remote)
{
	aidl_fwupd::FwupdRemote r;
	r.id = fwupd_remote_get_id(remote);
	if (fwupd_remote_get_title(remote) != NULL)
		r.title = fwupd_remote_get_title(remote);
	if (fwupd_remote_get_metadata_uri(remote) != NULL)
		r.metadataUri = fwupd_remote_get_metadata_uri(remote);
	r.flags = fwupd_remote_get_flags(remote);
	return r;
}

FwupdRemote *
fu_binder_remote_from_aidl(const aidl_fwupd::FwupdRemote &r)
{
	FwupdRemote *remote = fwupd_remote_new();
	if (r.id.has_value())
		fwupd_remote_set_id(remote, r.id.value().c_str());
	if (r.title.has_value())
		fwupd_remote_set_title(remote, r.title.value().c_str());
	if (r.metadataUri.has_value())
		fwupd_remote_set_metadata_uri(remote, r.metadataUri.value().c_str());
	fwupd_remote_set_flags(remote, (FwupdRemoteFlags)r.flags);
	return remote;
}

static std::optional<std::vector<std::optional<std::string>>>
fu_binder_ptr_array_to_vec(GPtrArray *values)
{
	std::vector<std::optional<std::string>> vec;
	for (guint i = 0; i < values->len; i++) {
		const gchar *value = ((const gchar *)g_ptr_array_index(values, i));
		vec.push_back(std::string(value));
	}
	return vec;
}

aidl_fwupd::FwupdDevice
fu_binder_device_to_aidl(FwupdDevice *device)
{
	aidl_fwupd::FwupdDevice d;
	if (fwupd_device_get_id(device) != NULL)
		d.id = fwupd_device_get_id(device);
	if (fwupd_device_get_parent_id(device) != NULL)
		d.parentId = fwupd_device_get_parent_id(device);
	if (fwupd_device_get_composite_id(device) != NULL)
		d.compositeId = fwupd_device_get_composite_id(device);
	if (fwupd_device_get_name(device) != NULL)
		d.name = fwupd_device_get_name(device);
	if (fwupd_device_get_version(device) != NULL)
		d.version = fwupd_device_get_version(device);
	if (fwupd_device_get_plugin(device) != NULL)
		d.plugin = fwupd_device_get_plugin(device);
	if (fwupd_device_get_summary(device) != NULL)
		d.summary = fwupd_device_get_summary(device);
	if (fwupd_device_get_vendor(device) != NULL)
		d.vendor = fwupd_device_get_vendor(device);
	d.protocols = fu_binder_ptr_array_to_vec(fwupd_device_get_protocols(device));
	d.icons = fu_binder_ptr_array_to_vec(fwupd_device_get_icons(device));
	d.flags = fwupd_device_get_flags(device);
	d.percentage = fwupd_device_get_percentage(device);
	d.status = fwupd_device_get_status(device);
	return d;
}

FwupdDevice *
fu_binder_device_from_aidl(const aidl_fwupd::FwupdDevice &d)
{
	FwupdDevice *device = fwupd_device_new();
	if (!d.id.empty())
		fwupd_device_set_id(device, d.id.c_str());
	if (d.parentId.has_value() && !d.parentId.value().empty())
		fwupd_device_set_parent_id(device, d.parentId.value().c_str());
	if (d.compositeId.has_value() && !d.compositeId.value().empty())
		fwupd_device_set_composite_id(device, d.compositeId.value().c_str());
	if (!d.name.empty())
		fwupd_device_set_name(device, d.name.c_str());
	if (!d.version.empty())
		fwupd_device_set_version(device, d.version.c_str());
	if (!d.plugin.empty())
		fwupd_device_set_plugin(device, d.plugin.c_str());
	if (d.summary.has_value() && !d.summary.value().empty())
		fwupd_device_set_summary(device, d.summary.value().c_str());
	if (d.vendor.has_value())
		fwupd_device_set_vendor(device, d.vendor.value().c_str());
	if (d.protocols.has_value()) {
		for (const auto &protocol : d.protocols.value())
			fwupd_device_add_protocol(device, protocol.value().c_str());
	}
	if (d.icons.has_value()) {
		for (const auto &icon : d.icons.value())
			fwupd_device_add_icon(device, icon.value().c_str());
	}
	fwupd_device_set_flags(device, d.flags);
	fwupd_device_set_percentage(device, d.percentage);
	fwupd_device_set_status(device, (FwupdStatus)d.status);
	return device;
}

aidl_fwupd::FwupdRelease
fu_binder_release_to_aidl(FwupdRelease *release)
{
	aidl_fwupd::FwupdRelease r;
	if (fwupd_release_get_id(release) != NULL)
		r.remoteId = fwupd_release_get_id(release);
	if (fwupd_release_get_name(release) != NULL)
		r.name = fwupd_release_get_name(release);
	if (fwupd_release_get_version(release) != NULL)
		r.version = fwupd_release_get_version(release);
	if (fwupd_release_get_filename(release) != NULL)
		r.filename = fwupd_release_get_filename(release);
	if (fwupd_release_get_summary(release) != NULL)
		r.summary = fwupd_release_get_summary(release);
	if (fwupd_release_get_description(release) != NULL)
		r.description = fwupd_release_get_description(release);
	r.locations = fu_binder_ptr_array_to_vec(fwupd_release_get_locations(release));
	r.checksums = fu_binder_ptr_array_to_vec(fwupd_release_get_checksums(release));
	if (fwupd_release_get_appstream_id(release) != NULL)
		r.appstreamId = fwupd_release_get_appstream_id(release);
	r.size = fwupd_release_get_size(release);
	r.flags = fwupd_release_get_flags(release);
	r.urgency = fwupd_release_get_urgency(release);
	return r;
}

FwupdRelease *
fu_binder_release_from_aidl(const aidl_fwupd::FwupdRelease &r)
{
	FwupdRelease *release = fwupd_release_new();
	fwupd_release_set_id(release, r.remoteId.c_str());
	fwupd_release_set_name(release, r.name.c_str());
	fwupd_release_set_version(release, r.version.c_str());
	fwupd_release_set_filename(release, r.filename.c_str());
	if (r.checksums.has_value()) {
		for (const auto &checksum : r.checksums.value())
			fwupd_release_add_checksum(release, checksum.value().c_str());
	}
	if (r.appstreamId.has_value())
		fwupd_release_set_appstream_id(release, r.appstreamId.value().c_str());
	if (r.summary.has_value())
		fwupd_release_set_summary(release, r.summary.value().c_str());
	if (r.description.has_value())
		fwupd_release_set_description(release, r.description.value().c_str());
	if (r.locations.has_value()) {
		for (const auto &loc : r.locations.value())
			fwupd_release_add_location(release, loc.value().c_str());
	}
	fwupd_release_set_flags(release, (FwupdReleaseFlags)r.flags);
	fwupd_release_set_size(release, r.size);
	return release;
}

aidl_fwupd::FwupdRequest
fu_binder_request_to_aidl(FwupdRequest *request)
{
	aidl_fwupd::FwupdRequest r;
	r.kind = fwupd_request_get_kind(request);
	if (fwupd_request_get_id(request) != NULL)
		r.id = fwupd_request_get_id(request);
	if (fwupd_request_get_message(request) != NULL)
		r.message = fwupd_request_get_message(request);
	return r;
}
