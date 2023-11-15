/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_LINUX_IPMI_H
#include "fu-ipmi-device.h"
#endif

#include "fu-redfish-backend.h"
#include "fu-redfish-common.h"
#include "fu-redfish-device.h"
#include "fu-redfish-network.h"
#include "fu-redfish-plugin.h"
#include "fu-redfish-smbios.h"
#include "fu-redfish-struct.h"

#define FU_REDFISH_PLUGIN_CLEANUP_RETRIES_DELAY 10 /* seconds */

/* defaults changed here will also be reflected in the fwupd.conf man page */
#define FU_REDFISH_CONFIG_DEFAULT_CA_CHECK		   FALSE
#define FU_REDFISH_CONFIG_DEFAULT_IPMI_DISABLE_CREATE_USER FALSE
#define FU_REDFISH_CONFIG_DEFAULT_MANAGER_RESET_TIMEOUT	   "1800" /* seconds */

struct _FuRedfishPlugin {
	FuPlugin parent_instance;
	FuRedfishBackend *backend;
	FuRedfishSmbios *smbios; /* nullable */
};

G_DEFINE_TYPE(FuRedfishPlugin, fu_redfish_plugin, FU_TYPE_PLUGIN)

static void
fu_redfish_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(plugin);
	fu_backend_add_string(FU_BACKEND(self->backend), idt, str);
	if (self->smbios != NULL) {
		g_autofree gchar *smbios = fu_firmware_to_string(FU_FIRMWARE(self->smbios));
		fu_string_append(str, idt, "Smbios", smbios);
	}
	fu_string_append(str, idt, "Vendor", fu_redfish_backend_get_vendor(self->backend));
	fu_string_append(str, idt, "Version", fu_redfish_backend_get_version(self->backend));
	fu_string_append(str, idt, "UUID", fu_redfish_backend_get_uuid(self->backend));
}

static gchar *
fu_common_generate_password(guint length)
{
	GString *str = g_string_sized_new(length);

	/* get a random password string */
	while (str->len < length) {
		gchar tmp = (gchar)g_random_int_range(0x0, 0xff);
		if (g_ascii_isalnum(tmp))
			g_string_append_c(str, tmp);
	}
	return g_string_free(str, FALSE);
}

static gboolean
fu_redfish_plugin_change_expired(FuPlugin *plugin, GError **error)
{
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(plugin);
	g_autofree gchar *password_new = fu_common_generate_password(15);
	g_autofree gchar *uri = NULL;
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();

	/* select correct, falling back to default for old fwupd versions */
	uri = fu_plugin_get_config_value(plugin, "UserUri", NULL);
	if (uri == NULL) {
		uri = g_strdup("/redfish/v1/AccountService/Accounts/2");
		if (!fu_plugin_set_config_value(plugin, "UserUri", uri, error))
			return FALSE;
	}

	/* now use Redfish to change the temporary password to the actual password */
	request = fu_redfish_backend_request_new(self->backend);
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "Password");
	json_builder_add_string_value(builder, password_new);
	json_builder_end_object(builder);
	if (!fu_redfish_request_perform_full(request,
					     uri,
					     "PATCH",
					     builder,
					     FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON |
						 FU_REDFISH_REQUEST_PERFORM_FLAG_USE_ETAG,
					     error))
		return FALSE;
	fu_redfish_backend_set_password(self->backend, password_new);

	/* success */
	return fu_plugin_set_config_value(plugin, "Password", password_new, error);
}

static gboolean
fu_redfish_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(plugin);
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get the list of devices */
	if (!fu_backend_coldplug(FU_BACKEND(self->backend), progress, &error_local)) {
		/* did the user password expire? */
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_AUTH_EXPIRED)) {
			if (!fu_redfish_plugin_change_expired(plugin, error))
				return FALSE;
			if (!fu_backend_coldplug(FU_BACKEND(self->backend), progress, error)) {
				fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_AUTH_REQUIRED);
				return FALSE;
			}
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}
	devices = fu_backend_get_devices(FU_BACKEND(self->backend));
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (fu_context_has_hwid_flag(fu_plugin_get_context(plugin), "reset-required"))
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
		fu_plugin_device_add(plugin, device);
	}

	/* this is no longer relevant */
	if (devices->len > 0) {
		fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "bios");
		fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "uefi_capsule");
	}
	return TRUE;
}

void
fu_redfish_plugin_set_credentials(FuPlugin *plugin, const gchar *username, const gchar *password)
{
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(plugin);

	fu_redfish_backend_set_username(self->backend, username);
	fu_redfish_backend_set_password(self->backend, password);
}

