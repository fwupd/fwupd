/*
 * Copyright 2022 Mario Limonciello <mario.limonciello@amd.com>
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBiosSettings"

#include "config.h"

#include <glib/gi18n.h>

#include "fu-bios-setting.h"
#include "fu-context.h"
#include "fu-quirks.h"

typedef struct {
	FuContext *ctx;
} FuBiosSettingPrivate;

enum { PROP_0, PROP_CONTEXT, PROP_LAST };

G_DEFINE_TYPE_WITH_PRIVATE(FuBiosSetting, fu_bios_setting, FWUPD_TYPE_BIOS_SETTING)

#define GET_PRIVATE(o) (fu_bios_setting_get_instance_private(o))

void
fu_bios_setting_set_appstream_id(FuBiosSetting *self, const gchar *appstream_id)
{
	struct {
		const gchar *appstream_id;
		const gchar *description;
		const gchar *icon;
	} map[] = {
	    {
		"org.fwupd.bios.secure-boot",
		N_("Secure Boot"),
		"application-certificate",
	    },
	    {
		"org.fwupd.bios.firmware-update-opt-in",
		N_("Firmware Update Opt-in"),
		"system-software-update",
	    },
	    {
		"org.fwupd.bios.wake-on-lan",
		N_("Wake on LAN"),
		"network-wired",
	    },
	    {
		"org.fwupd.bios.num-lock",
		N_("Num Lock"),
		"input-keyboard",
	    },
	    {
		"org.fwupd.bios.memory-encryption",
		N_("Memory Encryption"),
		"security-high",
	    },
	    {
		"org.fwupd.bios.video-memory",
		N_("Video Memory"),
		"video-display",
	    },
	    {
		"org.fwupd.bios.camera.enabled",
		N_("Camera"),
		"camera-web",
	    },
	    {
		"org.fwupd.bios.microphone.enabled",
		N_("Microphone"),
		"audio-input-microphone",
	    },
	    {
		"org.fwupd.bios.bluetooth.enabled",
		N_("Bluetooth"),
		"bluetooth",
	    },
	    {
		"org.fwupd.bios.wireless-lan.enabled",
		N_("Wireless LAN"),
		"network-wireless",
	    },
	    {
		"org.fwupd.bios.fingerprint-reader.enabled",
		N_("Fingerprint Reader"),
		"auth-fingerprint",
	    },
	    {
		"org.fwupd.bios.touchscreen.enabled",
		N_("Touchscreen"),
		"input-touchpad",
	    },
	    {
		"org.fwupd.bios.audio.enabled",
		N_("Audio"),
		"audio-card",
	    },
	    {
		"org.fwupd.bios.media-card-reader.enabled",
		N_("Media Card Reader"),
		"media-flash",
	    },
	    {
		"org.fwupd.bios.keyboard-backlight.enabled",
		N_("Keyboard Backlight"),
		"input-keyboard",
	    },
	    {
		"com.thinklmi.WindowsUEFIFirmwareUpdate",
		N_("BIOS updates delivered via LVFS or Windows Update"),
		"security-high",
	    },
	};

	g_return_if_fail(FU_IS_BIOS_SETTING(self));

	/* fallbacks */
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		if (g_strcmp0(appstream_id, map[i].appstream_id) == 0) {
			if (map[i].description != NULL)
				fu_bios_setting_set_description(self, map[i].description);
			if (map[i].icon != NULL)
				fu_bios_setting_set_icon(self, map[i].icon);
			break;
		}
	}

	/* success */
	fwupd_bios_setting_set_appstream_id(FWUPD_BIOS_SETTING(self), appstream_id);
}

void
fu_bios_setting_set_id(FuBiosSetting *self, const gchar *id)
{
	FuBiosSettingPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_BIOS_SETTING(self));
	g_return_if_fail(FU_IS_CONTEXT(priv->ctx));

	/* the hardware-assigned setting points at an abstract fwupd AppStream ID */
	if (id != NULL) {
		g_autofree gchar *guid = fwupd_guid_hash_string(id);
		const gchar *appstream_id =
		    fu_context_lookup_quirk_by_id(priv->ctx, guid, FU_QUIRKS_APPSTREAM_ID);
		if (appstream_id != NULL)
			fu_bios_setting_set_appstream_id(self, appstream_id);
	}

	/* success */
	fwupd_bios_setting_set_id(FWUPD_BIOS_SETTING(self), id);
}

void
fu_bios_setting_set_name(FuBiosSetting *self, const gchar *name)
{
	g_return_if_fail(FU_IS_BIOS_SETTING(self));

	/* fixups */
	if (g_strcmp0(name, "pending_reboot") == 0) {
		fu_bios_setting_set_description(self,
						/* TRANSLATORS: description of a BIOS setting */
						_("Settings will apply after system reboots"));
	}

	/* success */
	fwupd_bios_setting_set_name(FWUPD_BIOS_SETTING(self), name);
}

static void
fu_bios_setting_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuBiosSetting *self = FU_BIOS_SETTING(object);
	FuBiosSettingPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_CONTEXT:
		g_value_set_object(value, priv->ctx);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_bios_setting_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuBiosSetting *self = FU_BIOS_SETTING(object);
	FuBiosSettingPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_CONTEXT:
		g_set_object(&priv->ctx, g_value_get_object(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_bios_setting_init(FuBiosSetting *self)
{
}

static void
fu_bios_setting_dispose(GObject *object)
{
	FuBiosSetting *self = FU_BIOS_SETTING(object);
	FuBiosSettingPrivate *priv = GET_PRIVATE(self);
	g_clear_object(&priv->ctx);
	G_OBJECT_CLASS(fu_bios_setting_parent_class)->dispose(object);
}

static void
fu_bios_setting_class_init(FuBiosSettingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->get_property = fu_bios_setting_get_property;
	object_class->set_property = fu_bios_setting_set_property;
	object_class->dispose = fu_bios_setting_dispose;

	/**
	 * FuBiosSetting:context:
	 *
	 * The #FuContent to use.
	 *
	 * Since: 2.1.8
	 */
	pspec =
	    g_param_spec_object("context",
				NULL,
				NULL,
				FU_TYPE_CONTEXT,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_CONTEXT, pspec);
}

FuBiosSetting *
fu_bios_setting_new(gpointer ctx)
{
	g_return_val_if_fail(FU_IS_CONTEXT(ctx), NULL);
	return g_object_new(FU_TYPE_BIOS_SETTING, "context", ctx, NULL);
}
