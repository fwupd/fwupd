/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBinder"

#include "config.h"

#include <optional>
#include <string>
#include <vector>

#include "fwupd-remote-private.h"

#include "fu-binder-common.h"

static std::optional<std::vector<std::optional<std::string>>>
fu_binder_ptr_array_to_vec(GPtrArray *values)
{
	std::vector<std::optional<std::string>> vec;
	if (values == NULL)
		return std::nullopt;
	vec.reserve(values->len);
	for (guint i = 0; i < values->len; i++) {
		const gchar *value = ((const gchar *)g_ptr_array_index(values, i));
		if (value == NULL)
			continue;
		vec.push_back(std::string(value));
	}
	return vec;
}

aidl_fwupd::FwupdRemote
fu_binder_remote_to_aidl(FwupdRemote *remote)
{
	aidl_fwupd::FwupdRemote r;
	if (fwupd_remote_get_id(remote) != NULL)
		r.id = fwupd_remote_get_id(remote);
	r.kind = fwupd_remote_get_kind(remote);
	if (fwupd_remote_get_report_uri(remote) != NULL)
		r.reportUri = fwupd_remote_get_report_uri(remote);
	if (fwupd_remote_get_metadata_uri(remote) != NULL)
		r.metadataUri = fwupd_remote_get_metadata_uri(remote);
	if (fwupd_remote_get_metadata_uri_sig(remote) != NULL)
		r.metadataUriSig = fwupd_remote_get_metadata_uri_sig(remote);
	if (fwupd_remote_get_firmware_base_uri(remote) != NULL)
		r.firmwareBaseUri = fwupd_remote_get_firmware_base_uri(remote);
	if (fwupd_remote_get_username(remote) != NULL)
		r.username = fwupd_remote_get_username(remote);
	if (fwupd_remote_get_title(remote) != NULL)
		r.title = fwupd_remote_get_title(remote);
	if (fwupd_remote_get_privacy_uri(remote) != NULL)
		r.privacyUri = fwupd_remote_get_privacy_uri(remote);
	if (fwupd_remote_get_agreement(remote) != NULL)
		r.agreement = fwupd_remote_get_agreement(remote);
	if (fwupd_remote_get_checksum(remote) != NULL)
		r.checksumSig = fwupd_remote_get_checksum(remote);
	if (fwupd_remote_get_checksum_metadata(remote) != NULL)
		r.checksum = fwupd_remote_get_checksum_metadata(remote);
	if (fwupd_remote_get_filename_cache(remote) != NULL)
		r.filenameCache = fwupd_remote_get_filename_cache(remote);
	if (fwupd_remote_get_filename_cache_sig(remote) != NULL)
		r.filenameCacheSig = fwupd_remote_get_filename_cache_sig(remote);
	if (fwupd_remote_get_filename_source(remote) != NULL)
		r.filenameSource = fwupd_remote_get_filename_source(remote);
	if (fwupd_remote_get_remotes_dir(remote) != NULL)
		r.remotesDir = fwupd_remote_get_remotes_dir(remote);
	r.flags = fwupd_remote_get_flags(remote);
	r.priority = fwupd_remote_get_priority(remote);
	r.mtime = fwupd_remote_get_mtime(remote);
	r.refreshIntervalSec = fwupd_remote_get_refresh_interval(remote);
	return r;
}