static gboolean
fu_redfish_plugin_discover_uefi_credentials(FuPlugin *plugin, GError **error)
{
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(plugin);
	gsize bufsz = 0;
	guint32 indications = 0x0;
	g_autofree gchar *userpass_safe = NULL;
	g_autofree guint8 *buf = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GBytes) userpass = NULL;

	/* get the uint32 specifying if there are EFI variables set */
	if (!fu_efivar_get_data(REDFISH_EFI_INFORMATION_GUID,
				REDFISH_EFI_INFORMATION_INDICATIONS,
				&buf,
				&bufsz,
				NULL,
				error))
		return FALSE;
	if (!fu_memread_uint32_safe(buf, bufsz, 0x0, &indications, G_LITTLE_ENDIAN, error))
		return FALSE;
	if ((indications & REDFISH_EFI_INDICATIONS_OS_CREDENTIALS) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no indications for OS credentials");
		return FALSE;
	}

	/* read the correct EFI var for runtime */
	userpass = fu_efivar_get_data_bytes(REDFISH_EFI_INFORMATION_GUID,
					    REDFISH_EFI_INFORMATION_OS_CREDENTIALS,
					    NULL,
					    error);
	if (userpass == NULL)
		return FALSE;

	/* it might not be NUL terminated */
	userpass_safe = g_strndup(g_bytes_get_data(userpass, NULL), g_bytes_get_size(userpass));
	split = g_strsplit(userpass_safe, ":", -1);
	if (g_strv_length(split) != 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid format for username:password, got '%s'",
			    userpass_safe);
		return FALSE;
	}
	fu_redfish_backend_set_username(self->backend, split[0]);
	fu_redfish_backend_set_password(self->backend, split[1]);
	return TRUE;
}

