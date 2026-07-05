/*
 * Copyright 2026 Luca Boccassi <luca.boccassi@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <systemd/sd-json.h>
#include <systemd/sd-varlink-idl.h>
#include <systemd/sd-varlink.h>

#include "fu-systemd-pcrlock-plugin.h"

struct _FuSystemdPcrlockPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSystemdPcrlockPlugin, fu_systemd_pcrlock_plugin, FU_TYPE_PLUGIN)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(sd_varlink, sd_varlink_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(sd_varlink_interface, sd_varlink_interface_free)

/* The privileged Varlink service provided by systemd-pcrlock is
 * socket-activated and only present when the system was booted with a measured
 * UKI (ConditionSecurity=measured-uki) */
#define FU_SYSTEMD_PCRLOCK_VARLINK_ADDRESS "/run/systemd/io.systemd.PCRLock"

static gboolean
fu_systemd_pcrlock_plugin_check_reply(int r,
				      const char *error_id,
				      const gchar *method,
				      GError **error)
{
	if (r < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to call %s: %s",
			    method,
			    fwupd_strerror(-r));
		return FALSE;
	}
	if (error_id != NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s returned %s",
			    method,
			    error_id);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_systemd_pcrlock_plugin_unlock(sd_varlink *vl, const gchar *category, GError **error)
{
	const char *error_id = NULL;
	int r;

	/* Removing the .pcrlock drop-in (Lock=false) stops the regenerated policy
	 * from requiring the current measurements for this category, so the disk
	 * can still be unlocked once the update has been applied and the PCRs have
	 * changed */
	r = sd_varlink_callbo(vl,
			      "io.systemd.PCRLock.Lock",
			      /* ret_parameters= */ NULL,
			      &error_id,
			      SD_JSON_BUILD_PAIR_STRING("category", category),
			      SD_JSON_BUILD_PAIR_BOOLEAN("lock", FALSE));
	return fu_systemd_pcrlock_plugin_check_reply(r, error_id, "io.systemd.PCRLock.Lock", error);
}

static gboolean
fu_systemd_pcrlock_plugin_lock(sd_varlink *vl, const gchar *category, GError **error)
{
	const char *error_id = NULL;
	int r;

	/* opposite of fu_systemd_pcrlock_plugin_unlock() */
	r = sd_varlink_callbo(vl,
			      "io.systemd.PCRLock.Lock",
			      /* ret_parameters= */ NULL,
			      &error_id,
			      SD_JSON_BUILD_PAIR_STRING("category", category),
			      SD_JSON_BUILD_PAIR_BOOLEAN("lock", TRUE));
	return fu_systemd_pcrlock_plugin_check_reply(r, error_id, "io.systemd.PCRLock.Lock", error);
}

static gboolean
fu_systemd_pcrlock_plugin_make_policy(sd_varlink *vl, GError **error)
{
	const char *error_id = NULL;
	int r;

	/* regenerate the policy from the remaining .pcrlock drop-ins and reseal it into the TPM */
	r = sd_varlink_call(vl,
			    "io.systemd.PCRLock.MakePolicy",
			    /* parameters= */ NULL,
			    /* ret_parameters= */ NULL,
			    &error_id);
	if (r < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to call io.systemd.PCRLock.MakePolicy: %s",
			    fwupd_strerror(-r));
		return FALSE;
	}

	/* the policy was already up to date, which is not an error for us */
	if (g_strcmp0(error_id, "io.systemd.PCRLock.NoChange") == 0) {
		g_debug("systemd-pcrlock policy already up to date, nothing to do");
		return TRUE;
	}
	return fu_systemd_pcrlock_plugin_check_reply(r,
						     error_id,
						     "io.systemd.PCRLock.MakePolicy",
						     error);
}