FwupdRemote *
fu_binder_remote_from_aidl(const aidl_fwupd::FwupdRemote &r, GError **error)
{
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	if (!r.id.empty())
		fwupd_remote_set_id(remote, r.id.c_str());
	if (r.kind < 0 || r.kind >= FWUPD_REMOTE_KIND_LAST) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid FwupdRemoteKind %i from client",
			    r.kind);
		return NULL;
	}
	fwupd_remote_set_kind(remote, (FwupdRemoteKind)r.kind);
	if (r.reportUri.has_value())
		fwupd_remote_set_report_uri(remote, r.reportUri.value().c_str());
	if (r.metadataUri.has_value())
		fwupd_remote_set_metadata_uri(remote, r.metadataUri.value().c_str());
	if (r.firmwareBaseUri.has_value())
		fwupd_remote_set_firmware_base_uri(remote, r.firmwareBaseUri.value().c_str());
	if (r.username.has_value())
		fwupd_remote_set_username(remote, r.username.value().c_str());
	if (r.title.has_value())
		fwupd_remote_set_title(remote, r.title.value().c_str());
	if (r.privacyUri.has_value())
		fwupd_remote_set_privacy_uri(remote, r.privacyUri.value().c_str());
	if (r.agreement.has_value())
		fwupd_remote_set_agreement(remote, r.agreement.value().c_str());
	if (r.checksumSig.has_value())
		fwupd_remote_set_checksum_sig(remote, r.checksumSig.value().c_str());
	/*
	 * metadataUriSig and filenameCacheSig have no independent setter and are
	 * regenerated from metadataUri and filenameCache above; the metadata
	 * checksum (r.checksum) is exported by the daemon but has no setter.
	 */
	if (r.filenameCache.has_value())
		fwupd_remote_set_filename_cache(remote, r.filenameCache.value().c_str());
	if (r.filenameSource.has_value())
		fwupd_remote_set_filename_source(remote, r.filenameSource.value().c_str());
	if (r.remotesDir.has_value())
		fwupd_remote_set_remotes_dir(remote, r.remotesDir.value().c_str());
	fwupd_remote_set_flags(remote, (FwupdRemoteFlags)r.flags);
	fwupd_remote_set_priority(remote, r.priority);
	if (r.mtime < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative mtime %" G_GINT64_FORMAT " from client",
			    r.mtime);
		return NULL;
	}
	fwupd_remote_set_mtime(remote, r.mtime);
	if (r.refreshIntervalSec < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative refresh interval %i from client",
			    r.refreshIntervalSec);
		return NULL;
	}
	fwupd_remote_set_refresh_interval(remote, r.refreshIntervalSec);
	return g_steal_pointer(&remote);
}

aidl_fwupd::FwupdDevice
fu_binder_device_to_aidl(FwupdDevice *device)
{
	GPtrArray *releases = fwupd_device_get_releases(device);
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
	if (fwupd_device_get_serial(device) != NULL)
		d.serial = fwupd_device_get_serial(device);
	if (fwupd_device_get_summary(device) != NULL)
		d.summary = fwupd_device_get_summary(device);
	if (fwupd_device_get_details_url(device) != NULL)
		d.detailsUrl = fwupd_device_get_details_url(device);
	if (fwupd_device_get_branch(device) != NULL)
		d.branch = fwupd_device_get_branch(device);
	if (fwupd_device_get_vendor(device) != NULL)
		d.vendor = fwupd_device_get_vendor(device);
	if (fwupd_device_get_version_lowest(device) != NULL)
		d.versionLowest = fwupd_device_get_version_lowest(device);
	if (fwupd_device_get_version_highest(device) != NULL)
		d.versionHighest = fwupd_device_get_version_highest(device);
	if (fwupd_device_get_version_bootloader(device) != NULL)
		d.versionBootloader = fwupd_device_get_version_bootloader(device);
	if (fwupd_device_get_update_error(device) != NULL)
		d.updateError = fwupd_device_get_update_error(device);
	d.instanceIds = fu_binder_ptr_array_to_vec(fwupd_device_get_instance_ids(device));
	d.guids = fu_binder_ptr_array_to_vec(fwupd_device_get_guids(device));
	d.protocols = fu_binder_ptr_array_to_vec(fwupd_device_get_protocols(device));
	d.issues = fu_binder_ptr_array_to_vec(fwupd_device_get_issues(device));
	d.problems = fwupd_device_get_problems(device);
	d.checksums = fu_binder_ptr_array_to_vec(fwupd_device_get_checksums(device));
	d.vendorIds = fu_binder_ptr_array_to_vec(fwupd_device_get_vendor_ids(device));
	d.icons = fu_binder_ptr_array_to_vec(fwupd_device_get_icons(device));
	if (releases->len > 0) {
		std::vector<std::optional<aidl_fwupd::FwupdRelease>> vec;
		for (guint i = 0; i < releases->len; i++) {
			FwupdRelease *release = (FwupdRelease *)g_ptr_array_index(releases, i);
			vec.push_back(fu_binder_release_to_aidl(release));
		}
		d.releases = vec;
	}
	d.flags = fwupd_device_get_flags(device);
	d.requestFlags = fwupd_device_get_request_flags(device);
	d.versionFormat = fwupd_device_get_version_format(device);
	d.flashesLeft = fwupd_device_get_flashes_left(device);
	d.batteryLevel = fwupd_device_get_battery_level(device);
	d.batteryThreshold = fwupd_device_get_battery_threshold(device);
	d.versionRaw = fwupd_device_get_version_raw(device);
	d.versionLowestRaw = fwupd_device_get_version_lowest_raw(device);
	d.versionHighestRaw = fwupd_device_get_version_highest_raw(device);
	d.versionBootloaderRaw = fwupd_device_get_version_bootloader_raw(device);
	d.versionBuildDate = fwupd_device_get_version_build_date(device);
	d.installDuration = fwupd_device_get_install_duration(device);
	d.created = fwupd_device_get_created(device);
	d.modified = fwupd_device_get_modified(device);
	d.updateState = fwupd_device_get_update_state(device);
	d.percentage = fwupd_device_get_percentage(device);
	d.status = fwupd_device_get_status(device);
	return d;
}