static gboolean
fu_redfish_plugin_discover_smbios_table(FuPlugin *plugin, GError **error)
{
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *smbios_data_fn;
	g_autoptr(GPtrArray) type42_tables = NULL;

	/* in self tests */
	smbios_data_fn = g_getenv("FWUPD_REDFISH_SMBIOS_DATA");
	if (smbios_data_fn != NULL) {
		g_autoptr(GBytes) type42_blob = fu_bytes_get_contents(smbios_data_fn, error);
		g_autoptr(FuRedfishSmbios) smbios = fu_redfish_smbios_new();
		if (type42_blob == NULL)
			return FALSE;
		if (!fu_firmware_parse(FU_FIRMWARE(smbios),
				       type42_blob,
				       FWUPD_INSTALL_FLAG_NO_SEARCH,
				       error)) {
			g_prefix_error(error, "failed to parse SMBIOS entry type 42: ");
			return FALSE;
		}
		g_set_object(&self->smbios, smbios);
		return TRUE;
	}

	/* is optional */
	type42_tables = fu_context_get_smbios_data(ctx, REDFISH_SMBIOS_TABLE_TYPE, NULL);
	if (type42_tables == NULL)
		return TRUE;
	for (guint i = 0; i < type42_tables->len; i++) {
		GBytes *type42_blob = g_ptr_array_index(type42_tables, i);
		g_autoptr(FuRedfishSmbios) smbios = fu_redfish_smbios_new();
		if (!fu_firmware_parse(FU_FIRMWARE(smbios),
				       type42_blob,
				       FWUPD_INSTALL_FLAG_NO_SEARCH,
				       error)) {
			g_prefix_error(error, "failed to parse SMBIOS entry type 42: ");
			return FALSE;
		}
		if (fu_redfish_smbios_get_interface_type(smbios) ==
		    FU_REDFISH_SMBIOS_INTERFACE_TYPE_NETWORK) {
			g_set_object(&self->smbios, smbios);
			return TRUE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_redfish_plugin_autoconnect_network_device(FuRedfishPlugin *self, GError **error)
{
	g_autofree gchar *hostname = NULL;
	g_autoptr(FuRedfishNetworkDevice) device = NULL;

	/* we have no data */
	if (self->smbios == NULL)
		return TRUE;

	/* get IP, falling back to hostname, then MAC, then VID:PID */
	hostname = g_strdup(fu_redfish_smbios_get_ip_addr(self->smbios));
	if (hostname == NULL)
		hostname = g_strdup(fu_redfish_smbios_get_hostname(self->smbios));
	if (device == NULL) {
		const gchar *mac_addr = fu_redfish_smbios_get_mac_addr(self->smbios);
		if (mac_addr != NULL) {
			g_autoptr(GError) error_network = NULL;
			device = fu_redfish_network_device_for_mac_addr(mac_addr, &error_network);
			if (device == NULL)
				g_debug("failed to get device: %s", error_network->message);
		}
	}
	if (device == NULL) {
		guint16 vid = fu_redfish_smbios_get_vid(self->smbios);
		guint16 pid = fu_redfish_smbios_get_pid(self->smbios);
		if (vid != 0x0 && pid != 0x0) {
			g_autoptr(GError) error_network = NULL;
			device = fu_redfish_network_device_for_vid_pid(vid, pid, &error_network);
			if (device == NULL)
				g_debug("failed to get device: %s", error_network->message);
		}
	}

	/* autoconnect device if required */
	if (device != NULL) {
		FuRedfishNetworkDeviceState state = FU_REDFISH_NETWORK_DEVICE_STATE_UNKNOWN;
		if (!fu_redfish_network_device_get_state(device, &state, error))
			return FALSE;
		g_info("device state is now %s [%u]",
		       fu_redfish_network_device_state_to_string(state),
		       state);
		if (state == FU_REDFISH_NETWORK_DEVICE_STATE_DISCONNECTED) {
			if (!fu_redfish_network_device_connect(device, error))
				return FALSE;
		}
		if (hostname == NULL) {
			hostname = fu_redfish_network_device_get_address(device, error);
			if (hostname == NULL)
				return FALSE;
		}
	}
	if (hostname == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "no hostname");
		return FALSE;
	}
	fu_redfish_backend_set_hostname(self->backend, hostname);
	fu_redfish_backend_set_port(self->backend, fu_redfish_smbios_get_port(self->smbios));
	return TRUE;
}

#ifdef HAVE_LINUX_IPMI_H

static gboolean
fu_redfish_plugin_ipmi_create_user(FuPlugin *plugin, GError **error)
{
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(plugin);
	const gchar *username_fwupd = "fwupd";
	guint8 user_id = G_MAXUINT8;
	g_autofree gchar *password_new = fu_common_generate_password(15);
	g_autofree gchar *password_tmp = fu_common_generate_password(15);
	g_autofree gchar *uri = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuIpmiDevice) device = fu_ipmi_device_new(fu_plugin_get_context(plugin));
	g_autoptr(FuRedfishRequest) request = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();

	/* create device */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	/* check for existing user, and if not then remember the first spare slot */
	for (guint8 i = 2; i < 0xFF; i++) {
		g_autofree gchar *username = fu_ipmi_device_get_user_password(device, i, NULL);
		if (username == NULL && user_id == G_MAXUINT8) {
			g_debug("KCS slot %u free", i);
			user_id = i;
			continue;
		}
		if (g_strcmp0(username, "fwupd") == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "fwupd user already exists in KCS slot %u",
				    (guint)i);
			return FALSE;
		}
	}
	if (user_id == G_MAXUINT8) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "all KCS slots full, cannot create user");
		return FALSE;
	}

	/* create a user with appropriate permissions */
	if (!fu_ipmi_device_set_user_name(device, user_id, username_fwupd, error))
		return FALSE;
	if (!fu_ipmi_device_set_user_enable(device, user_id, TRUE, error))
		return FALSE;
	if (!fu_ipmi_device_set_user_priv(device, user_id, 0x4, 1, error))
		return FALSE;
	if (!fu_ipmi_device_set_user_password(device, user_id, password_tmp, error))
		return FALSE;
	/* OEM specific for Advantech manufacture */
	if (fu_context_has_hwid_guid(fu_plugin_get_context(plugin),
				     "18789130-a714-53c0-b025-fa93801d3995")) {
		if (!fu_redfish_device_set_user_group_redfish_enable_advantech(device,
									       user_id,
									       error))
			return FALSE;
	}
	fu_redfish_backend_set_username(self->backend, username_fwupd);
	fu_redfish_backend_set_password(self->backend, password_tmp);

	/* wait for Redfish to sync */
	g_usleep(2 * G_USEC_PER_SEC);

	/* XCC is the only BMC implementation that does not map the user_ids 1:1 */
	if (fu_context_has_hwid_guid(fu_plugin_get_context(plugin),
				     "42f00735-c9ab-5374-bd63-a5deee5881e0"))
		user_id -= 1;

	/* now use Redfish to change the temporary password to the actual password */
	request = fu_redfish_backend_request_new(self->backend);
	uri = g_strdup_printf("/redfish/v1/AccountService/Accounts/%u", (guint)user_id);
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "Password");
	json_builder_add_string_value(builder, password_new);
	json_builder_end_object(builder);
	if (!fu_redfish_request_perform_full(request,
					     uri,
					     "PATCH",
					     builder,
					     FU_REDFISH_REQUEST_PERFORM_FLAG_LOAD_JSON |
						 FU_REDFISH_REQUEST_PERFORM_FLAG_USE_ETAG,
					     error))
		return FALSE;
	fu_redfish_backend_set_password(self->backend, password_new);

	/* success */
	if (!fu_plugin_set_config_value(plugin, "UserUri", uri, error))
		return FALSE;
	if (!fu_plugin_set_config_value(plugin, "Username", username_fwupd, error))
		return FALSE;
	if (!fu_plugin_set_config_value(plugin, "Password", password_new, error))
		return FALSE;

	return TRUE;
}
#endif