static gboolean
fu_systemd_pcrlock_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const char *error_id = NULL;
	const char *description;
	sd_json_variant *reply = NULL;
	const gchar *address = g_getenv("FWUPD_SYSTEMD_PCRLOCK_VARLINK_ADDRESS");
	int r;
	g_autoptr(sd_varlink) vl = NULL;
	g_autoptr(sd_varlink_interface) interface = NULL;

	/* the plugin is only useful when systemd-pcrlock is protecting this system */
	if (!fu_context_has_flag(ctx, FU_CONTEXT_FLAG_FDE_SYSTEMD_PCRLOCK)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "systemd-pcrlock is not being used to protect this system");
		return FALSE;
	}

	if (address == NULL)
		address = FU_SYSTEMD_PCRLOCK_VARLINK_ADDRESS;

	r = sd_varlink_connect_address(&vl, address);
	if (r < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to connect to %s: %s",
			    address,
			    fwupd_strerror(-r));
		return FALSE;
	}

	/* introspect the interface: an older systemd-pcrlock does not implement the
	 * Lock method (or the interface at all), so disable the plugin rather than
	 * failing an update later on */
	r = sd_varlink_callbo(vl,
			      "org.varlink.service.GetInterfaceDescription",
			      &reply,
			      &error_id,
			      SD_JSON_BUILD_PAIR_STRING("interface", "io.systemd.PCRLock"));
	if (r < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to introspect io.systemd.PCRLock: %s",
			    fwupd_strerror(-r));
		return FALSE;
	}
	if (error_id == NULL) {
		description = sd_json_variant_string(sd_json_variant_by_key(reply, "description"));
		if (description == NULL) {
			g_set_error_literal(
			    error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "io.systemd.PCRLock introspection returned no description");
			return FALSE;
		}
		r = sd_varlink_idl_parse(description,
					 /* reterr_line= */ NULL,
					 /* reterr_column= */ NULL,
					 &interface);
		if (r < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to parse the io.systemd.PCRLock interface: %s",
				    fwupd_strerror(-r));
			return FALSE;
		}
		for (const sd_varlink_symbol *const *symbol = interface->symbols; *symbol != NULL;
		     symbol++) {
			if ((*symbol)->symbol_type == SD_VARLINK_METHOD &&
			    g_strcmp0((*symbol)->name, "Lock") == 0)
				return TRUE;
		}
	} else if (g_strcmp0(error_id, "org.varlink.service.InterfaceNotFound") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to introspect io.systemd.PCRLock: %s",
			    error_id);
		return FALSE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "systemd-pcrlock is too old to support the Lock method");
	return FALSE;
}

/* used to rollback unlocks in case any fails, to avoid leaving intermediate state around */
typedef struct {
	sd_varlink *vl; /* noref */
	const gchar *name;
} FuSystemdPcrlockRelock;