FwupdDevice *
fu_binder_device_from_aidl(const aidl_fwupd::FwupdDevice &d, GError **error)
{
	g_autoptr(FwupdDevice) device = fwupd_device_new();
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
	if (d.serial.has_value() && !d.serial.value().empty())
		fwupd_device_set_serial(device, d.serial.value().c_str());
	if (d.summary.has_value() && !d.summary.value().empty())
		fwupd_device_set_summary(device, d.summary.value().c_str());
	if (d.detailsUrl.has_value() && !d.detailsUrl.value().empty())
		fwupd_device_set_details_url(device, d.detailsUrl.value().c_str());
	if (d.branch.has_value() && !d.branch.value().empty())
		fwupd_device_set_branch(device, d.branch.value().c_str());
	if (d.vendor.has_value() && !d.vendor.value().empty())
		fwupd_device_set_vendor(device, d.vendor.value().c_str());
	if (d.versionLowest.has_value() && !d.versionLowest.value().empty())
		fwupd_device_set_version_lowest(device, d.versionLowest.value().c_str());
	if (d.versionHighest.has_value() && !d.versionHighest.value().empty())
		fwupd_device_set_version_highest(device, d.versionHighest.value().c_str());
	if (d.versionBootloader.has_value() && !d.versionBootloader.value().empty())
		fwupd_device_set_version_bootloader(device, d.versionBootloader.value().c_str());
	if (d.updateError.has_value() && !d.updateError.value().empty())
		fwupd_device_set_update_error(device, d.updateError.value().c_str());
	if (d.instanceIds.has_value()) {
		for (const auto &instance_id : d.instanceIds.value()) {
			if (instance_id.has_value())
				fwupd_device_add_instance_id(device, instance_id.value().c_str());
		}
	}
	if (d.guids.has_value()) {
		for (const auto &guid : d.guids.value()) {
			if (guid.has_value())
				fwupd_device_add_guid(device, guid.value().c_str());
		}
	}
	if (d.protocols.has_value()) {
		for (const auto &protocol : d.protocols.value()) {
			if (protocol.has_value())
				fwupd_device_add_protocol(device, protocol.value().c_str());
		}
	}
	if (d.issues.has_value()) {
		for (const auto &issue : d.issues.value()) {
			if (issue.has_value())
				fwupd_device_add_issue(device, issue.value().c_str());
		}
	}
	fwupd_device_set_problems(device, d.problems);
	if (d.checksums.has_value()) {
		for (const auto &checksum : d.checksums.value()) {
			if (checksum.has_value())
				fwupd_device_add_checksum(device, checksum.value().c_str());
		}
	}
	if (d.vendorIds.has_value()) {
		for (const auto &vendor_id : d.vendorIds.value()) {
			if (vendor_id.has_value())
				fwupd_device_add_vendor_id(device, vendor_id.value().c_str());
		}
	}
	if (d.icons.has_value()) {
		for (const auto &icon : d.icons.value()) {
			if (icon.has_value())
				fwupd_device_add_icon(device, icon.value().c_str());
		}
	}
	if (d.releases.has_value()) {
		for (const auto &release : d.releases.value()) {
			if (release.has_value()) {
				g_autoptr(FwupdRelease) rel =
				    fu_binder_release_from_aidl(release.value(), error);
				if (rel == NULL)
					return NULL;
				fwupd_device_add_release(device, rel);
			}
		}
	}
	fwupd_device_set_flags(device, d.flags);
	fwupd_device_set_request_flags(device, d.requestFlags);
	if (d.versionFormat < 0 || d.versionFormat >= FWUPD_VERSION_FORMAT_LAST) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid FwupdVersionFormat %i from client",
			    d.versionFormat);
		return NULL;
	}
	fwupd_device_set_version_format(device, (FwupdVersionFormat)d.versionFormat);
	if (d.flashesLeft < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative flashes left %i from client",
			    d.flashesLeft);
		return NULL;
	}
	fwupd_device_set_flashes_left(device, d.flashesLeft);
	if (d.batteryLevel < 0 || d.batteryLevel > FWUPD_BATTERY_LEVEL_INVALID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "out-of-range battery level %i from client",
			    d.batteryLevel);
		return NULL;
	}
	fwupd_device_set_battery_level(device, d.batteryLevel);
	if (d.batteryThreshold < 0 || d.batteryThreshold > FWUPD_BATTERY_LEVEL_INVALID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "out-of-range battery threshold %i from client",
			    d.batteryThreshold);
		return NULL;
	}
	fwupd_device_set_battery_threshold(device, d.batteryThreshold);
	if (d.versionRaw < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative version raw %" G_GINT64_FORMAT " from client",
			    d.versionRaw);
		return NULL;
	}
	fwupd_device_set_version_raw(device, d.versionRaw);
	if (d.versionLowestRaw < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative version lowest raw %" G_GINT64_FORMAT " from client",
			    d.versionLowestRaw);
		return NULL;
	}
	fwupd_device_set_version_lowest_raw(device, d.versionLowestRaw);
	if (d.versionHighestRaw < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative version highest raw %" G_GINT64_FORMAT " from client",
			    d.versionHighestRaw);
		return NULL;
	}
	fwupd_device_set_version_highest_raw(device, d.versionHighestRaw);
	if (d.versionBootloaderRaw < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative version bootloader raw %" G_GINT64_FORMAT " from client",
			    d.versionBootloaderRaw);
		return NULL;
	}
	fwupd_device_set_version_bootloader_raw(device, d.versionBootloaderRaw);
	if (d.versionBuildDate < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative version build date %" G_GINT64_FORMAT " from client",
			    d.versionBuildDate);
		return NULL;
	}
	fwupd_device_set_version_build_date(device, d.versionBuildDate);
	if (d.installDuration < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative install duration %i from client",
			    d.installDuration);
		return NULL;
	}
	fwupd_device_set_install_duration(device, d.installDuration);
	if (d.created < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative created %" G_GINT64_FORMAT " from client",
			    d.created);
		return NULL;
	}
	fwupd_device_set_created(device, d.created);
	if (d.modified < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative modified %" G_GINT64_FORMAT " from client",
			    d.modified);
		return NULL;
	}
	fwupd_device_set_modified(device, d.modified);
	if (d.updateState < 0 || d.updateState >= FWUPD_UPDATE_STATE_LAST) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid FwupdUpdateState %i from client",
			    d.updateState);
		return NULL;
	}
	fwupd_device_set_update_state(device, (FwupdUpdateState)d.updateState);
	if (!fwupd_percentage_is_valid(d.percentage)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "out-of-range percentage %i from client",
			    d.percentage);
		return NULL;
	}
	fwupd_device_set_percentage(device, d.percentage);
	if (d.status < 0 || d.status >= FWUPD_STATUS_LAST) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid FwupdStatus %i from client",
			    d.status);
		return NULL;
	}
	fwupd_device_set_status(device, (FwupdStatus)d.status);
	return g_steal_pointer(&device);
}