static gboolean
fu_redfish_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(plugin);
	g_autofree gchar *password = NULL;
	g_autofree gchar *redfish_uri = NULL;
	g_autofree gchar *username = NULL;
	g_autoptr(GError) error_uefi = NULL;

	/* optional */
	if (!fu_redfish_plugin_discover_smbios_table(plugin, error))
		return FALSE;
	if (!fu_redfish_plugin_autoconnect_network_device(self, error))
		return FALSE;
	if (!fu_redfish_plugin_discover_uefi_credentials(plugin, &error_uefi)) {
		g_debug("failed to get username and password automatically: %s",
			error_uefi->message);
	}

	/* override with the conf file */
	redfish_uri = fu_plugin_get_config_value(plugin, "Uri", NULL);
	if (redfish_uri != NULL) {
		const gchar *ip_str = NULL;
		g_auto(GStrv) split = NULL;
		guint64 port = 0;

		if (g_str_has_prefix(redfish_uri, "https://")) {
			fu_redfish_backend_set_https(self->backend, TRUE);
			ip_str = redfish_uri + strlen("https://");
			port = 443;
		} else if (g_str_has_prefix(redfish_uri, "http://")) {
			fu_redfish_backend_set_https(self->backend, FALSE);
			ip_str = redfish_uri + strlen("http://");
			port = 80;
		} else {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid scheme");
			return FALSE;
		}

		split = g_strsplit(ip_str, ":", 2);
		fu_redfish_backend_set_hostname(self->backend, split[0]);
		if (g_strv_length(split) > 1)
			port = g_ascii_strtoull(split[1], NULL, 10);
		if (port == 0 || port == G_MAXUINT64) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "no valid port specified");
			return FALSE;
		}
		fu_redfish_backend_set_port(self->backend, port);
	}
	username = fu_plugin_get_config_value(plugin, "Username", NULL);
	if (username != NULL)
		fu_redfish_backend_set_username(self->backend, username);
	password = fu_plugin_get_config_value(plugin, "Password", NULL);
	if (password != NULL)
		fu_redfish_backend_set_password(self->backend, password);
	fu_redfish_backend_set_cacheck(
	    self->backend,
	    fu_plugin_get_config_value_boolean(plugin,
					       "CACheck",
					       FU_REDFISH_CONFIG_DEFAULT_CA_CHECK));
	if (fu_context_has_hwid_flag(fu_plugin_get_context(plugin), "wildcard-targets"))
		fu_redfish_backend_set_wildcard_targets(self->backend, TRUE);

#ifdef HAVE_LINUX_IPMI_H
	/* we got neither a type 42 entry or config value, lets try IPMI */
	if (fu_redfish_backend_get_username(self->backend) == NULL) {
		if (!fu_context_has_hwid_flag(fu_plugin_get_context(plugin), "ipmi-create-user")) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "no username and password specified, "
					    "and no vendor quirk for 'ipmi-create-user'");
			return FALSE;
		}
		if (!fu_plugin_get_config_value_boolean(
			plugin,
			"IpmiDisableCreateUser",
			FU_REDFISH_CONFIG_DEFAULT_IPMI_DISABLE_CREATE_USER)) {
			g_info("attempting to create user using IPMI");
			if (!fu_redfish_plugin_ipmi_create_user(plugin, error))
				return FALSE;
		}
	}
#endif

	return fu_backend_setup(FU_BACKEND(self->backend), progress, error);
}

static gboolean
fu_redfish_plugin_cleanup_setup_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(user_data);
	FuProgress *progress = fu_progress_new(G_STRLOC);

	/* the network adaptor might not auto-connect when coming back */
	if (!fu_redfish_plugin_autoconnect_network_device(self, error))
		return FALSE;
	return fu_backend_setup(FU_BACKEND(self->backend), progress, error);
}