static void
fu_systemd_pcrlock_plugin_relock_free(FuSystemdPcrlockRelock *self)
{
	if (self->vl != NULL)
		(void)fu_systemd_pcrlock_plugin_lock(self->vl, self->name, NULL);
	g_free(self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuSystemdPcrlockRelock, fu_systemd_pcrlock_plugin_relock_free)

static gboolean
fu_systemd_pcrlock_plugin_relock(gboolean unlock_firmware,
				 gboolean unlock_secureboot,
				 GError **error)
{
	const gchar *address = g_getenv("FWUPD_SYSTEMD_PCRLOCK_VARLINK_ADDRESS");
	int r;
	g_autoptr(sd_varlink) vl = NULL;
	g_autoptr(FuSystemdPcrlockRelock) firmware_code_rollback = NULL;
	g_autoptr(FuSystemdPcrlockRelock) firmware_config_rollback = NULL;
	g_autoptr(FuSystemdPcrlockRelock) secureboot_policy_rollback = NULL;
	g_autoptr(FuSystemdPcrlockRelock) secureboot_authority_rollback = NULL;

	if (address == NULL)
		address = FU_SYSTEMD_PCRLOCK_VARLINK_ADDRESS;

	r = sd_varlink_connect_address(&vl, address);
	if (r < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to connect to %s: %s",
			    address,
			    fwupd_strerror(-r));
		return FALSE;
	}

	/* TODO: check if any of these are actually locked before unlocking once API is available */

	/* remove the drop-ins for the affected measurements (firmware = PCRs 0, 2, 4
	 * and 1, 3, 5 and SecureBoot = PCR 7), arming a rollback guard after each unlock
	 * so anything already removed is re-locked if a later step fails, to avoid leaving
	 * intermediate state around */
	if (unlock_firmware) {
		if (!fu_systemd_pcrlock_plugin_unlock(vl, "firmwareCode", error))
			return FALSE;
		firmware_code_rollback = g_new0(FuSystemdPcrlockRelock, 1);
		firmware_code_rollback->vl = vl;
		firmware_code_rollback->name = "firmwareCode";

		if (!fu_systemd_pcrlock_plugin_unlock(vl, "firmwareConfig", error))
			return FALSE;
		firmware_config_rollback = g_new0(FuSystemdPcrlockRelock, 1);
		firmware_config_rollback->vl = vl;
		firmware_config_rollback->name = "firmwareConfig";
	}
	if (unlock_secureboot) {
		if (!fu_systemd_pcrlock_plugin_unlock(vl, "secureBootPolicy", error))
			return FALSE;
		secureboot_policy_rollback = g_new0(FuSystemdPcrlockRelock, 1);
		secureboot_policy_rollback->vl = vl;
		secureboot_policy_rollback->name = "secureBootPolicy";

		if (!fu_systemd_pcrlock_plugin_unlock(vl, "secureBootAuthority", error))
			return FALSE;
		secureboot_authority_rollback = g_new0(FuSystemdPcrlockRelock, 1);
		secureboot_authority_rollback->vl = vl;
		secureboot_authority_rollback->name = "secureBootAuthority";
	}

	if (!fu_systemd_pcrlock_plugin_make_policy(vl, error))
		return FALSE;

	/* success, disarm the rollbacks */
	if (firmware_code_rollback != NULL)
		firmware_code_rollback->vl = NULL;
	if (firmware_config_rollback != NULL)
		firmware_config_rollback->vl = NULL;
	if (secureboot_policy_rollback != NULL)
		secureboot_policy_rollback->vl = NULL;
	if (secureboot_authority_rollback != NULL)
		secureboot_authority_rollback->vl = NULL;
	return TRUE;
}

static gboolean
fu_systemd_pcrlock_plugin_composite_prepare(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	gboolean unlock_firmware = FALSE;
	gboolean unlock_secureboot = FALSE;

	/* work out what kind of measurements the updates about to be applied will change */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		gboolean fw;
		gboolean sb;

		/* only devices that change the measured-boot state */
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_AFFECTS_FDE))
			continue;

		/* plugins opt in to the specific measurements their updates change */
		fw = fu_device_has_private_flag(device,
						FU_DEVICE_PRIVATE_FLAG_REQUIRES_UNLOCK_FIRMWARE);
		sb = fu_device_has_private_flag(device,
						FU_DEVICE_PRIVATE_FLAG_REQUIRES_UNLOCK_SECUREBOOT);

		/* affects the measured-boot state but did not say how, so be conservative */
		if (!fw && !sb) {
			fw = TRUE;
			sb = TRUE;
		}
		if (fw)
			unlock_firmware = TRUE;
		if (sb)
			unlock_secureboot = TRUE;
	}

	/* nothing being updated changes a measurement that pcrlock is locking */
	if (!unlock_firmware && !unlock_secureboot)
		return TRUE;

	/* Loosen and regenerate the policy before the update is applied, as if this
	 * fails the update must be aborted, as otherwise the measurements would
	 * change on the next boot and no longer satisfy the still-sealed policy,
	 * and the disk could not be unlocked */
	if (!fu_systemd_pcrlock_plugin_relock(unlock_firmware, unlock_secureboot, error)) {
		g_prefix_error_literal(error,
				       "failed to update systemd-pcrlock policy for firmware "
				       "update: ");
		return FALSE;
	}

	return TRUE;
}

static void
fu_systemd_pcrlock_plugin_init(FuSystemdPcrlockPlugin *self)
{
}

static void
fu_systemd_pcrlock_plugin_class_init(FuSystemdPcrlockPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->startup = fu_systemd_pcrlock_plugin_startup;
	plugin_class->composite_prepare = fu_systemd_pcrlock_plugin_composite_prepare;
}