aidl_fwupd::FwupdRelease
fu_binder_release_to_aidl(FwupdRelease *release)
{
	aidl_fwupd::FwupdRelease r;
	if (fwupd_release_get_remote_id(release) != NULL)
		r.remoteId = fwupd_release_get_remote_id(release);
	if (fwupd_release_get_name(release) != NULL)
		r.name = fwupd_release_get_name(release);
	if (fwupd_release_get_version(release) != NULL)
		r.version = fwupd_release_get_version(release);
	if (fwupd_release_get_filename(release) != NULL)
		r.filename = fwupd_release_get_filename(release);
	if (fwupd_release_get_appstream_id(release) != NULL)
		r.appstreamId = fwupd_release_get_appstream_id(release);
	if (fwupd_release_get_id(release) != NULL)
		r.releaseId = fwupd_release_get_id(release);
	if (fwupd_release_get_name_variant_suffix(release) != NULL)
		r.nameVariantSuffix = fwupd_release_get_name_variant_suffix(release);
	if (fwupd_release_get_summary(release) != NULL)
		r.summary = fwupd_release_get_summary(release);
	if (fwupd_release_get_description(release) != NULL)
		r.description = fwupd_release_get_description(release);
	if (fwupd_release_get_branch(release) != NULL)
		r.branch = fwupd_release_get_branch(release);
	if (fwupd_release_get_protocol(release) != NULL)
		r.protocol = fwupd_release_get_protocol(release);
	r.categories = fu_binder_ptr_array_to_vec(fwupd_release_get_categories(release));
	r.issues = fu_binder_ptr_array_to_vec(fwupd_release_get_issues(release));
	r.checksums = fu_binder_ptr_array_to_vec(fwupd_release_get_checksums(release));
	r.tags = fu_binder_ptr_array_to_vec(fwupd_release_get_tags(release));
	if (fwupd_release_get_license(release) != NULL)
		r.license = fwupd_release_get_license(release);
	r.locations = fu_binder_ptr_array_to_vec(fwupd_release_get_locations(release));
	if (fwupd_release_get_homepage(release) != NULL)
		r.homepage = fwupd_release_get_homepage(release);
	if (fwupd_release_get_details_url(release) != NULL)
		r.detailsUrl = fwupd_release_get_details_url(release);
	if (fwupd_release_get_source_url(release) != NULL)
		r.sourceUrl = fwupd_release_get_source_url(release);
	if (fwupd_release_get_sbom_url(release) != NULL)
		r.sbomUrl = fwupd_release_get_sbom_url(release);
	if (fwupd_release_get_vendor(release) != NULL)
		r.vendor = fwupd_release_get_vendor(release);
	if (fwupd_release_get_detach_caption(release) != NULL)
		r.detachCaption = fwupd_release_get_detach_caption(release);
	if (fwupd_release_get_detach_image(release) != NULL)
		r.detachImage = fwupd_release_get_detach_image(release);
	if (fwupd_release_get_update_message(release) != NULL)
		r.updateMessage = fwupd_release_get_update_message(release);
	if (fwupd_release_get_update_image(release) != NULL)
		r.updateImage = fwupd_release_get_update_image(release);
	r.size = fwupd_release_get_size(release);
	r.created = fwupd_release_get_created(release);
	r.flags = fwupd_release_get_flags(release);
	r.urgency = fwupd_release_get_urgency(release);
	r.installDuration = fwupd_release_get_install_duration(release);
	return r;
}