static gboolean
fu_redfish_plugin_cleanup_coldplug_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(user_data);
	FuProgress *progress = fu_progress_new(G_STRLOC);
	if (!fu_redfish_plugin_autoconnect_network_device(self, error))
		return FALSE;
	return fu_redfish_plugin_coldplug(FU_PLUGIN(self), progress, error);
}

static gboolean
fu_redfish_plugin_cleanup(FuPlugin *plugin,
			  FuDevice *device,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(plugin);
	guint64 reset_timeout = 0;
	g_autofree gchar *restart_timeout_str = NULL;
	g_autoptr(FuRedfishRequest) request = fu_redfish_backend_request_new(self->backend);
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(GPtrArray) devices = NULL;

	/* nothing to do */
	if (!fu_device_has_private_flag(device, FU_REDFISH_DEVICE_FLAG_MANAGER_RESET))
		return TRUE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "manager-reboot");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "pre-delay");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 67, "poll-manager");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 18, "post-delay");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 9, "recoldplug");

	/* ask the BMC to reboot */
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "ResetType");
	json_builder_add_string_value(builder, "ForceRestart");
	json_builder_end_object(builder);
	if (!fu_redfish_request_perform_full(request,
					     "/redfish/v1/Managers/1/Actions/Manager.Reset",
					     "POST",
					     builder,
					     FU_REDFISH_REQUEST_PERFORM_FLAG_NONE,
					     error)) {
		g_prefix_error(error, "failed to reset manager: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* remove all the devices */
	devices = fu_backend_get_devices(FU_BACKEND(self->backend));
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		fu_backend_device_removed(FU_BACKEND(self->backend), device_tmp);
	}

	/* work around manager bugs... */
	fu_backend_invalidate(FU_BACKEND(self->backend));
	fu_device_sleep_full(device,
			     fu_redfish_device_get_reset_pre_delay(FU_REDFISH_DEVICE(device)),
			     fu_progress_get_child(progress));
	fu_progress_step_done(progress);

	/* read the config file to work out how long to wait */
	restart_timeout_str =
	    fu_plugin_get_config_value(plugin,
				       "ManagerResetTimeout",
				       FU_REDFISH_CONFIG_DEFAULT_MANAGER_RESET_TIMEOUT);
	if (!fu_strtoull(restart_timeout_str, &reset_timeout, 1, 86400, error))
		return FALSE;

	/* wait for the BMC to come back */
	if (!fu_device_retry_full(device,
				  fu_redfish_plugin_cleanup_setup_cb,
				  reset_timeout / FU_REDFISH_PLUGIN_CLEANUP_RETRIES_DELAY,
				  FU_REDFISH_PLUGIN_CLEANUP_RETRIES_DELAY * 1000,
				  self,
				  error)) {
		g_prefix_error(error, "manager failed to come back from setup: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* work around manager bugs... */
	fu_device_sleep_full(device,
			     fu_redfish_device_get_reset_post_delay(FU_REDFISH_DEVICE(device)),
			     fu_progress_get_child(progress));
	fu_progress_step_done(progress);

	/* get the new list of devices */
	if (!fu_device_retry_full(device,
				  fu_redfish_plugin_cleanup_coldplug_cb,
				  reset_timeout / FU_REDFISH_PLUGIN_CLEANUP_RETRIES_DELAY,
				  FU_REDFISH_PLUGIN_CLEANUP_RETRIES_DELAY * 1000,
				  self,
				  error)) {
		g_prefix_error(error, "manager failed to come back from coldplug: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_redfish_plugin_init(FuRedfishPlugin *self)
{
}

static void
fu_redfish_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(plugin);
	fu_context_add_quirk_key(ctx, "RedfishResetPreDelay");
	fu_context_add_quirk_key(ctx, "RedfishResetPostDelay");
	self->backend = fu_redfish_backend_new(ctx);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_REDFISH_SMBIOS);
	fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_SECURE_CONFIG);
}

static void
fu_redfish_finalize(GObject *obj)
{
	FuRedfishPlugin *self = FU_REDFISH_PLUGIN(obj);
	if (self->smbios != NULL)
		g_object_unref(self->smbios);
	if (self->backend != NULL)
		g_object_unref(self->backend);
	G_OBJECT_CLASS(fu_redfish_plugin_parent_class)->finalize(obj);
}

static void
fu_redfish_plugin_class_init(FuRedfishPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_redfish_finalize;
	plugin_class->constructed = fu_redfish_plugin_constructed;
	plugin_class->to_string = fu_redfish_plugin_to_string;
	plugin_class->startup = fu_redfish_plugin_startup;
	plugin_class->coldplug = fu_redfish_plugin_coldplug;
	plugin_class->cleanup = fu_redfish_plugin_cleanup;
}