FwupdRelease *
fu_binder_release_from_aidl(const aidl_fwupd::FwupdRelease &r, GError **error)
{
	g_autoptr(FwupdRelease) release = fwupd_release_new();
	if (!r.remoteId.empty())
		fwupd_release_set_remote_id(release, r.remoteId.c_str());
	if (!r.name.empty())
		fwupd_release_set_name(release, r.name.c_str());
	if (!r.version.empty())
		fwupd_release_set_version(release, r.version.c_str());
	if (!r.filename.empty())
		fwupd_release_set_filename(release, r.filename.c_str());
	if (r.appstreamId.has_value())
		fwupd_release_set_appstream_id(release, r.appstreamId.value().c_str());
	if (r.releaseId.has_value())
		fwupd_release_set_id(release, r.releaseId.value().c_str());
	if (r.nameVariantSuffix.has_value())
		fwupd_release_set_name_variant_suffix(release, r.nameVariantSuffix.value().c_str());
	if (r.summary.has_value())
		fwupd_release_set_summary(release, r.summary.value().c_str());
	if (r.description.has_value())
		fwupd_release_set_description(release, r.description.value().c_str());
	if (r.branch.has_value())
		fwupd_release_set_branch(release, r.branch.value().c_str());
	if (r.protocol.has_value())
		fwupd_release_set_protocol(release, r.protocol.value().c_str());
	if (r.categories.has_value()) {
		for (const auto &category : r.categories.value()) {
			if (category.has_value())
				fwupd_release_add_category(release, category.value().c_str());
		}
	}
	if (r.issues.has_value()) {
		for (const auto &issue : r.issues.value()) {
			if (issue.has_value())
				fwupd_release_add_issue(release, issue.value().c_str());
		}
	}
	if (r.checksums.has_value()) {
		for (const auto &checksum : r.checksums.value()) {
			if (checksum.has_value())
				fwupd_release_add_checksum(release, checksum.value().c_str());
		}
	}
	if (r.tags.has_value()) {
		for (const auto &tag : r.tags.value()) {
			if (tag.has_value())
				fwupd_release_add_tag(release, tag.value().c_str());
		}
	}
	if (r.license.has_value())
		fwupd_release_set_license(release, r.license.value().c_str());
	if (r.locations.has_value()) {
		for (const auto &loc : r.locations.value()) {
			if (loc.has_value())
				fwupd_release_add_location(release, loc.value().c_str());
		}
	}
	if (r.homepage.has_value())
		fwupd_release_set_homepage(release, r.homepage.value().c_str());
	if (r.detailsUrl.has_value())
		fwupd_release_set_details_url(release, r.detailsUrl.value().c_str());
	if (r.sourceUrl.has_value())
		fwupd_release_set_source_url(release, r.sourceUrl.value().c_str());
	if (r.sbomUrl.has_value())
		fwupd_release_set_sbom_url(release, r.sbomUrl.value().c_str());
	if (r.vendor.has_value())
		fwupd_release_set_vendor(release, r.vendor.value().c_str());
	if (r.detachCaption.has_value())
		fwupd_release_set_detach_caption(release, r.detachCaption.value().c_str());
	if (r.detachImage.has_value())
		fwupd_release_set_detach_image(release, r.detachImage.value().c_str());
	if (r.updateMessage.has_value())
		fwupd_release_set_update_message(release, r.updateMessage.value().c_str());
	if (r.updateImage.has_value())
		fwupd_release_set_update_image(release, r.updateImage.value().c_str());
	if (r.size < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative size %" G_GINT64_FORMAT " from client",
			    r.size);
		return NULL;
	}
	fwupd_release_set_size(release, r.size);
	if (r.created < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative created %" G_GINT64_FORMAT " from client",
			    r.created);
		return NULL;
	}
	fwupd_release_set_created(release, r.created);
	fwupd_release_set_flags(release, (FwupdReleaseFlags)r.flags);
	if (r.urgency < 0 || r.urgency >= FWUPD_RELEASE_URGENCY_LAST) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid FwupdReleaseUrgency %i from client",
			    r.urgency);
		return NULL;
	}
	fwupd_release_set_urgency(release, (FwupdReleaseUrgency)r.urgency);
	if (r.installDuration < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative install duration %i from client",
			    r.installDuration);
		return NULL;
	}
	fwupd_release_set_install_duration(release, r.installDuration);
	return g_steal_pointer(&release);
}

aidl_fwupd::FwupdRequest
fu_binder_request_to_aidl(FwupdRequest *request)
{
	aidl_fwupd::FwupdRequest r;
	r.kind = fwupd_request_get_kind(request);
	if (fwupd_request_get_id(request) != NULL)
		r.id = fwupd_request_get_id(request);
	if (fwupd_request_get_device_id(request) != NULL)
		r.deviceId = fwupd_request_get_device_id(request);
	if (fwupd_request_get_message(request) != NULL)
		r.message = fwupd_request_get_message(request);
	if (fwupd_request_get_image(request) != NULL)
		r.image = fwupd_request_get_image(request);
	r.created = fwupd_request_get_created(request);
	r.flags = fwupd_request_get_flags(request);
	return r;
}

FwupdRequest *
fu_binder_request_from_aidl(const aidl_fwupd::FwupdRequest &r, GError **error)
{
	g_autoptr(FwupdRequest) request = fwupd_request_new();
	if (r.kind < 0 || r.kind >= FWUPD_REQUEST_KIND_LAST) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid FwupdRequestKind %i from client",
			    r.kind);
		return NULL;
	}
	fwupd_request_set_kind(request, (FwupdRequestKind)r.kind);
	if (!r.id.empty())
		fwupd_request_set_id(request, r.id.c_str());
	if (r.deviceId.has_value())
		fwupd_request_set_device_id(request, r.deviceId.value().c_str());
	if (r.message.has_value())
		fwupd_request_set_message(request, r.message.value().c_str());
	if (r.image.has_value())
		fwupd_request_set_image(request, r.image.value().c_str());
	if (r.created < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "negative created %" G_GINT64_FORMAT " from client",
			    r.created);
		return NULL;
	}
	fwupd_request_set_created(request, r.created);
	fwupd_request_set_flags(request, (FwupdRequestFlags)r.flags);
	return g_steal_pointer(&request);
}
