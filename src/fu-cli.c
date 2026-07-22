/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCli"

#include "config.h"

#include <glib/gi18n.h>
#ifdef HAVE_GIO_UNIX
#include <glib-unix.h>
#endif
#include <curl/curl.h>

#include "fu-bios-settings-private.h"
#include "fu-cli-common.h"
#include "fu-cli.h"
#include "fu-console.h"

typedef struct {
	GCancellable *cancellable;
	GMainContext *main_ctx;
	GMainLoop *loop;
	GSource *source_sigint;
	FwupdClientDownloadFlags download_flags;
	FwupdClient *client;
	/* only valid in update and downgrade */
	FuCliOperation current_operation;
	FwupdDevice *current_device;
	GPtrArray *post_requests;
	FwupdDeviceFlags completion_flags;
	GPtrArray *cmd_array;
	FuConsole *console;
	FuCliArgFlag arg_flags;
	FwupdDeviceFlags filter_device_include;
	FwupdDeviceFlags filter_device_exclude;
	FwupdReleaseFlags filter_release_include;
	FwupdReleaseFlags filter_release_exclude;
	GPtrArray *filter_protocols_include;
	GPtrArray *filter_protocols_exclude;
} FuCliPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCli, fu_cli, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_cli_get_instance_private(o))

static gboolean
fu_cli_report_history(FuCli *self, gchar **values, GError **error);
static FwupdDevice *
fu_cli_get_device_by_id(FuCli *self, const gchar *id, GError **error);

gboolean
fu_cli_has_arg_flag(FuCli *self, FuCliArgFlag arg_flag)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return (priv->arg_flags & arg_flag) > 0;
}

void
fu_cli_add_arg_flag(FuCli *self, FuCliArgFlag arg_flag)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	priv->arg_flags |= arg_flag;
}

/**
 * fu_cli_time_to_str:
 * @tmp: the time in seconds
 *
 * Converts a timestamp to a 'pretty' translated string
 *
 * Returns: (transfer full): A string
 **/
static gchar *
fu_cli_time_to_str(guint64 tmp)
{
	g_return_val_if_fail(tmp != 0, NULL);

	/* seconds */
	if (tmp < 60) {
		/* TRANSLATORS: duration in seconds */
		return g_strdup_printf(ngettext("%u second", "%u seconds", (gint)tmp), (guint)tmp);
	}

	/* minutes */
	tmp /= 60;
	if (tmp < 60) {
		/* TRANSLATORS: duration in minutes */
		return g_strdup_printf(ngettext("%u minute", "%u minutes", (gint)tmp), (guint)tmp);
	}

	/* hours */
	tmp /= 60;
	if (tmp < 60) {
		/* TRANSLATORS: duration in minutes */
		return g_strdup_printf(ngettext("%u hour", "%u hours", (gint)tmp), (guint)tmp);
	}

	/* days */
	tmp /= 24;
	/* TRANSLATORS: duration in days! */
	return g_strdup_printf(ngettext("%u day", "%u days", (gint)tmp), (guint)tmp);
}

static gchar *
fu_cli_release_get_name(FwupdRelease *release)
{
	const gchar *name = fwupd_release_get_name(release);
	GPtrArray *cats = fwupd_release_get_categories(release);

	for (guint i = 0; i < cats->len; i++) {
		const gchar *cat = g_ptr_array_index(cats, i);
		if (g_strcmp0(cat, "X-Device") == 0) {
			/* TRANSLATORS: a specific part of hardware,
			 * the first %s is the device name, e.g. 'Unifying Receiver` */
			return g_strdup_printf(_("%s Device Update"), name);
		}
		if (g_strcmp0(cat, "X-Configuration") == 0) {
			/* TRANSLATORS: a specific part of hardware,
			 * the first %s is the device name, e.g. 'Secure Boot` */
			return g_strdup_printf(_("%s Configuration Update"), name);
		}
		if (g_strcmp0(cat, "X-System") == 0) {
			/* TRANSLATORS: the entire system, e.g. all internal devices,
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf(_("%s System Update"), name);
		}
		if (g_strcmp0(cat, "X-EmbeddedController") == 0) {
			/* TRANSLATORS: the EC is typically the keyboard controller chip,
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf(_("%s Embedded Controller Update"), name);
		}
		if (g_strcmp0(cat, "X-ManagementEngine") == 0) {
			/* TRANSLATORS: ME stands for Management Engine, the Intel AMT thing,
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf(_("%s ME Update"), name);
		}
		if (g_strcmp0(cat, "X-CorporateManagementEngine") == 0) {
			/* TRANSLATORS: ME stands for Management Engine (with Intel AMT),
			 * where the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf(_("%s Corporate ME Update"), name);
		}
		if (g_strcmp0(cat, "X-ConsumerManagementEngine") == 0) {
			/* TRANSLATORS: ME stands for Management Engine, where
			 * the first %s is the device name, e.g. 'ThinkPad P50` */
			return g_strdup_printf(_("%s Consumer ME Update"), name);
		}
		if (g_strcmp0(cat, "X-Controller") == 0) {
			/* TRANSLATORS: the controller is a device that has other devices
			 * plugged into it, for example ThunderBolt, FireWire or USB,
			 * the first %s is the device name, e.g. 'Intel ThunderBolt` */
			return g_strdup_printf(_("%s Controller Update"), name);
		}
		if (g_strcmp0(cat, "X-ThunderboltController") == 0) {
			/* TRANSLATORS: the Thunderbolt controller is a device that
			 * has other high speed Thunderbolt devices plugged into it;
			 * the first %s is the system name, e.g. 'ThinkPad P50` */
			return g_strdup_printf(_("%s Thunderbolt Controller Update"), name);
		}
		if (g_strcmp0(cat, "X-CpuMicrocode") == 0) {
			/* TRANSLATORS: the CPU microcode is firmware loaded onto the CPU
			 * at system boot-up */
			return g_strdup_printf(_("%s CPU Microcode Update"), name);
		}
		if (g_strcmp0(cat, "X-Battery") == 0) {
			/* TRANSLATORS: battery refers to the system power source */
			return g_strdup_printf(_("%s Battery Update"), name);
		}
		if (g_strcmp0(cat, "X-Camera") == 0) {
			/* TRANSLATORS: camera can refer to the laptop internal
			 * camera in the bezel or external USB webcam */
			return g_strdup_printf(_("%s Camera Update"), name);
		}
		if (g_strcmp0(cat, "X-TPM") == 0) {
			/* TRANSLATORS: TPM refers to a Trusted Platform Module */
			return g_strdup_printf(_("%s TPM Update"), name);
		}
		if (g_strcmp0(cat, "X-Touchpad") == 0) {
			/* TRANSLATORS: TouchPad refers to a flat input device */
			return g_strdup_printf(_("%s Touchpad Update"), name);
		}
		if (g_strcmp0(cat, "X-Mouse") == 0) {
			/* TRANSLATORS: Mouse refers to a handheld input device */
			return g_strdup_printf(_("%s Mouse Update"), name);
		}
		if (g_strcmp0(cat, "X-Keyboard") == 0) {
			/* TRANSLATORS: Keyboard refers to an input device for typing */
			return g_strdup_printf(_("%s Keyboard Update"), name);
		}
		if (g_strcmp0(cat, "X-StorageController") == 0) {
			/* TRANSLATORS: Storage Controller is typically a RAID or SAS adapter */
			return g_strdup_printf(_("%s Storage Controller Update"), name);
		}
		if (g_strcmp0(cat, "X-NetworkInterface") == 0) {
			/* TRANSLATORS: Network Interface refers to the physical
			 * PCI card, not the logical wired connection */
			return g_strdup_printf(_("%s Network Interface Update"), name);
		}
		if (g_strcmp0(cat, "X-VideoDisplay") == 0) {
			/* TRANSLATORS: Video Display refers to the laptop internal display or
			 * external monitor */
			return g_strdup_printf(_("%s Display Update"), name);
		}
		if (g_strcmp0(cat, "X-BaseboardManagementController") == 0) {
			/* TRANSLATORS: BMC refers to baseboard management controller which
			 * is the device that updates all the other firmware on the system */
			return g_strdup_printf(_("%s BMC Update"), name);
		}
		if (g_strcmp0(cat, "X-UsbReceiver") == 0) {
			/* TRANSLATORS: Receiver refers to a radio device, e.g. a tiny Bluetooth
			 * device that stays in the USB port so the wireless peripheral works */
			return g_strdup_printf(_("%s USB Receiver Update"), name);
		}
		if (g_strcmp0(cat, "X-Drive") == 0) {
			/* TRANSLATORS: drive refers to a storage device, e.g. SATA disk */
			return g_strdup_printf(_("%s Drive Update"), name);
		}
		if (g_strcmp0(cat, "X-FlashDrive") == 0) {
			/* TRANSLATORS: flash refers to solid state storage, e.g. UFS or eMMC */
			return g_strdup_printf(_("%s Flash Drive Update"), name);
		}
		if (g_strcmp0(cat, "X-SolidStateDrive") == 0) {
			/* TRANSLATORS: SSD refers to a Solid State Drive, e.g. non-rotating
			 * SATA or NVMe disk */
			return g_strdup_printf(_("%s SSD Update"), name);
		}
		if (g_strcmp0(cat, "X-Gpu") == 0) {
			/* TRANSLATORS: GPU refers to a Graphics Processing Unit, e.g.
			 * the "video card" */
			return g_strdup_printf(_("%s GPU Update"), name);
		}
		if (g_strcmp0(cat, "X-Dock") == 0) {
			/* TRANSLATORS: Dock refers to the port replicator hardware laptops are
			 * cradled in, or lowered onto */
			return g_strdup_printf(_("%s Dock Update"), name);
		}
		if (g_strcmp0(cat, "X-UsbDock") == 0) {
			/* TRANSLATORS: Dock refers to the port replicator device connected
			 * by plugging in a USB cable -- which may or may not also provide power */
			return g_strdup_printf(_("%s USB Dock Update"), name);
		}
		if (g_strcmp0(cat, "X-FingerprintReader") == 0) {
			/* TRANSLATORS: a device that can read your fingerprint pattern */
			return g_strdup_printf(_("%s Fingerprint Reader Update"), name);
		}
		if (g_strcmp0(cat, "X-GraphicsTablet") == 0) {
			/* TRANSLATORS: a large pressure-sensitive drawing area typically used
			 * by artists and digital artists */
			return g_strdup_printf(_("%s Graphics Tablet Update"), name);
		}
		if (g_strcmp0(cat, "X-InputController") == 0) {
			/* TRANSLATORS: an input device used by gamers, e.g. a joystick */
			return g_strdup_printf(_("%s Input Controller Update"), name);
		}
		if (g_strcmp0(cat, "X-Headphones") == 0) {
			/* TRANSLATORS: two miniature speakers attached to your ears */
			return g_strdup_printf(_("%s Headphones Update"), name);
		}
		if (g_strcmp0(cat, "X-Headset") == 0) {
			/* TRANSLATORS: headphones with an integrated microphone */
			return g_strdup_printf(_("%s Headset Update"), name);
		}
		if (g_strcmp0(cat, "X-Touchscreen") == 0) {
			/* TRANSLATORS: a display that can detect touch input from a user */
			return g_strdup_printf(_("%s Touchscreen Update"), name);
		}
	}

	/* TRANSLATORS: this is the fallback where we don't know if the release
	 * is updating the system, the device, or a device class, or something else --
	 * the first %s is the device name, e.g. 'ThinkPad P50` */
	return g_strdup_printf(_("%s Update"), name);
}

static gchar *
fu_cli_license_to_string(const gchar *spdx_license)
{
	g_autofree const gchar **new = NULL;
	g_auto(GStrv) old = NULL;

	/* sanity check */
	if (spdx_license == NULL) {
		/* TRANSLATORS: we don't know the license of the update */
		return g_strdup(_("Unknown"));
	}

	/* replace any LicenseRef-proprietary with it's translated form */
	old = g_strsplit(spdx_license, " AND ", -1);
	new = g_new0(const gchar *, g_strv_length(old) + 1);
	for (guint i = 0; old[i] != NULL; i++) {
		const gchar *license = old[i];
		if (g_strcmp0(license, "LicenseRef-proprietary") == 0 ||
		    g_strcmp0(license, "proprietary") == 0) {
			/* TRANSLATORS: a non-free software license */
			license = _("Proprietary");
		}
		new[i] = license;
	}

	/* this is no longer SPDX */
	return g_strjoinv(", ", (gchar **)new);
}

static const gchar *
fu_cli_release_urgency_to_string(FwupdReleaseUrgency release_urgency)
{
	if (release_urgency == FWUPD_RELEASE_URGENCY_LOW) {
		/* TRANSLATORS: the release urgency */
		return _("Low");
	}
	if (release_urgency == FWUPD_RELEASE_URGENCY_MEDIUM) {
		/* TRANSLATORS: the release urgency */
		return _("Medium");
	}
	if (release_urgency == FWUPD_RELEASE_URGENCY_HIGH) {
		/* TRANSLATORS: the release urgency */
		return _("High");
	}
	if (release_urgency == FWUPD_RELEASE_URGENCY_CRITICAL) {
		/* TRANSLATORS: the release urgency */
		return _("Critical");
	}
	/* TRANSLATORS: unknown release urgency */
	return _("Unknown");
}

static void
fu_cli_report_add_string(FwupdReport *report, guint idt, GString *str)
{
	g_autofree gchar *title = NULL;

	/* TRANSLATORS: the %s is a vendor name, e.g. Lenovo */
	title = g_strdup_printf(_("Tested by %s"), fwupd_report_get_vendor(report));
	fwupd_codec_string_append(str, idt, title, NULL);
	/* TRANSLATORS: when the release was tested */
	fwupd_codec_string_append_time(str, idt + 1, _("Tested"), fwupd_report_get_created(report));
	if (fwupd_report_get_distro_id(report) != NULL) {
		g_autoptr(GString) str2 = g_string_new(fwupd_report_get_distro_id(report));
		if (fwupd_report_get_distro_version(report) != NULL)
			g_string_append_printf(str2,
					       " %s",
					       fwupd_report_get_distro_version(report));
		if (fwupd_report_get_distro_variant(report) != NULL)
			g_string_append_printf(str2,
					       " (%s)",
					       fwupd_report_get_distro_variant(report));
		/* TRANSLATORS: the OS the release was tested on */
		fwupd_codec_string_append(str, idt + 1, _("Distribution"), str2->str);
	}
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: the firmware old version */
				  _("Old version"),
				  fwupd_report_get_version_old(report));
	fwupd_codec_string_append(
	    str,
	    idt + 1,
	    /* TRANSLATORS: the fwupd version the release was tested on */
	    _("Version[fwupd]"),
	    fwupd_report_get_metadata_item(report, "RuntimeVersion(org.freedesktop.fwupd)"));
}

static gchar *
fu_cli_get_release_description_with_fallback(FwupdRelease *rel)
{
	g_autoptr(GString) str = g_string_new(NULL);

	/* add what we've got from the vendor */
	if (fwupd_release_get_description(rel) != NULL)
		g_string_append(str, fwupd_release_get_description(rel));

	/* add this client side to get the translations */
	if (fwupd_release_has_flag(rel, FWUPD_RELEASE_FLAG_IS_COMMUNITY)) {
		g_string_append_printf(
		    str,
		    "<p>%s</p>",
		    /* TRANSLATORS: the vendor did not upload this */
		    _("This firmware is provided by LVFS community members and is not "
		      "provided (or supported) by the original hardware vendor."));
		g_string_append_printf(
		    str,
		    "<p>%s</p>",
		    /* TRANSLATORS: if it breaks, you get to keep both pieces */
		    _("Installing this update may also void any device warranty."));
	}

	/* this can't be from the LVFS, but the user could be installing a local file */
	if (str->len == 0) {
		g_string_append_printf(str,
				       "<p>%s</p>",
				       /* TRANSLATORS: naughty vendor */
				       _("The vendor did not supply any release notes."));
	}

	return g_string_free(g_steal_pointer(&str), FALSE);
}

typedef struct {
	guint cnt;
	GString *str;
} FuCliConvertHelper;

static gboolean
fu_cli_convert_description_head_cb(XbNode *n, gpointer user_data)
{
	FuCliConvertHelper *helper = (FuCliConvertHelper *)user_data;
	helper->cnt++;

	/* start */
	if (g_strcmp0(xb_node_get_element(n), "em") == 0) {
		g_string_append(helper->str, "\033[3m");
	} else if (g_strcmp0(xb_node_get_element(n), "strong") == 0) {
		g_string_append(helper->str, "\033[1m");
	} else if (g_strcmp0(xb_node_get_element(n), "code") == 0) {
		g_string_append(helper->str, "`");
	} else if (g_strcmp0(xb_node_get_element(n), "li") == 0) {
		g_string_append(helper->str, "• ");
	} else if (g_strcmp0(xb_node_get_element(n), "p") == 0 ||
		   g_strcmp0(xb_node_get_element(n), "ul") == 0 ||
		   g_strcmp0(xb_node_get_element(n), "ol") == 0) {
		g_string_append(helper->str, "\n");
	}

	/* text */
	if (xb_node_get_text(n) != NULL)
		g_string_append(helper->str, xb_node_get_text(n));

	return FALSE;
}

static gboolean
fu_cli_convert_description_tail_cb(XbNode *n, gpointer user_data)
{
	FuCliConvertHelper *helper = (FuCliConvertHelper *)user_data;
	helper->cnt++;

	/* end */
	if (g_strcmp0(xb_node_get_element(n), "em") == 0 ||
	    g_strcmp0(xb_node_get_element(n), "strong") == 0) {
		g_string_append(helper->str, "\033[0m");
	} else if (g_strcmp0(xb_node_get_element(n), "code") == 0) {
		g_string_append(helper->str, "`");
	} else if (g_strcmp0(xb_node_get_element(n), "li") == 0) {
		g_string_append(helper->str, "\n");
	} else if (g_strcmp0(xb_node_get_element(n), "p") == 0) {
		g_string_append(helper->str, "\n");
	}

	/* tail */
	if (xb_node_get_tail(n) != NULL)
		g_string_append(helper->str, xb_node_get_tail(n));

	return FALSE;
}

static gchar *
fu_cli_convert_description(const gchar *xml, GError **error)
{
	g_autoptr(GString) str = g_string_new(NULL);
	g_autoptr(XbNode) n = NULL;
	g_autoptr(XbSilo) silo = NULL;
	FuCliConvertHelper helper = {
	    .cnt = 0,
	    .str = str,
	};

	/* parse XML */
	silo = xb_silo_new_from_xml(xml, error);
	if (silo == NULL)
		return NULL;

	/* convert to something we can show on the console */
	n = xb_silo_get_root(silo);
	xb_node_transmogrify(n,
			     fu_cli_convert_description_head_cb,
			     fu_cli_convert_description_tail_cb,
			     &helper);

	/* success */
	return fu_strstrip(str->str);
}

static gchar *
fu_cli_release_to_string(FwupdRelease *rel, guint idt)
{
	const gchar *title;
	const gchar *tmp2;
	GPtrArray *checksums = fwupd_release_get_checksums(rel);
	GPtrArray *issues = fwupd_release_get_issues(rel);
	GPtrArray *tags = fwupd_release_get_tags(rel);
	GPtrArray *reports = fwupd_release_get_reports(rel);
	guint64 flags = fwupd_release_get_flags(rel);
	g_autofree gchar *desc_fb = NULL;
	g_autofree gchar *name = fu_cli_release_get_name(rel);
	g_autoptr(GString) str = g_string_new(NULL);

	g_return_val_if_fail(FWUPD_IS_RELEASE(rel), NULL);

	fwupd_codec_string_append(str, idt, name, "");

	/* TRANSLATORS: version number of new firmware */
	fwupd_codec_string_append(str, idt + 1, _("New version"), fwupd_release_get_version(rel));

	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: the server the file is coming from */
				  _("Remote ID"),
				  fwupd_release_get_remote_id(rel));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: the exact component on the server */
				  _("Release ID"),
				  fwupd_release_get_id(rel));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: the stream of firmware, e.g. nonfree */
				  _("Branch"),
				  fwupd_release_get_branch(rel));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: one line summary of device */
				  _("Summary"),
				  fwupd_release_get_summary(rel));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: one line variant of release (e.g. 'China') */
				  _("Variant"),
				  fwupd_release_get_name_variant_suffix(rel));
	if (fwupd_release_get_license(rel) != NULL) {
		g_autofree gchar *license =
		    fu_cli_license_to_string(fwupd_release_get_license(rel));
		/* TRANSLATORS: e.g. GPLv2+, Proprietary etc */
		fwupd_codec_string_append(str, idt + 1, _("License"), license);
	}
	/* TRANSLATORS: file size of the download */
	fwupd_codec_string_append_size(str, idt + 1, _("Size"), fwupd_release_get_size(rel));
	/* TRANSLATORS: when the update was built */
	fwupd_codec_string_append_time(str, idt + 1, _("Created"), fwupd_release_get_created(rel));
	if (fwupd_release_get_urgency(rel) != FWUPD_RELEASE_URGENCY_UNKNOWN) {
		FwupdReleaseUrgency tmp = fwupd_release_get_urgency(rel);
		fwupd_codec_string_append(str,
					  idt + 1,
					  /* TRANSLATORS: how important the release is */
					  _("Urgency"),
					  fu_cli_release_urgency_to_string(tmp));
	}
	for (guint i = 0; i < reports->len; i++) {
		FwupdReport *report = g_ptr_array_index(reports, i);
		fu_cli_report_add_string(report, idt + 1, str);
	}
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: more details about the update link */
				  _("Details"),
				  fwupd_release_get_details_url(rel));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: source (as in code) link */
				  _("Source"),
				  fwupd_release_get_source_url(rel));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: Software Bill of Materials link */
				  _("SBOM"),
				  fwupd_release_get_sbom_url(rel));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: manufacturer of hardware */
				  _("Vendor"),
				  fwupd_release_get_vendor(rel));
	if (fwupd_release_get_install_duration(rel) != 0) {
		g_autofree gchar *tmp = fu_cli_time_to_str(fwupd_release_get_install_duration(rel));
		/* TRANSLATORS: length of time the update takes to apply */
		fwupd_codec_string_append(str, idt + 1, _("Duration"), tmp);
	}
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: helpful messages for the update */
				  _("Update Message"),
				  fwupd_release_get_update_message(rel));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: helpful image for the update */
				  _("Update Image"),
				  fwupd_release_get_update_image(rel));

	/* TRANSLATORS: release attributes */
	title = _("Release Flags");
	for (guint i = 0; i < 64; i++) {
		g_autofree gchar *bullet = NULL;
		if ((flags & ((guint64)1 << i)) == 0)
			continue;
		tmp2 = fu_cli_release_flag_to_string((guint64)1 << i);
		if (tmp2 == NULL)
			continue;
		bullet = g_strdup_printf("• %s", tmp2);
		fwupd_codec_string_append(str, idt + 1, title, bullet);
		title = "";
	}

	desc_fb = fu_cli_get_release_description_with_fallback(rel);
	if (desc_fb != NULL) {
		g_autofree gchar *desc = NULL;
		desc = fu_cli_convert_description(desc_fb, NULL);
		if (desc == NULL)
			desc = g_strdup(fwupd_release_get_description(rel));
		/* TRANSLATORS: multiline description of device */
		fwupd_codec_string_append(str, idt + 1, _("Description"), desc);
	}
	for (guint i = 0; i < issues->len; i++) {
		const gchar *issue = g_ptr_array_index(issues, i);
		if (i == 0) {
			fwupd_codec_string_append(
			    str,
			    idt + 1,
			    /* TRANSLATORS: issue fixed with the release, e.g. CVE */
			    ngettext("Issue", "Issues", issues->len),
			    issue);
		} else {
			fwupd_codec_string_append(str, idt + 1, "", issue);
		}
	}
	if (tags->len > 0) {
		g_autofree gchar *tag_strs = fu_strjoin(", ", tags);
		fwupd_codec_string_append(
		    str,
		    idt + 1,
		    /* TRANSLATORS: release tag set for release, e.g. lenovo-2021q3 */
		    ngettext("Tag", "Tags", tags->len),
		    tag_strs);
	}
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *checksum = g_ptr_array_index(checksums, i);
		GChecksumType checksum_type = fwupd_checksum_guess_kind(checksum);

		/* avoid showing brokwn checksums */
		if (checksum_type == G_CHECKSUM_SHA1)
			continue;

		/* TRANSLATORS: hash to that exact firmware archive */
		fwupd_codec_string_append(str, idt + 1, _("Checksum"), checksum);
	}

	return g_string_free(g_steal_pointer(&str), FALSE);
}

static const gchar *
fu_cli_update_state_to_string(FwupdUpdateState update_state)
{
	if (update_state == FWUPD_UPDATE_STATE_PENDING) {
		/* TRANSLATORS: the update state of the specific device */
		return _("Pending");
	}
	if (update_state == FWUPD_UPDATE_STATE_SUCCESS) {
		/* TRANSLATORS: the update state of the specific device */
		return _("Success");
	}
	if (update_state == FWUPD_UPDATE_STATE_FAILED) {
		/* TRANSLATORS: the update state of the specific device */
		return _("Failed");
	}
	if (update_state == FWUPD_UPDATE_STATE_FAILED_TRANSIENT) {
		/* TRANSLATORS: the update state of the specific device */
		return _("Transient failure");
	}
	if (update_state == FWUPD_UPDATE_STATE_NEEDS_REBOOT) {
		/* TRANSLATORS: the update state of the specific device */
		return _("Needs reboot");
	}
	return NULL;
}

static gchar *
fu_cli_device_flag_to_string(guint64 device_flag)
{
	if (device_flag == FWUPD_DEVICE_FLAG_NONE)
		return NULL;
	if (device_flag == FWUPD_DEVICE_FLAG_INTERNAL) {
		/* TRANSLATORS: Device cannot be removed easily*/
		return _("Internal device");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_UPDATABLE ||
	    device_flag == FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN) {
		/* TRANSLATORS: Device is updatable in this or any other mode */
		return _("Updatable");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_REQUIRE_AC) {
		/* TRANSLATORS: Must be plugged into an outlet */
		return _("System requires external power source");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_LOCKED) {
		/* TRANSLATORS: Is locked and can be unlocked */
		return _("Device is locked");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_SUPPORTED) {
		/* TRANSLATORS: Is found in current metadata */
		return _("Supported on remote server");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER) {
		/* TRANSLATORS: Requires a bootloader mode to be manually enabled by the user */
		return _("Requires a bootloader");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_REBOOT) {
		/* TRANSLATORS: Requires a reboot to apply firmware or to reload hardware */
		return _("Needs a reboot after installation");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN) {
		/* TRANSLATORS: Requires system shutdown to apply firmware */
		return _("Needs shutdown after installation");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_REPORTED) {
		/* TRANSLATORS: Has been reported to a metadata server */
		return _("Reported to remote server");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NOTIFIED) {
		/* TRANSLATORS: User has been notified */
		return _("User has been notified");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_IS_BOOTLOADER) {
		/* TRANSLATORS: Is currently in bootloader mode */
		return _("Is in bootloader mode");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG) {
		/* TRANSLATORS: the hardware is waiting to be replugged */
		return _("Hardware is waiting to be replugged");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION) {
		/* TRANSLATORS: Device update needs to be separately activated */
		return _("Device update needs activation");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_HISTORICAL) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_WILL_DISAPPEAR) {
		/* TRANSLATORS: Device will not return after update completes */
		return _("Device will not re-appear after update completes");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_CAN_VERIFY) {
		/* TRANSLATORS: Device supports some form of checksum verification */
		return _("Cryptographic hash verification is available");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_DUAL_IMAGE) {
		/* TRANSLATORS: Device supports a safety mechanism for flashing */
		return _("Device stages updates");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_SELF_RECOVERY) {
		/* TRANSLATORS: Device supports a safety mechanism for flashing */
		return _("Device can recover flash failures");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE) {
		/* TRANSLATORS: Device remains usable during update */
		return _("Device is usable for the duration of the update");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED) {
		/* TRANSLATORS: a version check is required for all firmware */
		return _("Device firmware is required to have a version check");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES) {
		/* TRANSLATORS: the device cannot update from A->C and has to go A->B->C */
		return _("Device is required to install all provided releases");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES) {
		/* TRANSLATORS: there is more than one supplier of the firmware */
		return _("Device supports switching to a different branch of firmware");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL) {
		/* TRANSLATORS: save the old firmware to disk before installing the new one */
		return _("Device will backup firmware before installing");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_WILDCARD_INSTALL) {
		/* TRANSLATORS: on some systems certain devices have to have matching versions,
		 * e.g. the EFI driver for a given network card cannot be different */
		return _("All devices of the same type will be updated at the same time");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE) {
		/* TRANSLATORS: some devices can only be updated to a new semver and cannot
		 * be downgraded or reinstalled with the existing version */
		return _("Only version upgrades are allowed");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_UNREACHABLE) {
		/* TRANSLATORS: currently unreachable, perhaps because it is in a lower power state
		 * or is out of wireless range */
		return _("Device is unreachable");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_AFFECTS_FDE) {
		/* TRANSLATORS: we might ask the user the recovery key when next booting Windows */
		return _("Full disk encryption secrets may be invalidated when updating");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_END_OF_LIFE) {
		/* TRANSLATORS: the vendor is no longer supporting the device */
		return _("End of life");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD) {
		/* TRANSLATORS: firmware is verified on-device the payload using strong crypto */
		return _("Signed Payload");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD) {
		/* TRANSLATORS: firmware payload is unsigned and it is possible to modify it */
		return _("Unsigned Payload");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_EMULATED) {
		/* TRANSLATORS: this device is not actually real */
		return _("Emulated");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_EMULATION_TAG) {
		/* TRANSLATORS: we're saving all USB events for emulation */
		return _("Tagged for emulation");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_ONLY_EXPLICIT_UPDATES) {
		/* TRANSLATORS: stay on one firmware version unless the new version is explicitly
		 * specified */
		return _("Installing a specific release is explicitly required");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG) {
		/* TRANSLATORS: we can save all device enumeration events for emulation */
		return _("Can tag for emulation");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_UNKNOWN)
		return NULL;
	return NULL;
}

static gchar *
fu_cli_device_to_string(FwupdClient *client, FwupdDevice *dev, guint idt)
{
	FwupdUpdateState state;
	GPtrArray *guids = fwupd_device_get_guids(dev);
	GPtrArray *issues = fwupd_device_get_issues(dev);
	GPtrArray *vendor_ids = fwupd_device_get_vendor_ids(dev);
	GPtrArray *instance_ids = fwupd_device_get_instance_ids(dev);
	const gchar *tmp;
	const gchar *tmp2;
	guint64 flags = fwupd_device_get_flags(dev);
	guint64 modified = fwupd_device_get_modified(dev);
	guint64 request_flags = fwupd_device_get_request_flags(dev);
	g_autoptr(GHashTable) ids = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* some fields are intentionally not included and are only shown in --verbose */
	if (g_log_get_debug_enabled()) {
		g_autofree gchar *debug_str = fwupd_codec_to_string(FWUPD_CODEC(dev));
		g_info("%s", debug_str);
		return NULL;
	}

	tmp = fwupd_device_get_name(dev);
	if (tmp == NULL) {
		/* TRANSLATORS: Name of hardware */
		tmp = _("Unknown Device");
	}
	fwupd_codec_string_append(str, idt, tmp, "");

	/* TRANSLATORS: ID for hardware, typically a SHA1 sum */
	fwupd_codec_string_append(str, idt + 1, _("Device ID"), fwupd_device_get_id(dev));

	/* TRANSLATORS: one line summary of device */
	fwupd_codec_string_append(str, idt + 1, _("Summary"), fwupd_device_get_summary(dev));

	/* versions */
	tmp = fwupd_device_get_version(dev);
	if (tmp != NULL) {
		g_autoptr(GString) verstr = g_string_new(tmp);
		if (fwupd_device_get_version_build_date(dev) != 0) {
			guint64 value = fwupd_device_get_version_build_date(dev);
			g_autoptr(GDateTime) date = g_date_time_new_from_unix_utc((gint64)value);
			if (date != NULL) {
				g_autofree gchar *datestr = g_date_time_format(date, "%F");
				g_string_append_printf(verstr, " [%s]", datestr);
			}
		}
		if (flags & FWUPD_DEVICE_FLAG_HISTORICAL) {
			fwupd_codec_string_append(
			    str,
			    idt + 1,
			    /* TRANSLATORS: version number of previous firmware */
			    _("Previous version"),
			    verstr->str);
		} else {
			/* TRANSLATORS: version number of current firmware */
			fwupd_codec_string_append(str, idt + 1, _("Current version"), verstr->str);
		}
	}
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: smallest version number installable on device */
				  _("Minimum Version"),
				  fwupd_device_get_version_lowest(dev));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: largest version number installable on device */
				  _("Maximum Version"),
				  fwupd_device_get_version_highest(dev));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: firmware version of bootloader */
				  _("Bootloader Version"),
				  fwupd_device_get_version_bootloader(dev));

	/* vendor */
	tmp = fwupd_device_get_vendor(dev);
	if (tmp != NULL && vendor_ids->len > 0) {
		g_autofree gchar *strv = fu_strjoin(", ", vendor_ids);
		g_autofree gchar *both = g_strdup_printf("%s (%s)", tmp, strv);
		/* TRANSLATORS: manufacturer of hardware */
		fwupd_codec_string_append(str, idt + 1, _("Vendor"), both);
	} else if (tmp != NULL) {
		/* TRANSLATORS: manufacturer of hardware */
		fwupd_codec_string_append(str, idt + 1, _("Vendor"), tmp);
	} else if (vendor_ids->len > 0) {
		g_autofree gchar *strv = fu_strjoin("|", vendor_ids);
		/* TRANSLATORS: manufacturer of hardware */
		fwupd_codec_string_append(str, idt + 1, _("Vendor"), strv);
	}

	/* TRANSLATORS: webpage for the specific device */
	fwupd_codec_string_append(str, idt + 1, _("URL"), fwupd_device_get_details_url(dev));

	/* branch */
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: the stream of firmware, e.g. nonfree */
				  _("Release Branch"),
				  fwupd_device_get_branch(dev));

	/* install duration */
	if (fwupd_device_get_install_duration(dev) > 0) {
		g_autofree gchar *time = fu_cli_time_to_str(fwupd_device_get_install_duration(dev));
		/* TRANSLATORS: length of time the update takes to apply */
		fwupd_codec_string_append(str, idt + 1, _("Install Duration"), time);
	}

	/* TRANSLATORS: serial number of hardware */
	fwupd_codec_string_append(str, idt + 1, _("Serial Number"), fwupd_device_get_serial(dev));

	/* update state */
	state = fwupd_device_get_update_state(dev);
	if (state != FWUPD_UPDATE_STATE_UNKNOWN) {
		fwupd_codec_string_append(str,
					  idt + 1,
					  /* TRANSLATORS: hardware state, e.g. "pending" */
					  _("Update State"),
					  fu_cli_update_state_to_string(state));
	}

	/* battery, but only if we're not about to show the same info as an inhibit */
	if (!fwupd_device_has_problem(dev, FWUPD_DEVICE_PROBLEM_POWER_TOO_LOW)) {
		if (fwupd_device_get_battery_level(dev) != FWUPD_BATTERY_LEVEL_INVALID &&
		    fwupd_device_get_battery_threshold(dev) != FWUPD_BATTERY_LEVEL_INVALID) {
			g_autofree gchar *val = NULL;
			/* TRANSLATORS: first percentage is current value, 2nd percentage is the
			 * lowest limit the firmware update is allowed for the update to happen */
			val = g_strdup_printf(_("%u%% (threshold %u%%)"),
					      fwupd_device_get_battery_level(dev),
					      fwupd_device_get_battery_threshold(dev));
			/* TRANSLATORS: refers to the battery inside the peripheral device */
			fwupd_codec_string_append(str, idt + 1, _("Battery"), val);
		} else if (fwupd_device_get_battery_level(dev) != FWUPD_BATTERY_LEVEL_INVALID) {
			g_autofree gchar *val = NULL;
			val = g_strdup_printf("%u%%", fwupd_device_get_battery_level(dev));
			/* TRANSLATORS: refers to the battery inside the peripheral device */
			fwupd_codec_string_append(str, idt + 1, _("Battery"), val);
		}
	}

	/* either show enumerated [translated] problems or the synthesized update error */
	if (fwupd_device_get_problems(dev) == FWUPD_DEVICE_PROBLEM_NONE) {
		tmp = fwupd_device_get_update_error(dev);
		if (tmp != NULL) {
			g_autofree gchar *color =
			    fu_console_color_format(tmp, FU_CONSOLE_COLOR_RED);
			/* TRANSLATORS: error message from last update attempt */
			fwupd_codec_string_append(str, idt + 1, _("Update Error"), color);
		}
	} else {
		/* TRANSLATORS: reasons the device is not updatable */
		tmp = _("Problems");
		for (guint i = 0; i < 64; i++) {
			FwupdDeviceProblem problem = (guint64)1 << i;
			g_autofree gchar *bullet = NULL;
			g_autofree gchar *desc = NULL;
			g_autofree gchar *color = NULL;

			if (!fwupd_device_has_problem(dev, problem))
				continue;
			desc = fu_cli_device_problem_to_string(client, dev, problem);
			if (desc == NULL)
				continue;
			bullet = g_strdup_printf("• %s", desc);
			color = fu_console_color_format(bullet, FU_CONSOLE_COLOR_RED);
			fwupd_codec_string_append(str, idt + 1, tmp, color);
			tmp = "";
		}
	}

	/* TRANSLATORS: the original time/date the device was modified */
	fwupd_codec_string_append_time(str, idt + 1, _("Last modified"), modified);

	/* all GUIDs for this hardware, with IDs if available */
	ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	for (guint i = 0; i < instance_ids->len; i++) {
		const gchar *instance_id = g_ptr_array_index(instance_ids, i);
		g_hash_table_insert(ids,
				    fwupd_guid_hash_string(instance_id),
				    g_strdup(instance_id));
	}
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index(guids, i);
		const gchar *instance_id = g_hash_table_lookup(ids, guid);
		g_autofree gchar *guid_src = NULL;

		/* instance IDs are only available as root */
		if (instance_id == NULL) {
			guid_src = g_strdup(guid);
		} else {
			guid_src = g_strdup_printf("%s ← %s", guid, instance_id);
		}
		if (i == 0) {
			fwupd_codec_string_append(
			    str,
			    idt + 1,
			    /* TRANSLATORS: global ID common to all similar hardware */
			    ngettext("GUID", "GUIDs", guids->len),
			    guid_src);
		} else {
			fwupd_codec_string_append(str, idt + 1, "", guid_src);
		}
	}

	/* TRANSLATORS: description of device ability */
	tmp = _("Device Flags");
	for (guint i = 0; i < 64; i++) {
		if ((flags & ((guint64)1 << i)) == 0)
			continue;
		tmp2 = fu_cli_device_flag_to_string((guint64)1 << i);
		if (tmp2 == NULL)
			continue;
		/* header */
		if (tmp != NULL) {
			g_autofree gchar *bullet = NULL;
			bullet = g_strdup_printf("• %s", tmp2);
			fwupd_codec_string_append(str, idt + 1, tmp, bullet);
			tmp = NULL;
		} else {
			g_autofree gchar *bullet = NULL;
			bullet = g_strdup_printf("• %s", tmp2);
			fwupd_codec_string_append(str, idt + 1, "", bullet);
		}
	}

	/* TRANSLATORS: description of the device requests */
	tmp = _("Device Requests");
	for (guint i = 0; i < 64; i++) {
		if ((request_flags & ((guint64)1 << i)) == 0)
			continue;
		tmp2 = fu_cli_request_flag_to_string((guint64)1 << i);
		if (tmp2 == NULL)
			continue;
		/* header */
		if (tmp != NULL) {
			g_autofree gchar *bullet = NULL;
			bullet = g_strdup_printf("• %s", tmp2);
			fwupd_codec_string_append(str, idt + 1, tmp, bullet);
			tmp = NULL;
		} else {
			g_autofree gchar *bullet = NULL;
			bullet = g_strdup_printf("• %s", tmp2);
			fwupd_codec_string_append(str, idt + 1, "", bullet);
		}
	}
	for (guint i = 0; i < issues->len; i++) {
		const gchar *issue = g_ptr_array_index(issues, i);
		fwupd_codec_string_append(str,
					  idt + 1,
					  /* TRANSLATORS: issue fixed with the release, e.g. CVE */
					  i == 0 ? ngettext("Issue", "Issues", issues->len) : "",
					  issue);
	}

	return g_string_free(g_steal_pointer(&str), FALSE);
}

static gchar *
fu_cli_remote_to_string(FwupdRemote *remote, guint idt)
{
	FwupdRemoteKind kind = fwupd_remote_get_kind(remote);
	const gchar *tmp;
	gint priority;
	g_autoptr(GString) str = g_string_new(NULL);

	g_return_val_if_fail(FWUPD_IS_REMOTE(remote), NULL);

	fwupd_codec_string_append(str, idt, fwupd_remote_get_title(remote), NULL);

	/* TRANSLATORS: remote identifier, e.g. lvfs-testing */
	fwupd_codec_string_append(str, idt + 1, _("Remote ID"), fwupd_remote_get_id(remote));

	/* TRANSLATORS: remote type, e.g. remote or local */
	fwupd_codec_string_append(str, idt + 1, _("Type"), fwupd_remote_kind_to_string(kind));

	fwupd_codec_string_append(
	    str,
	    idt + 1,
	    /* TRANSLATORS: if the remote is enabled */
	    _("Enabled"),
	    fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED) ? "true" : "false");
	if (fwupd_remote_get_kind(remote) == FWUPD_REMOTE_KIND_DOWNLOAD) {
		fwupd_codec_string_append(
		    str,
		    idt + 1,
		    /* TRANSLATORS: if we can get metadata from peer-to-peer clients */
		    _("P2P Metadata"),
		    fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ALLOW_P2P_METADATA) ? "true"
											: "false");
		fwupd_codec_string_append(
		    str,
		    idt + 1,
		    /* TRANSLATORS: if we can get metadata from peer-to-peer clients */
		    _("P2P Firmware"),
		    fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ALLOW_P2P_FIRMWARE) ? "true"
											: "false");
	}

	/* TRANSLATORS: remote checksum */
	fwupd_codec_string_append(str, idt + 1, _("Checksum"), fwupd_remote_get_checksum(remote));

	/* optional parameters */
	if (kind == FWUPD_REMOTE_KIND_DOWNLOAD && fwupd_remote_get_age(remote) != G_MAXUINT64) {
		g_autofree gchar *age_str = fu_cli_time_to_str(fwupd_remote_get_age(remote));
		/* TRANSLATORS: the age of the metadata */
		fwupd_codec_string_append(str, idt + 1, _("Age"), age_str);
	}
	if (kind == FWUPD_REMOTE_KIND_DOWNLOAD && fwupd_remote_get_refresh_interval(remote) > 0) {
		g_autofree gchar *age_str =
		    fu_cli_time_to_str(fwupd_remote_get_refresh_interval(remote));
		/* TRANSLATORS: how often we should refresh the metadata */
		fwupd_codec_string_append(str, idt + 1, _("Refresh Interval"), age_str);
	}
	priority = fwupd_remote_get_priority(remote);
	if (priority != 0) {
		g_autofree gchar *priority_str = NULL;
		priority_str = g_strdup_printf("%i", priority);
		/* TRANSLATORS: the numeric priority */
		fwupd_codec_string_append(str, idt + 1, _("Priority"), priority_str);
	}
	fwupd_codec_string_append(
	    str,
	    idt + 1,
	    /* TRANSLATORS: a username or password is required */
	    _("Requires Auth"),
	    fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_REQUIRES_AUTH) ? "true" : "false");
	/* TRANSLATORS: remote filename base */
	fwupd_codec_string_append(str, idt + 1, _("Username"), fwupd_remote_get_username(remote));
	tmp = fwupd_remote_get_password(remote);
	if (tmp != NULL) {
		g_autofree gchar *hidden = g_strnfill(fu_strwidth(tmp), '*');
		/* TRANSLATORS: the account token, a bit like a password */
		fwupd_codec_string_append(str, idt + 1, _("Token"), hidden);
	}
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: filename of the local file */
				  _("Filename"),
				  fwupd_remote_get_filename_cache(remote));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: filename of the local file */
				  _("Filename Signature"),
				  fwupd_remote_get_filename_cache_sig(remote));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: full path of the remote.conf file */
				  _("Filename Source"),
				  fwupd_remote_get_filename_source(remote));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: remote URI */
				  _("Metadata URI"),
				  fwupd_remote_get_metadata_uri(remote));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: remote URI */
				  _("Metadata Signature"),
				  fwupd_remote_get_metadata_uri_sig(remote));
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: remote URI */
				  _("Firmware Base URI"),
				  fwupd_remote_get_firmware_base_uri(remote));
	tmp = fwupd_remote_get_report_uri(remote);
	if (tmp != NULL) {
		/* TRANSLATORS: URI to send success/failure reports */
		fwupd_codec_string_append(str, idt + 1, _("Report URI"), tmp);
		fwupd_codec_string_append(
		    str,
		    idt + 1,
		    /* TRANSLATORS: Boolean value to automatically send reports */
		    _("Automatic Reporting"),
		    fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS) ? "true"
										       : "false");
	}

	return g_string_free(g_steal_pointer(&str), FALSE);
}

/* node with refcounted data */
typedef GNode FuCliNode;

static gboolean
fu_cli_free_tree_cb(FuCliNode *n, gpointer data)
{
	if (n->data != NULL)
		g_object_unref(n->data);
	return FALSE;
}

static void
fu_cli_free_node(FuCliNode *n)
{
	g_node_traverse(n, G_POST_ORDER, G_TRAVERSE_ALL, -1, fu_cli_free_tree_cb, NULL);
	g_node_destroy(n);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCliNode, fu_cli_free_node)

static gboolean
fu_cli_traverse_tree(FuCliNode *n, gpointer data)
{
	FuCli *self = FU_CLI(data);
	FuCliPrivate *priv = GET_PRIVATE(self);
	guint idx = g_node_depth(n) - 1;
	g_autofree gchar *tmp = NULL;
	g_auto(GStrv) split = NULL;

	/* get split lines */
	if (FWUPD_IS_DEVICE(n->data)) {
		FwupdDevice *dev = FWUPD_DEVICE(n->data);
		tmp = fu_cli_device_to_string(priv->client, dev, idx);
	} else if (FWUPD_IS_REMOTE(n->data)) {
		FwupdRemote *remote = FWUPD_REMOTE(n->data);
		tmp = fu_cli_remote_to_string(remote, idx);
	} else if (FWUPD_IS_RELEASE(n->data)) {
		FwupdRelease *release = FWUPD_RELEASE(n->data);
		tmp = fu_cli_release_to_string(release, idx);
		g_info("%s", tmp);
	}

	/* root node */
	if (n->parent == NULL && !g_log_get_debug_enabled()) {
		g_autofree gchar *str =
		    g_strdup_printf("%s %s",
				    fwupd_client_get_host_vendor(priv->client),
				    fwupd_client_get_host_product(priv->client));
		fu_console_print_literal(priv->console, str);
		fu_console_print_literal(priv->console, "│");
		return FALSE;
	}

	if (n->parent == NULL)
		return FALSE;

	if (tmp == NULL)
		return FALSE;
	split = g_strsplit(tmp, "\n", -1);
	for (guint i = 0; split[i] != NULL; i++) {
		g_autoptr(GString) str = g_string_new(NULL);

		/* header */
		if (i == 0) {
			if (g_node_next_sibling(n) == NULL)
				g_string_prepend(str, "└─");
			else
				g_string_prepend(str, "├─");

			/* properties */
		} else {
			g_string_prepend(str, n->children == NULL ? "  " : " │");
			g_string_prepend(str, g_node_next_sibling(n) == NULL ? " " : "│");
			g_string_append(str, " ");
		}

		/* ancestors */
		for (GNode *c = n->parent; c != NULL && c->parent != NULL; c = c->parent) {
			if (g_node_next_sibling(c) != NULL || idx == 0) {
				g_string_prepend(str, "│ ");
				continue;
			}
			g_string_prepend(str, "  ");
		}

		/* empty line */
		if (split[i][0] == '\0') {
			fu_console_print_literal(priv->console, str->str);
			continue;
		}

		/* dump to the console */
		g_string_append(str, split[i] + (idx * 2));
		fu_console_print_literal(priv->console, str->str);
	}

	return FALSE;
}

static void
fu_cli_print_node(FuCli *self, FuCliNode *n)
{
	g_node_traverse(n, G_PRE_ORDER, G_TRAVERSE_ALL, -1, fu_cli_traverse_tree, self);
}

static gboolean
fu_cli_check_daemon_version(FuCli *self, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	const gchar *daemon = fwupd_client_get_daemon_version(priv->client);

	if (daemon == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    /* TRANSLATORS: error message */
				    _("Unable to connect to service"));
		return FALSE;
	}

	if (g_strcmp0(daemon, PACKAGE_VERSION) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    /* TRANSLATORS: error message */
			    _("Unsupported daemon version %s, client version is %s"),
			    daemon,
			    PACKAGE_VERSION);
		return FALSE;
	}

	return TRUE;
}

static void
fu_cli_client_notify_cb(GObject *object, GParamSpec *pspec, FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return;
	fu_console_set_progress(priv->console,
				fwupd_client_get_status(priv->client),
				fwupd_client_get_percentage_full(priv->client));
}

static void
fu_cli_update_device_request_cb(FwupdClient *client, FwupdRequest *request, FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);

	/* nothing sensible to show */
	if (fwupd_request_get_message(request) == NULL)
		return;

	/* show this now */
	if (fwupd_request_get_kind(request) == FWUPD_REQUEST_KIND_IMMEDIATE) {
		g_autofree gchar *fmt = NULL;
		g_autofree gchar *tmp = NULL;

		/* TRANSLATORS: the user needs to do something, e.g. remove the device */
		fmt = fu_console_color_format(_("Action Required:"), FU_CONSOLE_COLOR_RED);
		tmp = g_strdup_printf("%s %s", fmt, fwupd_request_get_message(request));
		fu_console_set_progress_title(priv->console, tmp);
		fu_console_beep(priv->console, 5);
	}

	/* save for later */
	if (fwupd_request_get_kind(request) == FWUPD_REQUEST_KIND_POST)
		g_ptr_array_add(priv->post_requests, g_object_ref(request));
}

void
fu_cli_set_current_operation(FuCli *self, FuCliOperation current_operation)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	priv->current_operation = current_operation;
}

static void
fu_cli_update_device_changed_cb(FwupdClient *client, FwupdDevice *device, FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *str = NULL;

	/* action has not been assigned yet */
	if (priv->current_operation == FU_CLI_OPERATION_UNKNOWN)
		return;

	/* allowed to set whenever the device has changed */
	if (fwupd_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN))
		priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN;
	if (fwupd_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT))
		priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_REBOOT;

	/* same as last time, so ignore */
	if (priv->current_device == NULL ||
	    g_strcmp0(fwupd_device_get_composite_id(priv->current_device),
		      fwupd_device_get_composite_id(device)) == 0) {
		g_set_object(&priv->current_device, device);
		return;
	}

	/* ignore indirect devices that might have changed */
	if (fwupd_device_get_status(device) == FWUPD_STATUS_IDLE ||
	    fwupd_device_get_status(device) == FWUPD_STATUS_UNKNOWN) {
		g_debug("ignoring %s with status %s",
			fwupd_device_get_name(device),
			fwupd_status_to_string(fwupd_device_get_status(device)));
		return;
	}

	/* show message in console */
	if (priv->current_operation == FU_CLI_OPERATION_UPDATE) {
		/* TRANSLATORS: %1 is a device name */
		str = g_strdup_printf(_("Updating %s…"), fwupd_device_get_name(device));
		fu_console_set_progress_title(priv->console, str);
	} else if (priv->current_operation == FU_CLI_OPERATION_DOWNGRADE) {
		/* TRANSLATORS: %1 is a device name */
		str = g_strdup_printf(_("Downgrading %s…"), fwupd_device_get_name(device));
		fu_console_set_progress_title(priv->console, str);
	} else if (priv->current_operation == FU_CLI_OPERATION_INSTALL) {
		/* TRANSLATORS: %1 is a device name  */
		str = g_strdup_printf(_("Installing on %s…"), fwupd_device_get_name(device));
		fu_console_set_progress_title(priv->console, str);
	} else {
		g_warning("no FuCliOperation set");
	}
	g_set_object(&priv->current_device, device);
}

static FwupdDevice *
fu_cli_prompt_for_device(FuCli *self, GPtrArray *devices, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FwupdDevice *dev;
	guint idx;
	g_autoptr(GPtrArray) devices_filtered = NULL;

	/* filter results */
	devices_filtered = fu_cli_device_array_filter(self, devices, error);
	if (devices_filtered == NULL)
		return NULL;

	/* exactly one */
	if (devices_filtered->len == 1) {
		dev = g_ptr_array_index(devices_filtered, 0);
		if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
			fu_console_print(
			    priv->console,
			    "%s: %s",
			    /* TRANSLATORS: device has been chosen by the daemon for the user */
			    _("Selected device"),
			    fwupd_device_get_name(dev));
		}
		return g_object_ref(dev);
	}

	/* no questions */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_DEVICE_PROMPT)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "can't prompt for devices");
		return NULL;
	}

	/* TRANSLATORS: this is to abort the interactive prompt */
	fu_console_print(priv->console, "0.\t%s", _("Cancel"));
	for (guint i = 0; i < devices_filtered->len; i++) {
		FwupdDevice *device_tmp = g_ptr_array_index(devices_filtered, i);
		g_autofree gchar *id_display = fwupd_device_get_id_display(device_tmp);
		if (id_display != NULL)
			fu_console_print(priv->console, "%u.\t%s", i + 1, id_display);
	}
	/* TRANSLATORS: get interactive prompt */
	idx = fu_console_input_uint(priv->console, devices_filtered->len, "%s", _("Choose device"));
	if (idx == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return NULL;
	}
	dev = g_ptr_array_index(devices_filtered, idx - 1);
	return g_object_ref(dev);
}

static FwupdRelease *
fu_cli_get_release_with_tag(FuCli *self, FwupdDevice *dev, const gchar *host_bkc, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) rels = NULL;
	g_auto(GStrv) host_bkcs = g_strsplit(host_bkc, ",", -1);

	/* find the newest release that matches */
	rels = fwupd_client_get_releases(priv->client,
					 fwupd_device_get_id(dev),
					 priv->cancellable,
					 error);
	if (rels == NULL)
		return NULL;
	for (guint i = 0; i < rels->len; i++) {
		FwupdRelease *rel = g_ptr_array_index(rels, i);
		if (!fu_cli_release_match_flags(self, rel))
			continue;
		for (guint j = 0; host_bkcs[j] != NULL; j++) {
			if (fwupd_release_has_tag(rel, host_bkcs[j]))
				return g_object_ref(rel);
		}
	}

	/* no match */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no matching releases for device");
	return NULL;
}

static gboolean
fu_cli_prompt_warning_bkc(FuCli *self, FwupdDevice *dev, FwupdRelease *rel, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	const gchar *host_bkc = fwupd_client_get_host_bkc(priv->client);
	g_autofree gchar *cmd = g_strdup_printf("%s sync", g_get_prgname());
	g_autoptr(FwupdRelease) rel_bkc = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* nothing to do */
	if (host_bkc == NULL)
		return TRUE;

	/* get the release that corresponds with the host BKC */
	rel_bkc = fu_cli_get_release_with_tag(self, dev, host_bkc, &error_local);
	if (rel_bkc == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
			g_debug("ignoring %s: %s", fwupd_device_get_id(dev), error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* device is already on a different release */
	if (g_strcmp0(fwupd_device_get_version(dev), fwupd_release_get_version(rel)) != 0)
		return TRUE;

	/* TRANSLATORS: BKC is the industry name for the best known configuration and is a set
	 * of firmware that works together */
	g_string_append_printf(str, _("Your system is set up to the BKC of %s."), host_bkc);
	g_string_append(str, "\n\n");
	g_string_append_printf(
	    str,
	    /* TRANSLATORS: %1 is the current device version number, and %2 is the
	       command name, e.g. `fwupdmgr sync` */
	    _("This device will be reverted back to %s when the %s command is performed."),
	    fwupd_release_get_version(rel),
	    cmd);

	fu_console_box(
	    priv->console,
	    /* TRANSLATORS: the best known configuration is a set of software that we know works
	     * well together. In the OEM and ODM industries it is often called a BKC */
	    _("Deviate from the best known configuration?"),
	    str->str,
	    80);

	/* TRANSLATORS: prompt to apply the update */
	if (!fu_console_input_bool(priv->console, TRUE, "%s", _("Perform operation?"))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cli_prompt_warning(FuCli *self,
		      FwupdDevice *device,
		      FwupdRelease *release,
		      const gchar *machine,
		      GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FwupdDeviceFlags flags;
	gint vercmp;
	g_autofree gchar *desc_fb = NULL;
	g_autoptr(GString) title = g_string_new(NULL);
	g_autoptr(GString) str = g_string_new(NULL);

	/* up, down, or re-install */
	vercmp = fu_version_compare(fwupd_release_get_version(release),
				    fu_device_get_version(device),
				    fwupd_device_get_version_format(device));
	if (vercmp < 0) {
		g_string_append_printf(
		    title,
		    /* TRANSLATORS: message letting the user know an downgrade is available
		     * %1 is the device name and %2 and %3 are version strings */
		    _("Downgrade %s from %s to %s?"),
		    fwupd_device_get_name(device),
		    fwupd_device_get_version(device),
		    fwupd_release_get_version(release));
	} else if (vercmp > 0) {
		g_string_append_printf(
		    title,
		    /* TRANSLATORS: message letting the user know an upgrade is available
		     * %1 is the device name and %2 and %3 are version strings */
		    _("Upgrade %s from %s to %s?"),
		    fwupd_device_get_name(device),
		    fwupd_device_get_version(device),
		    fwupd_release_get_version(release));
	} else {
		g_string_append_printf(
		    title,
		    /* TRANSLATORS: message letting the user know an upgrade is available
		     * %1 is the device name and %2 is a version string */
		    _("Reinstall %s to %s?"),
		    fwupd_device_get_name(device),
		    fwupd_release_get_version(release));
	}

	/* description is optional */
	desc_fb = fu_cli_get_release_description_with_fallback(release);
	if (desc_fb != NULL) {
		g_autofree gchar *desc = fu_cli_convert_description(desc_fb, NULL);
		if (desc != NULL)
			g_string_append_printf(str, "\n%s", desc);
	}

	/* device is not already in bootloader mode so show warning */
	flags = fwupd_device_get_flags(device);
	if ((flags & FWUPD_DEVICE_FLAG_IS_BOOTLOADER) == 0) {
		/* device may reboot */
		if ((flags & FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE) == 0) {
			g_string_append(str, "\n\n");
			g_string_append_printf(
			    str,
			    /* TRANSLATORS: warn the user before updating, %1 is a device name */
			    _("%s and all connected devices may not be usable while updating."),
			    fwupd_device_get_name(device));

			/* device can get bricked */
		} else if ((flags & FWUPD_DEVICE_FLAG_SELF_RECOVERY) == 0) {
			g_string_append(str, "\n\n");
			/* external device */
			if ((flags & FWUPD_DEVICE_FLAG_INTERNAL) == 0) {
				g_string_append_printf(str,
						       /* TRANSLATORS: warn the user before
							* updating, %1 is a device name
							*/
						       _("%s must remain connected for the "
							 "duration of the update to avoid damage."),
						       fwupd_device_get_name(device));
			} else if (flags & FWUPD_DEVICE_FLAG_REQUIRE_AC) {
				g_string_append_printf(
				    str,
				    /* TRANSLATORS: warn the user before updating, %1 is a machine
				     * name
				     */
				    _("%s must remain plugged into a power source for the duration "
				      "of the update to avoid damage."),
				    machine);
			}
		}
	}
	fu_console_box(priv->console, title->str, str->str, 80);

	/* TRANSLATORS: prompt to apply the update */
	if (!fu_console_input_bool(priv->console, TRUE, "%s", _("Perform operation?"))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cli_prompt_warning_fde(FuCli *self, FwupdDevice *dev, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	const gchar *url = "https://github.com/fwupd/fwupd/wiki/Full-Disk-Encryption-Detected";
	g_autoptr(GString) str = g_string_new(NULL);

	if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_AFFECTS_FDE))
		return TRUE;

	g_string_append(
	    str,
	    /* TRANSLATORS: the platform secret is stored in the PCRx registers on the TPM */
	    _("Some of the platform secrets may be invalidated when updating this firmware."));
	g_string_append(str, " ");
	g_string_append(str,
			/* TRANSLATORS: 'recovery key' here refers to a code, rather than a physical
			   metal thing */
			_("Please ensure you have the volume recovery key before continuing."));
	g_string_append(str, "\n\n");
	g_string_append_printf(str,
			       /* TRANSLATORS: the %1 is a URL to a wiki page */
			       _("See %s for more details."),
			       url);
	/* TRANSLATORS: title text, shown as a warning */
	fu_console_box(priv->console, _("Full Disk Encryption Detected"), str->str, 80);

	/* TRANSLATORS: prompt to apply the update */
	if (!fu_console_input_bool(priv->console, TRUE, "%s", _("Perform operation?"))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_cli_prompt_warning_composite(FuCli *self, FwupdDevice *dev, FwupdRelease *rel, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	const gchar *rel_csum;
	g_autoptr(GPtrArray) devices = NULL;

	/* get the default checksum */
	rel_csum = fwupd_checksum_get_best(fwupd_release_get_checksums(rel));
	if (rel_csum == NULL) {
		g_debug("no checksum for release!");
		return TRUE;
	}

	/* find other devices matching the composite ID and the release checksum */
	devices = fwupd_client_get_devices(priv->client, priv->cancellable, error);
	if (devices == NULL)
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev_tmp = g_ptr_array_index(devices, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) rels = NULL;

		/* not the parent device */
		if (g_strcmp0(fwupd_device_get_id(dev), fwupd_device_get_id(dev_tmp)) == 0)
			continue;

		/* not the same composite device */
		if (g_strcmp0(fwupd_device_get_composite_id(dev),
			      fwupd_device_get_composite_id(dev_tmp)) != 0)
			continue;

		/* get releases */
		if (!fwupd_device_has_flag(dev_tmp, FWUPD_DEVICE_FLAG_UPDATABLE))
			continue;
		rels = fwupd_client_get_releases(priv->client,
						 fwupd_device_get_id(dev_tmp),
						 priv->cancellable,
						 &error_local);
		if (rels == NULL) {
			g_debug("ignoring: %s", error_local->message);
			continue;
		}

		/* do any releases match this checksum */
		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel_tmp = g_ptr_array_index(rels, j);
			FwupdInstallFlags install_flags = fu_cli_get_install_flags(self);
			g_autofree gchar *title = NULL;
			gint vercmp;

			if (!fwupd_release_has_checksum(rel_tmp, rel_csum))
				continue;
			vercmp = fu_version_compare(fwupd_release_get_version(rel_tmp),
						    fu_device_get_version(dev_tmp),
						    fwupd_device_get_version_format(dev_tmp));
			if ((install_flags & FWUPD_INSTALL_FLAG_ALLOW_REINSTALL) == 0 &&
			    vercmp == 0)
				continue;
			title = g_strdup_printf("%s %s",
						fwupd_client_get_host_vendor(priv->client),
						fwupd_client_get_host_product(priv->client));
			if (!fu_cli_prompt_warning(self, dev_tmp, rel_tmp, title, error))
				return FALSE;
			break;
		}
	}

	/* success */
	return TRUE;
}

static const gchar *
fu_cli_branch_for_display(const gchar *branch)
{
	if (branch == NULL) {
		/* TRANSLATORS: this is the default branch name when unset */
		return _("default");
	}
	return branch;
}

static gboolean
fu_cli_switch_branch_warning(FuCli *self,
			     FwupdDevice *dev,
			     FwupdRelease *rel,
			     gboolean assume_yes,
			     GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	const gchar *desc_markup = NULL;
	g_autofree gchar *desc_plain = NULL;
	g_autofree gchar *title = NULL;
	g_autoptr(GString) desc_full = g_string_new(NULL);

	/* warn the user if the vendor is different */
	if (g_strcmp0(fwupd_device_get_vendor(dev), fwupd_release_get_vendor(rel)) != 0) {
		g_string_append_printf(
		    desc_full,
		    /* TRANSLATORS: %1 is the firmware vendor, %2 is the device vendor name */
		    _("The firmware from %s is not "
		      "supplied by %s, the hardware vendor."),
		    fwupd_release_get_vendor(rel),
		    fwupd_device_get_vendor(dev));
		g_string_append(desc_full, "\n\n");
		g_string_append_printf(desc_full,
				       /* TRANSLATORS: %1 is the device vendor name */
				       _("Your hardware may be damaged using this firmware, "
					 "and installing this release may void any warranty "
					 "with %s."),
				       fwupd_device_get_vendor(dev));
		g_string_append(desc_full, "\n\n");
	}

	/* from the <description> in the AppStream data */
	desc_markup = fwupd_release_get_description(rel);
	if (desc_markup == NULL)
		return TRUE;
	desc_plain = fu_cli_convert_description(desc_markup, error);
	if (desc_plain == NULL)
		return FALSE;
	g_string_append(desc_full, desc_plain);

	/* TRANSLATORS: show and ask user to confirm --
	 * %1 is the old branch name, %2 is the new branch name */
	title = g_strdup_printf(_("Switch branch from %s to %s?"),
				fu_cli_branch_for_display(fwupd_device_get_branch(dev)),
				fu_cli_branch_for_display(fwupd_release_get_branch(rel)));
	fu_console_box(priv->console, title, desc_full->str, 80);
	if (!assume_yes) {
		if (!fu_console_input_bool(priv->console,
					   FALSE,
					   "%s",
					   /* TRANSLATORS: should the branch be changed */
					   _("Do you understand the consequences "
					     "of changing the firmware branch?"))) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOTHING_TO_DO,
					    "Declined branch switch");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_cli_modify_remote_warning(FuCli *self, FwupdRemote *remote, gboolean assume_yes, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	const gchar *warning_markup = NULL;
	g_autofree gchar *warning_plain = NULL;

	/* get formatted text */
	warning_markup = fwupd_remote_get_agreement(remote);
	if (warning_markup == NULL)
		return TRUE;
	warning_plain = fu_cli_convert_description(warning_markup, error);
	if (warning_plain == NULL)
		return FALSE;

	/* TRANSLATORS: a remote here is like a 'repo' or software source */
	fu_console_box(priv->console, _("Enable new remote?"), warning_plain, 80);
	if (!assume_yes) {
		if (!fu_console_input_bool(priv->console,
					   TRUE,
					   "%s",
					   /* TRANSLATORS: should the remote still be enabled */
					   _("Agree and enable the remote?"))) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOTHING_TO_DO,
					    "Declined agreement");
			return FALSE;
		}
	}
	return TRUE;
}

static void
fu_cli_show_unsupported_warning(FuCli *self)
{
#ifndef SUPPORTED_BUILD
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (g_getenv("FWUPD_SUPPORTED") != NULL)
		return;
	/* TRANSLATORS: this is a prefix on the console */
	fu_console_print_full(priv->console,
			      FU_CONSOLE_PRINT_FLAG_WARNING | FU_CONSOLE_PRINT_FLAG_STDERR,
			      "%s\n",
			      /* TRANSLATORS: unsupported build of the package */
			      _("This package has not been validated, it may not work properly."));
#endif
}

static gboolean
fu_cli_perhaps_show_unreported(FuCli *self, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_failed = g_ptr_array_new();
	g_autoptr(GPtrArray) devices_success = g_ptr_array_new();
	g_autoptr(GPtrArray) remotes = NULL;
	g_autoptr(GHashTable) remote_id_uri_map = NULL;
	gboolean all_automatic = FALSE;

	/* we don't want to ask anything */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_UNREPORTED_CHECK) ||
	    fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_debug("skipping unreported check");
		return TRUE;
	}

	/* get all devices from the history database */
	devices = fwupd_client_get_history(priv->client, priv->cancellable, &error_local);
	if (devices == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO))
			return TRUE;
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* create a map of RemoteID to RemoteURI */
	remotes = fwupd_client_get_remotes(priv->client, priv->cancellable, error);
	if (remotes == NULL)
		return FALSE;
	remote_id_uri_map = g_hash_table_new(g_str_hash, g_str_equal);
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		gboolean remote_automatic;
		if (fwupd_remote_get_id(remote) == NULL)
			continue;
		if (fwupd_remote_get_report_uri(remote) == NULL)
			continue;
		g_debug("adding %s for %s",
			fwupd_remote_get_report_uri(remote),
			fwupd_remote_get_id(remote));
		g_hash_table_insert(remote_id_uri_map,
				    (gpointer)fwupd_remote_get_id(remote),
				    (gpointer)fwupd_remote_get_report_uri(remote));
		remote_automatic =
		    fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS);
		g_debug("%s is %d", fwupd_remote_get_title(remote), remote_automatic);
		if (remote_automatic && !all_automatic)
			all_automatic = TRUE;
		if (!remote_automatic && all_automatic) {
			all_automatic = FALSE;
			break;
		}
	}

	/* check that they can be reported */
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		FwupdRelease *rel = fwupd_device_get_release_default(dev);
		const gchar *remote_id;
		const gchar *remote_uri;

		if (!fu_cli_device_match_flags(self, dev))
			continue;
		if (!fu_cli_device_match_protocol(self, dev))
			continue;
		if (fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_REPORTED))
			continue;
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED))
			continue;

		/* find the RemoteURI to use for the device */
		remote_id = fwupd_release_get_remote_id(rel);
		if (remote_id == NULL) {
			g_debug("%s has no RemoteID", fwupd_device_get_id(dev));
			continue;
		}
		remote_uri = g_hash_table_lookup(remote_id_uri_map, remote_id);
		if (remote_uri == NULL) {
			g_debug("%s has no RemoteURI", remote_id);
			continue;
		}

		/* only send success and failure */
		if (fwupd_device_get_update_state(dev) == FWUPD_UPDATE_STATE_FAILED) {
			g_ptr_array_add(devices_failed, dev);
		} else if (fwupd_device_get_update_state(dev) == FWUPD_UPDATE_STATE_SUCCESS) {
			g_ptr_array_add(devices_success, dev);
		} else {
			g_debug("ignoring %s with UpdateState %s",
				fwupd_device_get_id(dev),
				fwupd_update_state_to_string(fwupd_device_get_update_state(dev)));
		}
	}

	/* nothing to do */
	if (devices_failed->len == 0 && devices_success->len == 0) {
		g_debug("no unreported devices");
		return TRUE;
	}

	g_debug("all automatic: %d", all_automatic);
	/* show the success and failures */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES) && !all_automatic) {
		/* delimit */
		fu_console_line(priv->console, 48);

		/* failures */
		if (devices_failed->len > 0) {
			fu_console_print_literal(priv->console,
						 /* TRANSLATORS: a list of failed updates */
						 _("Devices that were not updated correctly:"));
			for (guint i = 0; i < devices_failed->len; i++) {
				FwupdDevice *dev = g_ptr_array_index(devices_failed, i);
				FwupdRelease *rel = fwupd_device_get_release_default(dev);
				fu_console_print(priv->console,
						 " • %s (%s → %s)",
						 fwupd_device_get_name(dev),
						 fwupd_device_get_version(dev),
						 fwupd_release_get_version(rel));
			}
		}

		/* success */
		if (devices_success->len > 0) {
			fu_console_print_literal(priv->console,
						 /* TRANSLATORS: a list of successful updates */
						 _("Devices that have been updated successfully:"));
			for (guint i = 0; i < devices_success->len; i++) {
				FwupdDevice *dev = g_ptr_array_index(devices_success, i);
				FwupdRelease *rel = fwupd_device_get_release_default(dev);
				fu_console_print(priv->console,
						 " • %s (%s → %s)",
						 fwupd_device_get_name(dev),
						 fwupd_device_get_version(dev),
						 fwupd_release_get_version(rel));
			}
		}

		/* ask for permission */
		fu_console_print_literal(priv->console,
					 /* TRANSLATORS: explain why we want to upload */
					 _("Uploading firmware reports helps hardware vendors "
					   "to quickly identify failing and successful updates "
					   "on real devices."));
		if (!fu_console_input_bool(priv->console,
					   TRUE,
					   "%s (%s)",
					   /* TRANSLATORS: ask the user to upload */
					   _("Review and upload report now?"),
					   /* TRANSLATORS: metadata is downloaded */
					   _("Requires internet connection"))) {
			if (fu_console_input_bool(priv->console,
						  FALSE,
						  "%s",
						  /* TRANSLATORS: offer to disable this nag */
						  _("Do you want to disable this feature "
						    "for future updates?"))) {
				for (guint i = 0; i < remotes->len; i++) {
					FwupdRemote *remote = g_ptr_array_index(remotes, i);
					const gchar *remote_id = fwupd_remote_get_id(remote);
					if (fwupd_remote_get_report_uri(remote) == NULL)
						continue;
					if (!fwupd_client_modify_remote(priv->client,
									remote_id,
									"ReportURI",
									"",
									priv->cancellable,
									error))
						return FALSE;
				}
			}
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOTHING_TO_DO,
					    "Declined upload");
			return FALSE;
		}
	}

	/* upload */
	if (!fu_cli_report_history(self, NULL, error))
		return FALSE;

	/* offer to make automatic */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES) && !all_automatic) {
		if (fu_console_input_bool(priv->console,
					  FALSE,
					  "%s",
					  /* TRANSLATORS: offer to stop asking the question */
					  _("Do you want to upload reports automatically for "
					    "future updates?"))) {
			for (guint i = 0; i < remotes->len; i++) {
				FwupdRemote *remote = g_ptr_array_index(remotes, i);
				const gchar *remote_id = fwupd_remote_get_id(remote);
				if (fwupd_remote_get_report_uri(remote) == NULL)
					continue;
				if (fwupd_remote_has_flag(remote,
							  FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS))
					continue;
				if (!fwupd_client_modify_remote(priv->client,
								remote_id,
								"AutomaticReports",
								"true",
								priv->cancellable,
								error))
					return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

static void
fu_cli_build_device_tree(FuCli *self, FuCliNode *root, GPtrArray *devs, FwupdDevice *dev)
{
	for (guint i = 0; i < devs->len; i++) {
		FwupdDevice *dev_tmp = g_ptr_array_index(devs, i);
		if (!fu_cli_device_match_flags(self, dev_tmp))
			continue;
		if (!fu_cli_device_match_protocol(self, dev_tmp))
			continue;
		if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_SHOW_ALL) &&
		    !fu_cli_is_interesting_device(devs, dev_tmp))
			continue;
		if (fwupd_device_get_parent(dev_tmp) == dev) {
			FuCliNode *child = g_node_append_data(root, g_object_ref(dev_tmp));
			fu_cli_build_device_tree(self, child, devs, dev_tmp);
		}
	}
}

static void
fu_cli_get_releases_as_json(FuCli *self, GPtrArray *rels)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	for (guint i = 0; i < rels->len; i++) {
		FwupdRelease *rel = g_ptr_array_index(rels, i);
		g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();
		if (!fu_cli_release_match_flags(self, rel))
			continue;
		fwupd_codec_to_json(FWUPD_CODEC(rel), json_obj_tmp, FWUPD_CODEC_FLAG_NONE);
		fwupd_json_array_add_object(json_arr, json_obj_tmp);
	}
	fwupd_json_object_add_array(json_obj, "Releases", json_arr);
	fu_cli_print_json_object(priv->console, json_obj);
}

static void
fu_cli_get_devices_as_json(FuCli *self, GPtrArray *devs)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	for (guint i = 0; i < devs->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devs, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();

		/* filter */
		if (!fu_cli_device_match_flags(self, dev))
			continue;
		if (!fu_cli_device_match_protocol(self, dev))
			continue;

		/* add all releases that could be applied */
		rels = fwupd_client_get_releases(priv->client,
						 fwupd_device_get_id(dev),
						 priv->cancellable,
						 &error_local);
		if (rels == NULL) {
			g_debug("not adding releases to device: %s", error_local->message);
		} else {
			for (guint j = 0; j < rels->len; j++) {
				FwupdRelease *rel = g_ptr_array_index(rels, j);
				if (!fu_cli_release_match_flags(self, rel))
					continue;
				fwupd_device_add_release(dev, rel);
			}
		}

		/* add to builder */
		fwupd_codec_to_json(FWUPD_CODEC(dev), json_obj_tmp, FWUPD_CODEC_FLAG_TRUSTED);
		fwupd_json_array_add_object(json_arr, json_obj_tmp);
	}
	fwupd_json_object_add_array(json_obj, "Devices", json_arr);
	fu_cli_print_json_object(priv->console, json_obj);
}

static gboolean
fu_cli_update_shutdown(GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;

	connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;

#ifdef HAVE_LOGIND
	/* shutdown using logind */
	val = g_dbus_connection_call_sync(connection,
					  "org.freedesktop.login1",
					  "/org/freedesktop/login1",
					  "org.freedesktop.login1.Manager",
					  "PowerOff",
					  g_variant_new("(b)", TRUE),
					  NULL,
					  G_DBUS_CALL_FLAGS_NONE,
					  -1,
					  NULL,
					  error);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "No way to perform the operation");
#endif
	return val != NULL;
}

static gboolean
fu_cli_update_reboot(GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;

	connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;

#ifdef HAVE_LOGIND
	/* reboot using logind */
	val = g_dbus_connection_call_sync(connection,
					  "org.freedesktop.login1",
					  "/org/freedesktop/login1",
					  "org.freedesktop.login1.Manager",
					  "Reboot",
					  g_variant_new("(b)", TRUE),
					  NULL,
					  G_DBUS_CALL_FLAGS_NONE,
					  -1,
					  NULL,
					  error);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "No way to perform the operation");
#endif
	return val != NULL;
}

gboolean
fu_cli_prompt_complete(FuCli *self, gboolean prompt, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (priv->completion_flags & FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN) {
		if (prompt) {
			if (!fu_console_input_bool(priv->console,
						   FALSE,
						   "%s %s",
						   /* TRANSLATORS: explain why */
						   _("An update requires the system to shutdown "
						     "to complete."),
						   /* TRANSLATORS: shutdown to apply the update */
						   _("Shutdown now?")))
				return TRUE;
		}
		return fu_cli_update_shutdown(error);
	}
	if (priv->completion_flags & FWUPD_DEVICE_FLAG_NEEDS_REBOOT) {
		if (prompt) {
			if (!fu_console_input_bool(priv->console,
						   FALSE,
						   "%s %s",
						   /* TRANSLATORS: explain why we want to reboot */
						   _("An update requires a reboot to complete."),
						   /* TRANSLATORS: reboot to apply the update */
						   _("Restart now?")))
				return TRUE;
		}
		return fu_cli_update_reboot(error);
	}

	return TRUE;
}

static gboolean
fu_cli_check_reboot_needed(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);

	/* handle both forms */
	if (g_strv_length(values) == 0) {
		g_autoptr(GPtrArray) devices =
		    fwupd_client_get_devices(priv->client, priv->cancellable, error);
		if (devices == NULL)
			return FALSE;
		for (guint i = 0; i < devices->len; i++) {
			FwupdDevice *device = g_ptr_array_index(devices, i);

			if (fwupd_device_get_update_state(device) ==
			    FWUPD_UPDATE_STATE_NEEDS_REBOOT)
				priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_REBOOT;
		}
	} else {
		for (guint i = 0; values[i] != NULL; i++) {
			g_autoptr(FwupdDevice) device = NULL;
			device = fu_cli_get_device_by_id(self, values[i], error);
			if (device == NULL)
				return FALSE;
			if (fwupd_device_get_update_state(device) ==
			    FWUPD_UPDATE_STATE_NEEDS_REBOOT)
				priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_REBOOT;
		}
	}

	if (!(priv->completion_flags & FWUPD_DEVICE_FLAG_NEEDS_REBOOT)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: no rebooting needed */
				    _("No reboot is necessary"));
		return FALSE;
	}

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	return fu_cli_prompt_complete(self, TRUE, error);
}

static gboolean
fu_cli_get_devices(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuCliNode) root = g_node_new(NULL);
	g_autoptr(GPtrArray) devs = NULL;
	g_autoptr(GPtrArray) devices_filtered = NULL;

	/* get results from daemon */
	if (g_strv_length(values) > 0) {
		devs = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		for (guint i = 0; values[i] != NULL; i++) {
			FwupdDevice *device = fu_cli_get_device_by_id(self, values[i], error);
			if (device == NULL)
				return FALSE;
			g_ptr_array_add(devs, device);
		}
	} else {
		devs = fwupd_client_get_devices(priv->client, priv->cancellable, error);
		if (devs == NULL)
			return FALSE;
	}

	/* filter results */
	devices_filtered = fu_cli_device_array_filter(self, devs, error);
	if (devices_filtered == NULL)
		return FALSE;

	/* not for human consumption */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_cli_get_devices_as_json(self, devices_filtered);
		return TRUE;
	}

	/* print */
	if (devices_filtered->len > 0)
		fu_cli_build_device_tree(self, root, devices_filtered, NULL);
	if (g_node_n_children(root) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: nothing attached that can be upgraded */
				    _("No hardware detected with firmware update capability"));
		return FALSE;
	}
	fu_cli_print_node(self, root);

	/* nag? */
	if (!fu_cli_perhaps_show_unreported(self, error))
		return FALSE;

	return TRUE;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(CURLU, curl_url_cleanup)

static gboolean
fu_cli_is_url(const gchar *perhaps_url)
{
	g_autoptr(CURLU) h = curl_url();
	return curl_url_set(h, CURLUPART_URL, perhaps_url, 0) == CURLUE_OK;
}

static gchar *
fu_cli_download_if_required(FuCli *self, const gchar *perhapsfn, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *filename = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* a local file */
	if (g_file_test(perhapsfn, G_FILE_TEST_EXISTS))
		return g_strdup(perhapsfn);
	if (!fu_cli_is_url(perhapsfn))
		return g_strdup(perhapsfn);

	/* download the firmware to a cachedir */
	filename = fu_cli_get_user_cache_path(perhapsfn);
	if (g_file_test(filename, G_FILE_TEST_EXISTS))
		return g_steal_pointer(&filename);
	if (!fu_path_mkdir_parent(filename, error))
		return NULL;
	blob = fwupd_client_download_bytes(priv->client,
					   perhapsfn,
					   priv->download_flags,
					   priv->cancellable,
					   error);
	if (blob == NULL)
		return NULL;

	/* save file to cache */
	if (!fu_bytes_set_contents(filename, blob, error))
		return NULL;
	return g_steal_pointer(&filename);
}

static const gchar *
fu_cli_request_get_message(FwupdRequest *req)
{
	if (fwupd_request_has_flag(req, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE)) {
		if (g_strcmp0(fwupd_request_get_id(req), FWUPD_REQUEST_ID_REMOVE_REPLUG) == 0) {
			/* TRANSLATORS: warning message shown after update has been scheduled */
			return _("The update will continue when the device USB cable has been "
				 "unplugged and then re-inserted.");
		}
		if (g_strcmp0(fwupd_request_get_id(req), FWUPD_REQUEST_ID_REMOVE_USB_CABLE) == 0) {
			/* TRANSLATORS: warning message shown after update has been scheduled */
			return _("The update will continue when the device USB cable has been "
				 "unplugged.");
		}
		if (g_strcmp0(fwupd_request_get_id(req), FWUPD_REQUEST_ID_INSERT_USB_CABLE) == 0) {
			/* TRANSLATORS: warning message shown after update has been scheduled */
			return _("The update will continue when the device USB cable has been "
				 "re-inserted.");
		}
		if (g_strcmp0(fwupd_request_get_id(req), FWUPD_REQUEST_ID_PRESS_UNLOCK) == 0) {
			/* TRANSLATORS: warning message */
			return _("Press unlock on the device to continue the update process.");
		}
		if (g_strcmp0(fwupd_request_get_id(req), FWUPD_REQUEST_ID_DO_NOT_POWER_OFF) == 0) {
			/* TRANSLATORS: warning message shown after update has been scheduled */
			return _("Do not turn off your computer or remove the AC adaptor "
				 "while the update is in progress.");
		}
		if (g_strcmp0(fwupd_request_get_id(req), FWUPD_REQUEST_ID_REPLUG_INSTALL) == 0) {
			/* TRANSLATORS: message shown after device has been marked for emulation */
			return _("Unplug and replug the device to continue the update process.");
		}
		if (g_strcmp0(fwupd_request_get_id(req), FWUPD_REQUEST_ID_REPLUG_POWER) == 0) {
			/* TRANSLATORS: warning message */
			return _("The update will continue when the device power cable has been "
				 "removed and re-inserted.");
		}
	}
	return fwupd_request_get_message(req);
}

void
fu_cli_display_current_message(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return;

	/* TRANSLATORS: success message */
	fu_console_print_literal(priv->console, _("Successfully installed firmware"));

	/* print all POST requests */
	for (guint i = 0; i < priv->post_requests->len; i++) {
		FwupdRequest *request = g_ptr_array_index(priv->post_requests, i);
		fu_console_print_literal(priv->console, fu_cli_request_get_message(request));
	}
}

typedef struct {
	guint nr_success;
	guint nr_missing;
	guint nr_skipped;
	const gchar *name;
	gboolean use_emulation;
	GHashTable *report_metadata;
} FuCliDeviceTestHelper;

static void
fu_cli_device_test_helper_free(FuCliDeviceTestHelper *helper)
{
	if (helper->report_metadata != NULL)
		g_hash_table_unref(helper->report_metadata);
	g_free(helper);
}

static FuCliDeviceTestHelper *
fu_cli_device_test_helper_new(void)
{
	FuCliDeviceTestHelper *helper = g_new0(FuCliDeviceTestHelper, 1);
	return helper;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCliDeviceTestHelper, fu_cli_device_test_helper_free)

static GPtrArray *
fu_cli_filter_devices(FuCli *self, GPtrArray *devices, GError **error)
{
	g_autoptr(GPtrArray) devices_filtered =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		if (!fu_cli_device_match_flags(self, dev))
			continue;
		if (!fu_cli_device_match_protocol(self, dev))
			continue;
		g_ptr_array_add(devices_filtered, g_object_ref(dev));
	}
	if (devices_filtered->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "failed to find any devices");
		return NULL;
	}
	return g_steal_pointer(&devices_filtered);
}

static gboolean
fu_cli_device_test_component(FuCli *self,
			     FuCliDeviceTestHelper *helper,
			     FwupdJsonObject *json_obj,
			     FwupdJsonObject *json_object_result,
			     GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	const gchar *name;
	const gchar *protocol;
	const gchar *version;
	g_autoptr(FwupdDevice) device = NULL;
	g_autoptr(FwupdJsonArray) json_arr = NULL;

	/* some elements are optional */
	name = fwupd_json_object_get_string_with_default(json_obj, "name", "component", error);
	if (name == NULL)
		return FALSE;
	if (g_strcmp0(name, "component") != 0)
		fwupd_json_object_add_string(json_object_result, "name", name);
	protocol = fwupd_json_object_get_string(json_obj, "protocol", NULL);
	if (protocol != NULL)
		fwupd_json_object_add_string(json_object_result, "protocol", protocol);

	/* find the device with any of the matching GUIDs */
	json_arr = fwupd_json_object_get_array(json_obj, "guids", error);
	if (json_arr == NULL)
		return FALSE;
	fwupd_json_object_add_array(json_object_result, "guids", json_arr);
	for (guint i = 0; i < fwupd_json_array_get_size(json_arr); i++) {
		FwupdDevice *device_tmp;
		const gchar *guid;
		g_autoptr(GPtrArray) devices = NULL;
		g_autoptr(GPtrArray) devices_filtered = NULL;

		guid = fwupd_json_array_get_string(json_arr, i, error);
		if (guid == NULL)
			return FALSE;

		g_debug("looking for guid %s", guid);
		devices =
		    fwupd_client_get_devices_by_guid(priv->client, guid, priv->cancellable, NULL);
		if (devices == NULL)
			continue;
		devices_filtered = fu_cli_filter_devices(self, devices, NULL);
		if (devices_filtered == NULL)
			continue;
		if (devices_filtered->len > 1) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "multiple devices with GUID %s",
				    guid);
			return FALSE;
		}
		device_tmp = g_ptr_array_index(devices_filtered, 0);
		if (protocol != NULL && !fwupd_device_has_protocol(device_tmp, protocol))
			continue;
		device = g_object_ref(device_tmp);
		break;
	}
	if (device == NULL) {
		if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
			g_autofree gchar *msg = NULL;
			msg = fu_console_color_format(
			    /* TRANSLATORS: this is for the device tests */
			    _("Did not find any devices with matching GUIDs"),
			    FU_CONSOLE_COLOR_RED);
			fu_console_print(priv->console, "%s: %s", name, msg);
		}
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no devices found");
		return FALSE;
	}

	/* verify the version matches what we expected */
	version = fwupd_json_object_get_string(json_obj, "version", NULL);
	if (version != NULL) {
		if (g_strcmp0(version, fwupd_device_get_version(device)) != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "version did not match: got %s, expected %s",
				    fwupd_device_get_version(device),
				    version);
			return FALSE;
		}
	}

	/* verify the bootloader version matches what we expected */
	version = fwupd_json_object_get_string(json_obj, "version-bootloader", NULL);
	if (version != NULL) {
		if (g_strcmp0(version, fwupd_device_get_version_bootloader(device)) != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "bootloader version did not match: got %s, expected %s",
				    fwupd_device_get_version_bootloader(device),
				    version);
			return FALSE;
		}
	}

	/* verify the branch matches what we expected */
	version = fwupd_json_object_get_string(json_obj, "branch", NULL);
	if (version != NULL) {
		if (g_strcmp0(version, fwupd_device_get_branch(device)) != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "branch did not match: got %s, expected %s",
				    fwupd_device_get_branch(device),
				    version);
			return FALSE;
		}
	}

	/* success */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_autofree gchar *msg = NULL;
		/* TRANSLATORS: this is for the device tests */
		msg = fu_console_color_format(_("OK!"), FU_CONSOLE_COLOR_GREEN);
		if (g_strcmp0(name, "component") != 0) {
			fu_console_print(priv->console, "%s [%s]: %s", helper->name, name, msg);
		} else {
			fu_console_print(priv->console, "%s: %s", helper->name, msg);
		}
	}
	helper->nr_success++;
	return TRUE;
}

static gboolean
fu_cli_device_test_remove_emulated_devices(FuCli *self, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) devices = NULL;

	devices = fwupd_client_get_devices(priv->client, priv->cancellable, error);
	if (devices == NULL)
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *device = g_ptr_array_index(devices, i);
		g_autoptr(GError) error_local = NULL;
		if (!fwupd_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
			continue;
		if (!fwupd_client_modify_device(priv->client,
						fwupd_device_get_id(device),
						"Flags",
						"~emulated",
						priv->cancellable,
						&error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				g_debug("ignoring: %s", error_local->message);
				continue;
			}
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to modify device: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gchar *
fu_cli_maybe_expand_basename(FuCli *self, const gchar *maybe_basename, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdRemote) remote = NULL;

	if (g_str_has_prefix(maybe_basename, "https://"))
		return g_strdup(maybe_basename);
	if (g_str_has_prefix(maybe_basename, "/"))
		return g_strdup(maybe_basename);

	/* find LVFS remote */
	remote = fwupd_client_get_remote_by_id(priv->client, "lvfs", priv->cancellable, error);
	if (remote == NULL)
		return NULL;
	if (fwupd_remote_get_firmware_base_uri(remote) == NULL) {
		g_debug("no FirmwareBaseURI set in lvfs.conf, using default");
		return g_strdup_printf("https://fwupd.org/downloads/%s", maybe_basename);
	}
	return g_strdup_printf("%s/%s", fwupd_remote_get_firmware_base_uri(remote), maybe_basename);
}

static gboolean
fu_cli_device_test_step(FuCli *self,
			FuCliDeviceTestHelper *helper,
			FwupdJsonObject *json_obj,
			FwupdJsonObject *json_object_result,
			GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	const gchar *url_tmp;
	g_autoptr(FwupdJsonArray) json_arr = NULL;

	/* send this data to the daemon */
	if (helper->use_emulation) {
		g_autofree gchar *emulation_filename = NULL;
		g_autofree gchar *emulation_url = NULL;
		g_autoptr(GError) error_local = NULL;

		/* ignore anything without emulation data */
		url_tmp = fwupd_json_object_get_string(json_obj, "emulation-url", NULL);
		if (url_tmp != NULL) {
			emulation_url = fu_cli_maybe_expand_basename(self, url_tmp, error);
			if (emulation_url == NULL)
				return FALSE;
			emulation_filename =
			    fu_cli_download_if_required(self, emulation_url, error);
			if (emulation_filename == NULL) {
				g_prefix_error(error, "failed to download %s: ", emulation_url);
				return FALSE;
			}
		} else {
			emulation_filename = g_strdup(
			    fwupd_json_object_get_string(json_obj, "emulation-file", NULL));
			if (emulation_filename == NULL)
				return TRUE;
		}

		/* log */
		if (emulation_url != NULL) {
			fwupd_json_object_add_string(json_object_result,
						     "emulation-url",
						     emulation_url);
		}
		fwupd_json_object_add_string(json_object_result,
					     "emulation-file",
					     emulation_filename);
		if (!fwupd_client_emulation_load(priv->client,
						 emulation_filename,
						 priv->cancellable,
						 &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
				g_debug("ignoring: %s", error_local->message);
				helper->nr_skipped++;
				return TRUE;
			}
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to load %s: ",
						   emulation_filename);
			return FALSE;
		}
	}

	/* download file if required */
	url_tmp = fwupd_json_object_get_string(json_obj, "url", NULL);
	if (url_tmp != NULL) {
		FwupdInstallFlags install_flags = fu_cli_get_install_flags(self);
		g_autofree gchar *filename = NULL;
		g_autofree gchar *url = NULL;
		g_autoptr(GError) error_local = NULL;

		url = fu_cli_maybe_expand_basename(self, url_tmp, error);
		if (url == NULL)
			return FALSE;
		filename = fu_cli_download_if_required(self, url, error);
		if (filename == NULL) {
			g_prefix_error(error, "failed to download %s: ", url);
			return FALSE;
		}

		/* log */
		fwupd_json_object_add_string(json_object_result, "url", url);

		/* install file */
		install_flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
		install_flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
		if (!fwupd_client_install(priv->client,
					  FWUPD_DEVICE_ID_ANY,
					  filename,
					  install_flags,
					  priv->cancellable,
					  &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
					fwupd_json_object_add_string(json_object_result,
								     "info",
								     error_local->message);
				} else {
					g_autofree gchar *msg = NULL;
					msg = fu_console_color_format(error_local->message,
								      FU_CONSOLE_COLOR_YELLOW);
					fu_console_print(priv->console,
							 "%s: %s",
							 helper->name,
							 msg);
				}
				helper->nr_missing++;
				return TRUE;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	/* process each step */
	json_arr = fwupd_json_object_get_array(json_obj, "components", error);
	if (json_arr == NULL)
		return FALSE;
	for (guint i = 0; i < fwupd_json_array_get_size(json_arr); i++) {
		g_autoptr(FwupdJsonObject) json_obj_tmp = NULL;
		json_obj_tmp = fwupd_json_array_get_object(json_arr, i, error);
		if (json_obj_tmp == NULL)
			return FALSE;
		if (!fu_cli_device_test_component(self,
						  helper,
						  json_obj_tmp,
						  json_object_result,
						  error))
			return FALSE;
	}

	/* remove emulated devices */
	if (helper->use_emulation) {
		if (!fu_cli_device_test_remove_emulated_devices(self, error)) {
			g_prefix_error_literal(error, "failed to remove emulated devices: ");
			return FALSE;
		}
	}

	/* success */
	fwupd_json_object_add_boolean(json_object_result, "success", TRUE);
	return TRUE;
}

static gboolean
fu_cli_device_test_filename(FuCli *self,
			    FuCliDeviceTestHelper *helper,
			    const gchar *filename,
			    FwupdJsonObject *json_object_result,
			    GError **error)
{
	gint64 repeat = 1;
	gboolean interactive = FALSE;
	g_autoptr(FwupdJsonArray) json_archs_cpu = NULL;
	g_autoptr(FwupdJsonArray) json_archs_plat = NULL;
	g_autoptr(FwupdJsonArray) json_steps = NULL;
	g_autoptr(FwupdJsonArray) json_steps_result = fwupd_json_array_new();
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	g_autoptr(GBytes) blob = NULL;

	/* set appropriate limits */
	fwupd_json_parser_set_max_depth(json_parser, 10);
	fwupd_json_parser_set_max_items(json_parser, 100);
	fwupd_json_parser_set_max_quoted(json_parser, 10000);

	/* log */
	fwupd_json_object_add_string(json_object_result, "filename", filename);

	/* parse JSON */
	blob = fu_bytes_get_contents(filename, error);
	if (blob == NULL)
		return FALSE;
	json_node =
	    fwupd_json_parser_load_from_bytes(json_parser, blob, FWUPD_JSON_LOAD_FLAG_NONE, error);
	if (json_node == NULL) {
		g_prefix_error_literal(error, "test not in JSON format: ");
		return FALSE;
	}
	json_obj = fwupd_json_node_get_object(json_node, error);
	if (json_obj == NULL)
		return FALSE;

	/* some elements are optional */
	helper->name = fwupd_json_object_get_string(json_obj, "name", NULL);
	if (helper->name != NULL)
		fwupd_json_object_add_string(json_object_result, "name", helper->name);
	if (!fwupd_json_object_get_boolean_with_default(json_obj,
							"interactive",
							&interactive,
							FALSE,
							error))
		return FALSE;
	fwupd_json_object_add_boolean(json_object_result, "interactive", interactive);

	json_archs_cpu = fwupd_json_object_get_array(json_obj, "cpu-architectures", NULL);
	if (json_archs_cpu != NULL) {
		gboolean matched = FALSE;
		const gchar *arch = g_hash_table_lookup(helper->report_metadata, "CpuArchitecture");
		for (guint i = 0; i < fwupd_json_array_get_size(json_archs_cpu); i++) {
			const gchar *arch_tmp;
			arch_tmp = fwupd_json_array_get_string(json_archs_cpu, i, error);
			if (arch_tmp == NULL)
				return FALSE;
			if (g_strcmp0(arch, arch_tmp) == 0) {
				matched = TRUE;
				break;
			}
		}
		if (!matched) {
			helper->nr_skipped++;
			return TRUE;
		}
	}

	json_archs_plat = fwupd_json_object_get_array(json_obj, "platform-architectures", NULL);
	if (json_archs_plat != NULL) {
		gboolean matched = FALSE;
		const gchar *arch =
		    g_hash_table_lookup(helper->report_metadata, "PlatformArchitecture");
		for (guint i = 0; i < fwupd_json_array_get_size(json_archs_plat); i++) {
			const gchar *arch_tmp;
			arch_tmp = fwupd_json_array_get_string(json_archs_plat, i, error);
			if (arch_tmp == NULL)
				return FALSE;
			if (g_strcmp0(arch, arch_tmp) == 0) {
				matched = TRUE;
				break;
			}
		}
		if (!matched) {
			helper->nr_skipped++;
			return TRUE;
		}
	}

	/* process each step */
	if (!fwupd_json_object_get_integer_with_default(json_obj, "repeat", &repeat, 1, error))
		return FALSE;

	json_steps = fwupd_json_object_get_array(json_obj, "steps", error);
	if (json_steps == NULL)
		return FALSE;
	fwupd_json_object_add_array(json_object_result, "steps", json_steps_result);
	for (guint j = 0; j < repeat; j++) {
		for (guint i = 0; i < fwupd_json_array_get_size(json_steps); i++) {
			g_autoptr(FwupdJsonObject) json_step = NULL;
			g_autoptr(FwupdJsonObject) json_step_result = fwupd_json_object_new();

			json_step = fwupd_json_array_get_object(json_steps, i, error);
			if (json_step == NULL)
				return FALSE;
			fwupd_json_array_add_object(json_steps_result, json_step_result);
			if (!fu_cli_device_test_step(self,
						     helper,
						     json_step,
						     json_step_result,
						     error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

typedef struct {
	FuCli *self;
	gchar *inhibit_id;
} FuCliInhibitHelper;

static void
fu_cli_inhibit_helper_free(FuCliInhibitHelper *helper)
{
	g_free(helper->inhibit_id);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCliInhibitHelper, fu_cli_inhibit_helper_free)

static gboolean
fu_cli_inhibit_timeout_cb(FuCliInhibitHelper *helper)
{
	FuCli *self = helper->self;
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GError) error_local = NULL;

	if (!fwupd_client_uninhibit(priv->client,
				    helper->inhibit_id,
				    priv->cancellable,
				    &error_local)) {
		g_warning("failed to auto-uninhibit: %s", error_local->message);
	}
	g_main_loop_quit(priv->loop);
	return G_SOURCE_REMOVE;
}

static gboolean
fu_cli_inhibit(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	const gchar *reason = "not set";
	guint64 timeout_ms = 0;
	g_autoptr(FuCliInhibitHelper) helper = g_new0(FuCliInhibitHelper, 1);
	g_autoptr(GString) str = g_string_new(NULL);

	if (g_strv_length(values) > 0)
		reason = values[0];
	if (g_strv_length(values) > 1) {
		if (!fu_strtoull(values[1],
				 &timeout_ms,
				 0,
				 G_MAXUINT32,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
	}

	/* inhibit then wait */
	helper->self = self;
	helper->inhibit_id = fwupd_client_inhibit(priv->client, reason, priv->cancellable, error);
	if (helper->inhibit_id == NULL)
		return FALSE;
	if (timeout_ms > 0) {
		g_autoptr(GSource) source = g_timeout_source_new(timeout_ms);
		g_source_set_callback(source, (GSourceFunc)fu_cli_inhibit_timeout_cb, helper, NULL);
		g_source_attach(source, priv->main_ctx);
	}

	/* TRANSLATORS: the inhibit ID is a short string like dbus-123456 */
	g_string_append_printf(str, _("Inhibit ID is %s."), helper->inhibit_id);
	g_string_append(str, "\n");
	if (timeout_ms > 0) {
		g_string_append_printf(str,
				       /* TRANSLATORS: we can auto-uninhibit after a timeout */
				       _("Automatically uninhibiting in %ums…"),
				       (guint)timeout_ms);
		g_string_append(str, "\n");
	}
	/* TRANSLATORS: CTRL^C [holding control, and then pressing C] will exit the program */
	g_string_append(str, _("Use CTRL^C to cancel."));
	/* TRANSLATORS: this CLI tool is now preventing system updates */
	fu_console_box(priv->console, _("System Update Inhibited"), str->str, 80);
	fu_cli_loop_run(self);
	return TRUE;
}

static gboolean
fu_cli_uninhibit(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);

	/* one argument required */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments, expected INHIBIT-ID");
		return FALSE;
	}

	/* just uninhibit with the token */
	return fwupd_client_uninhibit(priv->client, values[0], priv->cancellable, error);
}

typedef struct {
	FuCli *self;
	const gchar *value;
	FwupdDevice *device; /* no-ref */
} FuCliWaitHelper;

static void
fu_cli_device_wait_added_cb(FwupdClient *client, FwupdDevice *device, FuCliWaitHelper *helper)
{
	FuCli *self = helper->self;
	FuCliPrivate *priv = GET_PRIVATE(self);

	if (g_strcmp0(fwupd_device_get_id(device), helper->value) == 0 ||
	    fwupd_device_has_guid(device, helper->value)) {
		helper->device = device;
		g_main_loop_quit(priv->loop);
		return;
	}
}

static gboolean
fu_cli_device_wait_timeout_cb(gpointer user_data)
{
	FuCli *self = FU_CLI(user_data);
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_main_loop_quit(priv->loop);
	return G_SOURCE_REMOVE;
}

static gboolean
fu_cli_device_wait(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdDevice) device = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GSource) source = g_timeout_source_new_seconds(30);
	g_autoptr(GTimer) timer = g_timer_new();
	FuCliWaitHelper helper = {.self = self, .value = values[0]};

	/* one argument required */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments, expected GUID|DEVICE-ID");
		return FALSE;
	}

	/* check if the device already exists */
	device = fwupd_client_get_device_by_id(priv->client, helper.value, NULL, NULL);
	if (device != NULL) {
		/* TRANSLATORS: the device is already connected */
		fu_console_print_literal(priv->console, _("Device already exists"));
		return TRUE;
	}
	devices = fwupd_client_get_devices_by_guid(priv->client, helper.value, NULL, NULL);
	if (devices != NULL) {
		/* TRANSLATORS: the device is already connected */
		fu_console_print_literal(priv->console, _("Device already exists"));
		return TRUE;
	}

	/* wait for device to show up */
	fu_console_set_progress(priv->console, FWUPD_STATUS_IDLE, 0);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "device-added",
			 G_CALLBACK(fu_cli_device_wait_added_cb),
			 &helper);
	g_source_set_callback(source, fu_cli_device_wait_timeout_cb, self, NULL);
	g_source_attach(source, priv->main_ctx);
	fu_cli_loop_run(self);

	/* timed out */
	if (helper.device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    "Stopped waiting for %s after %.0fms",
			    helper.value,
			    g_timer_elapsed(timer, NULL) * 1000.f);
		return FALSE;
	}

	/* success */
	fu_console_print(priv->console,
			 /* TRANSLATORS: the device showed up in time */
			 _("Successfully waited %.0fms for device"),
			 g_timer_elapsed(timer, NULL) * 1000.f);
	return TRUE;
}

static gboolean
fu_cli_quit(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return fwupd_client_quit(priv->client, priv->cancellable, error);
}

static gboolean
fu_cli_device_test_full(FuCli *self, gchar **values, FuCliDeviceTestHelper *helper, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdJsonArray) json_array_results = fwupd_json_array_new();

	/* required for interactive devices */
	fu_cli_set_current_operation(self, FU_CLI_OPERATION_UPDATE);

	/* at least one argument required */
	if (g_strv_length(values) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* get the report metadata */
	helper->report_metadata =
	    fwupd_client_get_report_metadata(priv->client, priv->cancellable, error);
	if (helper->report_metadata == NULL)
		return FALSE;

	/* process all the files */
	for (guint i = 0; values[i] != NULL; i++) {
		g_autoptr(FwupdJsonObject) json_object_result = fwupd_json_object_new();
		fwupd_json_array_add_object(json_array_results, json_object_result);
		if (!fu_cli_device_test_filename(self,
						 helper,
						 values[i],
						 json_object_result,
						 error)) {
			g_prefix_error(error, "%s failed: ", values[i]);
			return FALSE;
		}
	}

	/* dump to screen as JSON format */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_autoptr(FwupdJsonObject) json_object_results = fwupd_json_object_new();
		fwupd_json_object_add_array(json_object_results, "results", json_array_results);
		fu_cli_print_json_object(priv->console, json_object_results);
		return TRUE;
	}

	/* just warning */
	if (helper->nr_skipped > 0) {
		g_autoptr(GString) str = g_string_new(NULL);
		g_string_append_printf(
		    str,
		    /* TRANSLATORS: device tests can be specific to a CPU type */
		    ngettext("%u test was skipped", "%u tests were skipped", helper->nr_skipped),
		    helper->nr_skipped);
		fu_console_print_full(priv->console,
				      FU_CONSOLE_PRINT_FLAG_WARNING,
				      "%s\n",
				      str->str);
	}

	/* we need all to pass for a zero return code */
	if (helper->nr_missing > 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%u devices required for %u tests were not found",
			    helper->nr_missing,
			    g_strv_length(values));
		return FALSE;
	}
	if (helper->nr_success == 0 && helper->nr_skipped == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "None of the tests were successful");
		return FALSE;
	}

	/* nag? */
	if (!fu_cli_perhaps_show_unreported(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_cli_device_emulate(FuCli *self, gchar **values, GError **error)
{
	g_autoptr(FuCliDeviceTestHelper) helper = fu_cli_device_test_helper_new();
	helper->use_emulation = TRUE;
	fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_ONLY_EMULATED);
	fu_cli_add_filter_device_include(self, FWUPD_DEVICE_FLAG_EMULATED);
	return fu_cli_device_test_full(self, values, helper, error);
}

static gboolean
fu_cli_device_test(FuCli *self, gchar **values, GError **error)
{
	g_autoptr(FuCliDeviceTestHelper) helper = fu_cli_device_test_helper_new();
	fu_cli_add_filter_device_exclude(self, FWUPD_DEVICE_FLAG_EMULATED);
	return fu_cli_device_test_full(self, values, helper, error);
}

static gboolean
fu_cli_download(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *basename = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* one argument required */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* file already exists */
	basename = g_path_get_basename(values[0]);
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_FORCE) &&
	    g_file_test(basename, G_FILE_TEST_EXISTS)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    "%s already exists",
			    basename);
		return FALSE;
	}
	blob = fwupd_client_download_bytes(priv->client,
					   values[0],
					   priv->download_flags,
					   priv->cancellable,
					   error);
	if (blob == NULL)
		return FALSE;
	return g_file_set_contents(basename,
				   g_bytes_get_data(blob, NULL),
				   g_bytes_get_size(blob),
				   error);
}

static gboolean
fu_cli_local_install(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FwupdInstallFlags install_flags = fu_cli_get_install_flags(self);
	const gchar *id;
	g_autofree gchar *filename = NULL;
	g_autoptr(FwupdDevice) dev = NULL;

	/* handle both forms */
	if (g_strv_length(values) == 1) {
		id = FWUPD_DEVICE_ID_ANY;
	} else if (g_strv_length(values) == 2) {
		dev = fu_cli_get_device_by_id(self, values[1], error);
		if (dev == NULL)
			return FALSE;
		id = fwupd_device_get_id(dev);
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	fu_cli_set_current_operation(self, FU_CLI_OPERATION_INSTALL);

	/* install with flags chosen by the user */
	filename = fu_cli_download_if_required(self, values[0], error);
	if (filename == NULL)
		return FALSE;

	/* detect bitlocker */
	if (dev != NULL && !fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_SAFETY_CHECK) &&
	    !fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES)) {
		if (!fu_cli_prompt_warning_fde(self, dev, error))
			return FALSE;
	}

	if (!fwupd_client_install(priv->client,
				  id,
				  filename,
				  install_flags,
				  priv->cancellable,
				  error))
		return FALSE;

	fu_cli_display_current_message(self);

	/* we don't want to ask anything */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_REBOOT_CHECK)) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	/* show reboot if needed */
	return fu_cli_prompt_complete(self, TRUE, error);
}

static gboolean
fu_cli_get_details(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(FuCliNode) root = g_node_new(NULL);

	/* check args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* implied, important for get-details on a device not in your system */
	fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_SHOW_ALL);

	array = fwupd_client_get_details(priv->client, values[0], priv->cancellable, error);
	if (array == NULL)
		return FALSE;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
		fwupd_codec_array_to_json(array, "Devices", json_obj, FWUPD_CODEC_FLAG_TRUSTED);
		fu_cli_print_json_object(priv->console, json_obj);
		return TRUE;
	}

	fu_cli_build_device_tree(self, root, array, NULL);
	fu_cli_print_node(self, root);

	return TRUE;
}

static gboolean
fu_cli_report_history_for_remote(FuCli *self,
				 GPtrArray *devices,
				 FwupdRemote *remote_filter,
				 FwupdRemote *remote_upload,
				 GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *data = NULL;
	g_autofree gchar *report_uri = NULL;
	g_autofree gchar *sig = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(GHashTable) metadata = NULL;

	/* convert to JSON */
	metadata = fwupd_client_get_report_metadata(priv->client, priv->cancellable, error);
	if (metadata == NULL)
		return FALSE;
	data = fwupd_client_build_report_history(priv->client,
						 devices,
						 remote_filter,
						 metadata,
						 error);
	if (data == NULL)
		return FALSE;

	/* self sign data */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_SIGN)) {
		sig = fwupd_client_self_sign(priv->client,
					     data,
					     FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP,
					     priv->cancellable,
					     error);
		if (sig == NULL)
			return FALSE;
	}

	/* ask for permission */
	report_uri = fwupd_remote_build_report_uri(remote_upload, error);
	if (report_uri == NULL)
		return FALSE;
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES) &&
	    !fwupd_remote_has_flag(remote_upload, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS)) {
		fu_console_print_kv(priv->console, _("Target"), report_uri);
		fu_console_print_kv(priv->console, _("Payload"), data);
		if (sig != NULL)
			fu_console_print_kv(priv->console, _("Signature"), sig);
		if (!fu_console_input_bool(priv->console, TRUE, "%s", _("Proceed with upload?"))) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_PERMISSION_DENIED,
					    "User declined action");
			return FALSE;
		}
	}

	/* POST request and parse reply */
	uri = fwupd_client_upload_report(priv->client,
					 report_uri,
					 data,
					 sig,
					 FWUPD_CLIENT_UPLOAD_FLAG_NONE,
					 priv->cancellable,
					 error);
	if (uri == NULL)
		return FALSE;

	/* server wanted us to see a message */
	if (g_strcmp0(uri, "") != 0) {
		fu_console_print(
		    priv->console,
		    "%s %s",
		    /* TRANSLATORS: the server sent the user a small message */
		    _("Update failure is a known issue, visit this URL for more information:"),
		    uri);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cli_report_history_force(FuCli *self, GPtrArray *devices, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdRemote) remote_upload = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* just assume every report goes to this remote */
	remote_upload =
	    fwupd_client_get_remote_by_id(priv->client, "lvfs", priv->cancellable, error);
	if (remote_upload == NULL)
		return FALSE;
	if (!fu_cli_report_history_for_remote(self,
					      devices,
					      NULL, /* no filter */
					      remote_upload,
					      error))
		return FALSE;

	/* mark each device as reported */
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *device = g_ptr_array_index(devices, i);
		g_debug("setting flag on %s", fwupd_device_get_id(device));
		if (!fwupd_client_modify_device(priv->client,
						fwupd_device_get_id(device),
						"Flags",
						"reported",
						priv->cancellable,
						error))
			return FALSE;
	}

	/* success */
	g_string_append_printf(str,
			       /* TRANSLATORS: success message -- where the user has uploaded
				* success and/or failure reports to the remote server */
			       ngettext("Successfully uploaded %u report",
					"Successfully uploaded %u reports",
					devices->len),
			       devices->len);
	fu_console_print_literal(priv->console, str->str);
	return TRUE;
}

static gboolean
fu_cli_report_export(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GHashTable) metadata = NULL;
	g_autoptr(GPtrArray) devices_filtered =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(GPtrArray) devices = NULL;

	/* get all devices from the history database, then filter them and export to JSON */
	devices = fwupd_client_get_history(priv->client, priv->cancellable, error);
	if (devices == NULL)
		return FALSE;
	g_debug("%u devices with history", devices->len);

	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		gboolean dev_skip_byid = TRUE;

		/* only process particular DEVICE-ID or GUID if specified */
		for (guint idx = 0; idx < g_strv_length(values); idx++) {
			const gchar *tmpid = values[idx];
			const gchar *device_id = fwupd_device_get_id(dev);
			if (fwupd_device_has_guid(dev, tmpid) || g_strcmp0(device_id, tmpid) == 0) {
				dev_skip_byid = FALSE;
				break;
			}
		}
		if (g_strv_length(values) > 0 && dev_skip_byid)
			continue;

		/* filter, if not forcing */
		if (!fu_cli_device_match_flags(self, dev))
			continue;
		if (!fu_cli_device_match_protocol(self, dev))
			continue;
		if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_FORCE)) {
			if (fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_REPORTED)) {
				g_debug("%s has already been reported", fwupd_device_get_id(dev));
				continue;
			}
		}

		/* only send success and failure */
		if (fwupd_device_get_update_state(dev) != FWUPD_UPDATE_STATE_FAILED &&
		    fwupd_device_get_update_state(dev) != FWUPD_UPDATE_STATE_SUCCESS) {
			g_debug("ignoring %s with UpdateState %s",
				fwupd_device_get_id(dev),
				fwupd_update_state_to_string(fwupd_device_get_update_state(dev)));
			continue;
		}
		g_ptr_array_add(devices_filtered, g_object_ref(dev));
	}

	/* nothing to report, but try harder with --force */
	if (devices_filtered->len == 0 && !fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_FORCE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "No reports require exporting");
		return FALSE;
	}

	/* get metadata */
	metadata = fwupd_client_get_report_metadata(priv->client, priv->cancellable, error);
	if (metadata == NULL)
		return FALSE;

	/* write each device report as a new file */
	for (guint i = 0; i < devices_filtered->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices_filtered, i);
		g_autofree gchar *data = NULL;
		g_autofree gchar *filename = NULL;
		g_autoptr(FuFirmware) archive = fu_zip_firmware_new();
		g_autoptr(FuFirmware) payload_img = fu_zip_file_new();
		g_autoptr(GBytes) payload_blob = NULL;
		g_autoptr(GFile) file = NULL;
		g_autoptr(GPtrArray) devices_tmp = g_ptr_array_new();

		/* convert single device to JSON */
		g_ptr_array_add(devices_tmp, dev);
		data = fwupd_client_build_report_history(priv->client,
							 devices_tmp,
							 NULL, /* remote */
							 metadata,
							 error);
		if (data == NULL)
			return FALSE;
		payload_blob = g_bytes_new(data, strlen(data));
		fu_firmware_set_id(payload_img, "report.json");
		fu_firmware_set_bytes(payload_img, payload_blob);
		fu_zip_file_set_compression(FU_ZIP_FILE(payload_img), FU_ZIP_COMPRESSION_DEFLATE);
		if (!fu_firmware_add_image(archive, payload_img, error))
			return FALSE;

		/* self sign data */
		if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_SIGN)) {
			g_autofree gchar *sig = NULL;
			g_autoptr(FuFirmware) sig_img = fu_zip_file_new();
			g_autoptr(GBytes) sig_blob = NULL;

			sig = fwupd_client_self_sign(priv->client,
						     data,
						     FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP,
						     priv->cancellable,
						     error);
			if (sig == NULL)
				return FALSE;
			sig_blob = g_bytes_new(sig, strlen(sig));
			fu_firmware_set_id(sig_img, "report.json.p7c");
			fu_firmware_set_bytes(sig_img, sig_blob);
			fu_zip_file_set_compression(FU_ZIP_FILE(sig_img),
						    FU_ZIP_COMPRESSION_DEFLATE);
			if (!fu_firmware_add_image(archive, sig_img, error))
				return FALSE;
		}

		/* save to local file */
		filename = g_strdup_printf("%s.fwupdreport", fwupd_device_get_id(dev));
		file = g_file_new_for_path(filename);
		if (!fu_firmware_write_file(archive, file, error))
			return FALSE;

		/* TRANSLATORS: key for a offline report filename */
		fu_console_print_kv(priv->console, _("Saved report"), filename);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cli_report_history_full(FuCli *self, gboolean only_automatic_reports, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	guint cnt = 0;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_filtered = NULL;
	g_autoptr(GPtrArray) remotes = NULL;

	/* get all devices from the history database, then filter them,
	 * adding to a hash map of report-ids */
	devices = fwupd_client_get_history(priv->client, priv->cancellable, error);
	if (devices == NULL)
		return FALSE;
	g_debug("%u devices with history", devices->len);

	/* filter results */
	devices_filtered = fu_cli_device_array_filter(self, devices, error);
	if (devices_filtered == NULL)
		return FALSE;

	/* ignore the previous reported flag */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_FORCE)) {
		for (guint i = 0; i < devices_filtered->len; i++) {
			FwupdDevice *dev = g_ptr_array_index(devices_filtered, i);
			fwupd_device_remove_flag(dev, FWUPD_DEVICE_FLAG_REPORTED);
		}
	}

	/* needs an extra action, show something to the user */
	for (guint i = 0; i < devices_filtered->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices_filtered, i);
		if (fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
			g_autofree gchar *cmd = g_strdup_printf("%s activate", g_get_prgname());
			fu_console_print(
			    priv->console,
			    /* TRANSLATORS: %1 is a device name, e.g. "ThinkPad Universal
			     * ThunderBolt 4 Dock" and %2 is "fwupdmgr activate" */
			    _("%s is pending activation; use %s to complete the update."),
			    fwupd_device_get_name(dev),
			    cmd);
		}
	}

	/* get all remotes */
	remotes = fwupd_client_get_remotes(priv->client, priv->cancellable, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		g_autoptr(GError) error_local = NULL;

		/* filter this so we can use it from fwupd-refresh */
		if (only_automatic_reports &&
		    !fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS)) {
			g_debug("%s has no AutomaticReports set", fwupd_remote_get_id(remote));
			continue;
		}

		/* try to upload */
		if (!fu_cli_report_history_for_remote(self,
						      devices,
						      remote, /* filter */
						      remote, /* upload */
						      &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO))
				continue;
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}

		/* keep track to make sure *something* worked */
		cnt += 1;
	}

	/* nothing to report, but try harder with --force */
	if (cnt == 0) {
		if (!only_automatic_reports && fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_FORCE))
			return fu_cli_report_history_force(self, devices, error);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "No reports require uploading");
		return FALSE;
	}

	/* mark each device as reported */
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		g_debug("setting flag on %s", fwupd_device_get_id(dev));
		if (!fwupd_client_modify_device(priv->client,
						fwupd_device_get_id(dev),
						"Flags",
						"reported",
						priv->cancellable,
						error))
			return FALSE;
	}

	/* TRANSLATORS: where the user has uploaded success and/or failure report to the server */
	fu_console_print_literal(priv->console, "Successfully uploaded report");
	return TRUE;
}

static gboolean
fu_cli_report_history(FuCli *self, gchar **values, GError **error)
{
	if (values != NULL && g_strv_length(values) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}
	return fu_cli_report_history_full(self, FALSE, error);
}

static gboolean
fu_cli_get_history(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_filtered = NULL;
	g_autoptr(FuCliNode) root = g_node_new(NULL);

	/* get all devices from the history database */
	devices = fwupd_client_get_history(priv->client, priv->cancellable, error);
	if (devices == NULL)
		return FALSE;
	devices_filtered = fu_cli_device_array_filter(self, devices, error);
	if (devices_filtered == NULL)
		return FALSE;

	/* not for human consumption */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
		fwupd_codec_array_to_json(devices_filtered,
					  "Devices",
					  json_obj,
					  FWUPD_CODEC_FLAG_TRUSTED);
		fu_cli_print_json_object(priv->console, json_obj);
		return TRUE;
	}

	/* show each device */
	for (guint i = 0; i < devices_filtered->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices_filtered, i);
		FwupdRelease *rel;
		FuCliNode *child;

		if (!fu_cli_device_match_flags(self, dev))
			continue;
		if (!fu_cli_device_match_protocol(self, dev))
			continue;
		child = g_node_append_data(root, g_object_ref(dev));

		rel = fwupd_device_get_release_default(dev);
		if (rel == NULL)
			continue;
		g_node_append_data(child, g_object_ref(rel));
	}

	fu_cli_print_node(self, root);

	return TRUE;
}

static FwupdDevice *
fu_cli_get_device_by_id(FuCli *self, const gchar *id, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (fwupd_guid_is_valid(id)) {
		g_autoptr(GPtrArray) devices = NULL;
		devices =
		    fwupd_client_get_devices_by_guid(priv->client, id, priv->cancellable, error);
		if (devices == NULL)
			return NULL;
		return fu_cli_prompt_for_device(self, devices, error);
	}
	/* did this look like a GUID? */
	for (guint i = 0; id[i] != '\0'; i++) {
		if (id[i] == '-') {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_ARGS,
					    "Invalid arguments");
			return NULL;
		}
	}
	return fwupd_client_get_device_by_id(priv->client, id, priv->cancellable, error);
}

static FwupdDevice *
fu_cli_get_device_or_prompt(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) devices = NULL;

	/* get device to use */
	if (g_strv_length(values) >= 1) {
		if (g_strv_length(values) > 1) {
			for (guint i = 1; i < g_strv_length(values); i++)
				g_debug("ignoring extra input %s", values[i]);
		}
		return fu_cli_get_device_by_id(self, values[0], error);
	}

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "device ID required");
		return NULL;
	}

	/* get all devices from daemon */
	devices = fwupd_client_get_devices(priv->client, priv->cancellable, error);
	if (devices == NULL)
		return NULL;
	return fu_cli_prompt_for_device(self, devices, error);
}

static FwupdRelease *
fu_cli_get_release_for_device_version(FuCli *self,
				      FwupdDevice *device,
				      const gchar *version,
				      GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) releases = NULL;

	/* get all releases */
	releases = fwupd_client_get_releases(priv->client,
					     fwupd_device_get_id(device),
					     priv->cancellable,
					     error);
	if (releases == NULL)
		return NULL;

	/* find using vercmp */
	for (guint j = 0; j < releases->len; j++) {
		FwupdRelease *release = g_ptr_array_index(releases, j);
		if (fu_version_compare(fwupd_release_get_version(release),
				       version,
				       fwupd_device_get_version_format(device)) == 0) {
			return g_object_ref(release);
		}
	}

	/* did not find */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "Unable to locate release %s for %s",
		    version,
		    fwupd_device_get_name(device));
	return NULL;
}

static gboolean
fu_cli_clear_results(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdDevice) dev = NULL;

	dev = fu_cli_get_device_or_prompt(self, values, error);
	if (dev == NULL)
		return FALSE;

	return fwupd_client_clear_results(priv->client,
					  fwupd_device_get_id(dev),
					  priv->cancellable,
					  error);
}

static gboolean
fu_cli_verify_update(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdDevice) dev = NULL;

	fu_cli_add_filter_device_include(self, FWUPD_DEVICE_FLAG_CAN_VERIFY);
	dev = fu_cli_get_device_or_prompt(self, values, error);
	if (dev == NULL)
		return FALSE;

	if (!fwupd_client_verify_update(priv->client,
					fwupd_device_get_id(dev),
					priv->cancellable,
					error)) {
		g_prefix_error(error, "failed to verify update %s: ", fwupd_device_get_name(dev));
		return FALSE;
	}

	/* TRANSLATORS: success message when user refreshes device checksums */
	fu_console_print_literal(priv->console, _("Successfully updated device checksums"));

	return TRUE;
}

static gboolean
fu_cli_download_metadata_enable_lvfs(FuCli *self, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdRemote) remote = NULL;

	/* is the LVFS available but disabled? */
	remote = fwupd_client_get_remote_by_id(priv->client, "lvfs", priv->cancellable, error);
	if (remote == NULL)
		return TRUE;
	fu_console_print_literal(
	    priv->console,
	    /* TRANSLATORS: explain why no metadata available */
	    _("No remotes are currently enabled so no metadata is available."));
	fu_console_print_literal(
	    priv->console,
	    /* TRANSLATORS: explain why no metadata available */
	    _("Metadata can be obtained from the Linux Vendor Firmware Service."));

	/* TRANSLATORS: Turn on the remote */
	if (!fu_console_input_bool(priv->console, TRUE, "%s", _("Enable this remote?")))
		return TRUE;
	if (!fwupd_client_modify_remote(priv->client,
					fwupd_remote_get_id(remote),
					"Enabled",
					"true",
					priv->cancellable,
					error))
		return FALSE;
	if (!fu_cli_modify_remote_warning(self,
					  remote,
					  fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES),
					  error))
		return FALSE;

	/* refresh the newly-enabled remote */
	return fwupd_client_refresh_remote(priv->client,
					   remote,
					   priv->download_flags,
					   priv->cancellable,
					   error);
}

static gboolean
fu_cli_check_oldest_remote(FuCli *self, guint64 *age_oldest, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) remotes = NULL;
	gboolean checked = FALSE;

	/* get the age of the oldest enabled remotes */
	remotes = fwupd_client_get_remotes(priv->client, priv->cancellable, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		if (!fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED))
			continue;
		if (fwupd_remote_get_kind(remote) != FWUPD_REMOTE_KIND_DOWNLOAD)
			continue;
		checked = TRUE;
		if (!fwupd_remote_needs_refresh(remote))
			continue;
		g_debug("%s is age %u",
			fwupd_remote_get_id(remote),
			(guint)fwupd_remote_get_age(remote));
		if (fwupd_remote_get_age(remote) > *age_oldest)
			*age_oldest = fwupd_remote_get_age(remote);
	}
	if (!checked) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: error message for a user who ran fwupdmgr
				       refresh recently but no remotes */
				    "No remotes enabled.");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_cli_download_metadata(FuCli *self, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	gboolean download_remote_enabled = FALSE;
	guint devices_supported_cnt = 0;
	guint devices_updatable_cnt = 0;
	guint refresh_cnt = 0;
	g_autoptr(GPtrArray) devs = NULL;
	g_autoptr(GPtrArray) remotes = NULL;
	g_autoptr(GError) error_local = NULL;

	remotes = fwupd_client_get_remotes(priv->client, priv->cancellable, error);
	if (remotes == NULL)
		return FALSE;
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		if (!fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED))
			continue;
		if (fwupd_remote_get_kind(remote) != FWUPD_REMOTE_KIND_DOWNLOAD)
			continue;
		download_remote_enabled = TRUE;
		if (fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_REQUIRES_AUTH) &&
		    fwupd_remote_get_username(remote) == NULL &&
		    fwupd_remote_get_password(remote) == NULL) {
			g_debug("skipping remote %s as auth required but not supplied",
				fwupd_remote_get_id(remote));
			continue;
		}
		if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_FORCE) &&
		    !fwupd_remote_needs_refresh(remote)) {
			g_debug("skipping as remote %s age is %us",
				fwupd_remote_get_id(remote),
				(guint)fwupd_remote_get_age(remote));
			continue;
		}
		if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
			fu_console_print(priv->console,
					 "%s %s",
					 _("Updating"),
					 fwupd_remote_get_id(remote));
		if (!fwupd_client_refresh_remote(priv->client,
						 remote,
						 priv->download_flags,
						 priv->cancellable,
						 error))
			return FALSE;
		refresh_cnt++;
	}

	/* no web remote is declared; try to enable LVFS */
	if (!download_remote_enabled) {
		/* we don't want to ask anything */
		if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_REMOTE_CHECK)) {
			g_debug("skipping remote check");
			return TRUE;
		}

		if (!fu_cli_download_metadata_enable_lvfs(self, error))
			return FALSE;
	}

	/* metadata refreshed recently */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_FORCE) && refresh_cnt == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    /* TRANSLATORS: error message for a user who ran fwupdmgr
			     * refresh recently -- %1 is '--force' */
			    _("Metadata is up to date; use %s to refresh again."),
			    "--force");
		return FALSE;
	}

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	/* get devices from daemon */
	devs = fwupd_client_get_devices(priv->client, priv->cancellable, error);
	if (devs == NULL)
		return FALSE;

	/* get results */
	for (guint i = 0; i < devs->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devs, i);
		if (fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED))
			devices_supported_cnt++;
		if (fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE))
			devices_updatable_cnt++;
	}

	/* TRANSLATORS: success message -- where 'metadata' is information
	 * about available firmware on the remote server */
	fu_console_print_literal(priv->console, _("Successfully downloaded new metadata:"));

	if (devices_updatable_cnt == 0) {
		fu_console_print_full(
		    priv->console,
		    FU_CONSOLE_PRINT_FLAG_LIST_ITEM | FU_CONSOLE_PRINT_FLAG_NEWLINE,
		    "%s",
		    /* TRANSLATORS: no devices that can be upgraded with new firmware */
		    _("No devices are updatable"));
	} else {
		fu_console_print_full(priv->console,
				      FU_CONSOLE_PRINT_FLAG_LIST_ITEM |
					  FU_CONSOLE_PRINT_FLAG_NEWLINE,
				      /* TRANSLATORS: how many devices could be updated in theory if
					 we had the firmware locally */
				      ngettext("%u device is updatable",
					       "%u devices are updatable",
					       devices_updatable_cnt),
				      devices_updatable_cnt);
		fu_console_print_full(priv->console,
				      FU_CONSOLE_PRINT_FLAG_LIST_ITEM |
					  FU_CONSOLE_PRINT_FLAG_NEWLINE,
				      /* TRANSLATORS: how many devices have published updates on
					 something like the LVFS */
				      ngettext("%u device is supported in the enabled remotes (an "
					       "update has been published)",
					       "%u devices are supported in the enabled remotes "
					       "(an update has been published)",
					       devices_supported_cnt),
				      devices_supported_cnt);
	}

	/* auto-upload any reports */
	if (!fu_cli_report_history_full(self, TRUE, &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		g_debug("failed to auto-upload reports: %s", error_local->message);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cli_refresh(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (g_strv_length(values) == 0)
		return fu_cli_download_metadata(self, error);
	if (g_strv_length(values) != 3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* open file */
	if (!fwupd_client_update_metadata(priv->client,
					  values[2],
					  values[0],
					  values[1],
					  priv->cancellable,
					  error))
		return FALSE;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	/* TRANSLATORS: success message -- the user can do this by-hand too */
	fu_console_print_literal(priv->console, _("Successfully refreshed metadata manually"));
	return TRUE;
}

static gboolean
fu_cli_get_results(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *tmp = NULL;
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(FwupdDevice) rel = NULL;

	dev = fu_cli_get_device_or_prompt(self, values, error);
	if (dev == NULL)
		return FALSE;
	rel = fwupd_client_get_results(priv->client,
				       fwupd_device_get_id(dev),
				       priv->cancellable,
				       error);
	if (rel == NULL)
		return FALSE;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
		fwupd_codec_to_json(FWUPD_CODEC(rel), json_obj, FWUPD_CODEC_FLAG_TRUSTED);
		fu_cli_print_json_object(priv->console, json_obj);
		return TRUE;
	}
	tmp = fu_cli_device_to_string(priv->client, rel, 0);
	fu_console_print_literal(priv->console, tmp);
	return TRUE;
}

static gboolean
fu_cli_get_releases(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(GPtrArray) rels = NULL;

	fu_cli_add_filter_device_include(self, FWUPD_DEVICE_FLAG_SUPPORTED);
	dev = fu_cli_get_device_or_prompt(self, values, error);
	if (dev == NULL)
		return FALSE;

	/* get the releases for this device */
	rels = fwupd_client_get_releases(priv->client,
					 fwupd_device_get_id(dev),
					 priv->cancellable,
					 error);
	if (rels == NULL)
		return FALSE;

	/* not for human consumption */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_cli_get_releases_as_json(self, rels);
		return TRUE;
	}

	if (rels->len == 0) {
		/* TRANSLATORS: no repositories to download from */
		fu_console_print_literal(priv->console, _("No releases available"));
		return TRUE;
	}
	if (g_log_get_debug_enabled()) {
		for (guint i = 0; i < rels->len; i++) {
			FwupdRelease *rel = g_ptr_array_index(rels, i);
			g_autofree gchar *tmp = NULL;
			if (!fu_cli_release_match_flags(self, rel))
				continue;
			tmp = fwupd_codec_to_string(FWUPD_CODEC(rel));
			fu_console_print_literal(priv->console, tmp);
		}
	} else {
		g_autoptr(FuCliNode) root = g_node_new(NULL);
		for (guint i = 0; i < rels->len; i++) {
			FwupdRelease *rel = g_ptr_array_index(rels, i);
			if (!fu_cli_release_match_flags(self, rel))
				continue;
			g_node_append_data(root, g_object_ref(rel));
		}
		fu_cli_print_node(self, root);
	}

	return TRUE;
}

static gboolean
fu_cli_search(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) rels = NULL;

	/* sanity check */
	if (g_strv_length(values) < 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments, expected WORD");
		return FALSE;
	}

	/* get the releases for this device */
	rels = fwupd_client_search(priv->client, values[0], priv->cancellable, error);
	if (rels == NULL)
		return FALSE;

	/* not for human consumption */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_cli_get_releases_as_json(self, rels);
		return TRUE;
	}

	if (rels->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    /* TRANSLATORS: no repositories to download from */
				    _("No matching releases for search token"));
		return FALSE;
	}
	if (g_log_get_debug_enabled()) {
		for (guint i = 0; i < rels->len; i++) {
			FwupdRelease *rel = g_ptr_array_index(rels, i);
			g_autofree gchar *tmp = NULL;
			if (!fu_cli_release_match_flags(self, rel))
				continue;
			tmp = fwupd_codec_to_string(FWUPD_CODEC(rel));
			fu_console_print_literal(priv->console, tmp);
		}
	} else {
		g_autoptr(FuCliNode) root = g_node_new(NULL);
		for (guint i = 0; i < rels->len; i++) {
			FwupdRelease *rel = g_ptr_array_index(rels, i);
			if (!fu_cli_release_match_flags(self, rel))
				continue;
			g_node_append_data(root, g_object_ref(rel));
		}
		fu_cli_print_node(self, root);
	}

	return TRUE;
}

static FwupdRelease *
fu_cli_prompt_for_release(FuCli *self, GPtrArray *rels_unfiltered, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FwupdRelease *rel;
	guint idx;
	g_autoptr(GPtrArray) rels = NULL;

	/* filter */
	rels = fu_cli_release_array_filter_flags(self, rels_unfiltered, error);
	if (rels == NULL)
		return NULL;

	/* exactly one */
	if (rels->len == 1) {
		rel = g_ptr_array_index(rels, 0);
		return g_object_ref(rel);
	}

	/* TRANSLATORS: this is to abort the interactive prompt */
	fu_console_print(priv->console, "0.\t%s", _("Cancel"));
	for (guint i = 0; i < rels->len; i++) {
		FwupdRelease *rel_tmp = g_ptr_array_index(rels, i);
		fu_console_print(priv->console,
				 "%u.\t%s",
				 i + 1,
				 fwupd_release_get_version(rel_tmp));
	}
	/* TRANSLATORS: get interactive prompt */
	idx = fu_console_input_uint(priv->console, rels->len, "%s", _("Choose release"));
	if (idx == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "Request canceled");
		return NULL;
	}
	rel = g_ptr_array_index(rels, idx - 1);
	return g_object_ref(rel);
}

static gboolean
fu_cli_verify(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdDevice) dev = NULL;

	fu_cli_add_filter_device_include(self, FWUPD_DEVICE_FLAG_CAN_VERIFY);
	dev = fu_cli_get_device_or_prompt(self, values, error);
	if (dev == NULL)
		return FALSE;

	if (!fwupd_client_verify(priv->client,
				 fwupd_device_get_id(dev),
				 priv->cancellable,
				 error)) {
		g_prefix_error(error, "failed to verify %s: ", fwupd_device_get_name(dev));
		return FALSE;
	}

	/* TRANSLATORS: success message when user verified device checksums */
	fu_console_print_literal(priv->console, _("Successfully verified device checksums"));

	return TRUE;
}

static gboolean
fu_cli_unlock(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdDevice) dev = NULL;

	fu_cli_add_filter_device_include(self, FWUPD_DEVICE_FLAG_LOCKED);
	dev = fu_cli_get_device_or_prompt(self, values, error);
	if (dev == NULL)
		return FALSE;

	if (!fwupd_client_unlock(priv->client, fwupd_device_get_id(dev), priv->cancellable, error))
		return FALSE;

	/* check flags after unlocking in case the operation changes them */
	if (fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN))
		priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN;
	if (fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT))
		priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_REBOOT;

	return fu_cli_prompt_complete(self, TRUE, error);
}

static gboolean
fu_cli_perhaps_refresh_remotes(FuCli *self, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	guint64 age_oldest = 0;
	const guint64 age_limit_days = 30;

	/* we don't want to ask anything */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_METADATA_CHECK) ||
	    fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_debug("skipping metadata check");
		return TRUE;
	}

	if (!fu_cli_check_oldest_remote(self, &age_oldest, NULL))
		return TRUE;

	/* metadata is new enough */
	if (age_oldest < 60 * 60 * 24 * age_limit_days)
		return TRUE;

	/* ask for permission */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES)) {
		fu_console_print(
		    priv->console,
		    /* TRANSLATORS: the metadata is very out of date; %u is a number > 1 */
		    ngettext("Firmware metadata has not been updated for %u"
			     " day and may not be up to date.",
			     "Firmware metadata has not been updated for %u"
			     " days and may not be up to date.",
			     (gint)age_limit_days),
		    (guint)age_limit_days);
		if (!fu_console_input_bool(priv->console,
					   FALSE,
					   "%s (%s)",
					   /* TRANSLATORS: ask if we can update metadata */
					   _("Update now?"),
					   /* TRANSLATORS: metadata is downloaded */
					   _("Requires internet connection")))
			return TRUE;
	}

	/* downloads new metadata */
	return fu_cli_download_metadata(self, error);
}

static void
fu_cli_get_updates_as_json(FuCli *self, GPtrArray *devices)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();

		/* not going to have results, so save a D-Bus round-trip */
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED))
			continue;

		/* get the releases for this device and filter for validity */
		rels = fwupd_client_get_upgrades(priv->client,
						 fwupd_device_get_id(dev),
						 priv->cancellable,
						 &error_local);
		if (rels == NULL) {
			g_debug("no upgrades: %s", error_local->message);
			continue;
		}
		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel = g_ptr_array_index(rels, j);
			if (!fu_cli_release_match_flags(self, rel))
				continue;
			fwupd_device_add_release(dev, rel);
		}

		/* add to builder */
		fwupd_codec_to_json(FWUPD_CODEC(dev), json_obj_tmp, FWUPD_CODEC_FLAG_TRUSTED);
		fwupd_json_array_add_object(json_arr, json_obj_tmp);
	}
	fwupd_json_object_add_array(json_obj, "Devices", json_arr);
	fu_cli_print_json_object(priv->console, json_obj);
}

static gint
fu_cli_sort_devices_by_flags_cb(gconstpointer a, gconstpointer b)
{
	FuDevice *dev_a = *((FuDevice **)a);
	FuDevice *dev_b = *((FuDevice **)b);

	if ((!fu_device_has_flag(dev_a, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	     fu_device_has_flag(dev_b, FWUPD_DEVICE_FLAG_UPDATABLE)) ||
	    (!fu_device_has_flag(dev_a, FWUPD_DEVICE_FLAG_SUPPORTED) &&
	     fu_device_has_flag(dev_b, FWUPD_DEVICE_FLAG_SUPPORTED)))
		return -1;
	if ((fu_device_has_flag(dev_a, FWUPD_DEVICE_FLAG_UPDATABLE) &&
	     !fu_device_has_flag(dev_b, FWUPD_DEVICE_FLAG_UPDATABLE)) ||
	    (fu_device_has_flag(dev_a, FWUPD_DEVICE_FLAG_SUPPORTED) &&
	     !fu_device_has_flag(dev_b, FWUPD_DEVICE_FLAG_SUPPORTED)))
		return 1;

	return 0;
}

static gboolean
fu_cli_get_updates(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) devices = NULL;
	gboolean supported = FALSE;
	g_autoptr(FuCliNode) root = g_node_new(NULL);
	g_autoptr(GPtrArray) devices_no_support = g_ptr_array_new();
	g_autoptr(GPtrArray) devices_no_upgrades = g_ptr_array_new();

	/* are the remotes very old */
	if (!fu_cli_perhaps_refresh_remotes(self, error))
		return FALSE;

	/* handle both forms */
	if (g_strv_length(values) == 0) {
		devices = fwupd_client_get_devices(priv->client, priv->cancellable, error);
		if (devices == NULL)
			return FALSE;
	} else {
		devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		for (guint idx = 0; idx < g_strv_length(values); idx++) {
			FwupdDevice *device = fu_cli_get_device_by_id(self, values[idx], error);
			if (device == NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_ARGS,
					    "'%s' is not a valid GUID nor DEVICE-ID",
					    values[idx]);
				return FALSE;
			}
			g_ptr_array_add(devices, device);
		}
	}
	g_ptr_array_sort(devices, fu_cli_sort_devices_by_flags_cb);

	/* not for human consumption */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_cli_get_updates_as_json(self, devices);
		return TRUE;
	}

	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_local = NULL;
		FuCliNode *child;

		/* not going to have results, so save a D-Bus round-trip */
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE) &&
		    !fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN))
			continue;
		if (!fu_cli_device_match_flags(self, dev))
			continue;
		if (!fu_cli_device_match_protocol(self, dev))
			continue;
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED)) {
			g_ptr_array_add(devices_no_support, dev);
			continue;
		}
		supported = TRUE;

		/* get the releases for this device and filter for validity */
		rels = fwupd_client_get_upgrades(priv->client,
						 fwupd_device_get_id(dev),
						 priv->cancellable,
						 &error_local);
		if (rels == NULL) {
			g_ptr_array_add(devices_no_upgrades, dev);
			/* discard the actual reason from user, but leave for debugging */
			g_debug("%s", error_local->message);
			continue;
		}
		child = g_node_append_data(root, g_object_ref(dev));

		/* add all releases */
		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel = g_ptr_array_index(rels, j);
			if (!fu_cli_release_match_flags(self, rel))
				continue;
			g_node_append_data(child, g_object_ref(rel));
		}
	}

	/* devices that have no updates available for whatever reason */
	if (devices_no_support->len > 0) {
		fu_console_print_literal(priv->console,
					 /* TRANSLATORS: message letting the user know no device
					  * upgrade available due to missing on LVFS */
					 _("Devices with no available firmware updates:"));
		for (guint i = 0; i < devices_no_support->len; i++) {
			FwupdDevice *dev = g_ptr_array_index(devices_no_support, i);
			fu_console_print(priv->console, " • %s", fwupd_device_get_name(dev));
		}
	}
	if (devices_no_upgrades->len > 0) {
		fu_console_print_literal(
		    priv->console,
		    /* TRANSLATORS: message letting the user know no device upgrade available */
		    _("Devices with the latest available firmware version:"));
		for (guint i = 0; i < devices_no_upgrades->len; i++) {
			FwupdDevice *dev = g_ptr_array_index(devices_no_upgrades, i);
			fu_console_print(priv->console, " • %s", fwupd_device_get_name(dev));
		}
	}

	/* nag? */
	if (!fu_cli_perhaps_show_unreported(self, error))
		return FALSE;

	/* no devices supported by LVFS or all are filtered */
	if (!supported) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: this is an error string */
				    _("No updatable devices"));
		return FALSE;
	}
	/* no updates available */
	if (g_node_n_nodes(root, G_TRAVERSE_ALL) <= 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: this is an error string */
				    _("No updates available"));
		return FALSE;
	}

	fu_cli_print_node(self, root);

	/* success */
	return TRUE;
}

static gboolean
fu_cli_get_remotes(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuCliNode) root = g_node_new(NULL);
	g_autoptr(GPtrArray) remotes = NULL;

	remotes = fwupd_client_get_remotes(priv->client, priv->cancellable, error);
	if (remotes == NULL)
		return FALSE;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
		fwupd_codec_array_to_json(remotes, "Remotes", json_obj, FWUPD_CODEC_FLAG_TRUSTED);
		fu_cli_print_json_object(priv->console, json_obj);
		return TRUE;
	}

	if (remotes->len == 0) {
		/* TRANSLATORS: no repositories to download from */
		fu_console_print_literal(priv->console, _("No remotes available"));
		return TRUE;
	}

	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote_tmp = g_ptr_array_index(remotes, i);
		g_node_append_data(root, g_object_ref(remote_tmp));
	}
	fu_cli_print_node(self, root);

	return TRUE;
}

static FwupdRelease *
fu_cli_get_release_with_branch(FuCli *self, FwupdDevice *dev, const gchar *branch, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) rels = NULL;

	/* find the newest release that matches */
	rels = fwupd_client_get_releases(priv->client,
					 fwupd_device_get_id(dev),
					 priv->cancellable,
					 error);
	if (rels == NULL)
		return NULL;
	for (guint i = 0; i < rels->len; i++) {
		FwupdRelease *rel = g_ptr_array_index(rels, i);
		if (!fu_cli_release_match_flags(self, rel))
			continue;
		if (g_strcmp0(branch, fwupd_release_get_branch(rel)) == 0)
			return g_object_ref(rel);
	}

	/* no match */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no matching releases for device");
	return NULL;
}

/* enumerate each problem, and also untranslated update error if set */
static GPtrArray *
fu_cli_device_problems_to_strings(FwupdClient *client, FwupdDevice *dev)
{
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);

	for (guint i = 0; i < 64; i++) {
		FwupdDeviceProblem problem = (guint64)1 << i;
		g_autofree gchar *str = NULL;

		if (!fwupd_device_has_problem(dev, problem))
			continue;
		str = fu_cli_device_problem_to_string(client, dev, problem);
		if (str == NULL)
			continue;
		g_ptr_array_add(array, g_steal_pointer(&str));
	}
	if (fwupd_device_get_update_error(dev) != NULL)
		g_ptr_array_add(array, g_strdup(fwupd_device_get_update_error(dev)));
	return g_steal_pointer(&array);
}

static gboolean
fu_cli_update_device_with_release(FuCli *self, FwupdDevice *dev, FwupdRelease *rel, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FwupdInstallFlags install_flags = fu_cli_get_install_flags(self);
	if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE)) {
		const gchar *name = fwupd_device_get_name(dev);
		g_autoptr(GPtrArray) array = fu_cli_device_problems_to_strings(priv->client, dev);

		/* enumerate each problem to the console */
		fu_console_print(priv->console,
				 /* TRANSLATORS: there are reasons the device can't update */
				 _("%s is not currently updatable:"),
				 name);
		for (guint i = 0; i < array->len; i++) {
			const gchar *str = g_ptr_array_index(array, i);
			fu_console_print_full(priv->console,
					      FU_CONSOLE_PRINT_FLAG_LIST_ITEM |
						  FU_CONSOLE_PRINT_FLAG_NEWLINE,
					      "%s",
					      str);
		}
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: no update can be installed */
				    _("Nothing to do"));
		return FALSE;
	}
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON) &&
	    !fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_SAFETY_CHECK) &&
	    !fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES)) {
		const gchar *title = fwupd_client_get_host_product(priv->client);
		if (!fu_cli_prompt_warning(self, dev, rel, title, error))
			return FALSE;
		if (!fu_cli_prompt_warning_fde(self, dev, error))
			return FALSE;
		if (!fu_cli_prompt_warning_composite(self, dev, rel, error))
			return FALSE;
		if (!fu_cli_prompt_warning_bkc(self, dev, rel, error))
			return FALSE;
	}
	return fwupd_client_install_release(priv->client,
					    dev,
					    rel,
					    install_flags,
					    priv->download_flags,
					    priv->cancellable,
					    error);
}

static gboolean
fu_cli_maybe_send_reports(FuCli *self, FwupdRelease *rel, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error_local = NULL;
	if (fwupd_release_get_remote_id(rel) == NULL) {
		g_debug("not sending reports, no remote");
		return TRUE;
	}
	remote = fwupd_client_get_remote_by_id(priv->client,
					       fwupd_release_get_remote_id(rel),
					       priv->cancellable,
					       error);
	if (remote == NULL)
		return FALSE;
	if (fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_AUTOMATIC_REPORTS)) {
		if (!fu_cli_report_history(self, NULL, &error_local))
			if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED))
				g_warning("%s", error_local->message);
	}

	return TRUE;
}

static gboolean
fu_cli_update(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FwupdInstallFlags install_flags = fu_cli_get_install_flags(self);
	gboolean supported = FALSE;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_latest = g_ptr_array_new();
	g_autoptr(GPtrArray) devices_pending = g_ptr_array_new();
	g_autoptr(GPtrArray) devices_unsupported = g_ptr_array_new();

	if (install_flags & FWUPD_INSTALL_FLAG_ALLOW_OLDER) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "--allow-older is not supported for this command");
		return FALSE;
	}

	if (install_flags & FWUPD_INSTALL_FLAG_ALLOW_REINSTALL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "--allow-reinstall is not supported for this command");
		return FALSE;
	}

	/* are the remotes very old */
	if (!fu_cli_perhaps_refresh_remotes(self, error))
		return FALSE;

	/* DEVICE-ID and GUID are acceptable args to update */
	for (guint idx = 0; idx < g_strv_length(values); idx++) {
		if (!fwupd_guid_is_valid(values[idx]) && !fwupd_device_id_is_valid(values[idx])) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "'%s' is not a valid GUID nor DEVICE-ID",
				    values[idx]);
			return FALSE;
		}
	}

	/* get devices from daemon */
	devices = fwupd_client_get_devices(priv->client, priv->cancellable, error);
	if (devices == NULL)
		return FALSE;
	fu_cli_set_current_operation(self, FU_CLI_OPERATION_UPDATE);
	g_ptr_array_sort(devices, fu_cli_sort_devices_by_flags_cb);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		const gchar *device_id = fwupd_device_get_id(dev);
		g_autoptr(FwupdRelease) rel = NULL;
		g_autoptr(GPtrArray) rels = NULL;
		g_autoptr(GError) error_install = NULL;
		g_autoptr(GError) error_report = NULL;
		gboolean dev_skip_byid = TRUE;
		gboolean ret;

		/* not going to have results, so save a D-Bus round-trip */
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE) &&
		    !fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN))
			continue;
		if (!fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED)) {
			g_ptr_array_add(devices_unsupported, dev);
			continue;
		}

		/* only process particular DEVICE-ID or GUID if specified */
		for (guint idx = 0; idx < g_strv_length(values); idx++) {
			const gchar *tmpid = values[idx];
			if (fwupd_device_has_guid(dev, tmpid) || g_strcmp0(device_id, tmpid) == 0) {
				dev_skip_byid = FALSE;
				break;
			}
		}
		if (g_strv_length(values) > 0 && dev_skip_byid)
			continue;
		if (!fu_cli_device_match_flags(self, dev))
			continue;
		if (!fu_cli_device_match_protocol(self, dev))
			continue;
		supported = TRUE;

		/* get the releases for this device and filter for validity */
		rels = fwupd_client_get_upgrades(priv->client,
						 fwupd_device_get_id(dev),
						 priv->cancellable,
						 &error_install);
		if (rels == NULL) {
			g_ptr_array_add(devices_latest, dev);
			/* discard the actual reason from user, but leave for debugging */
			g_debug("%s", error_install->message);
			continue;
		}
		for (guint j = 0; j < rels->len; j++) {
			FwupdRelease *rel_tmp = g_ptr_array_index(rels, j);
			if (!fu_cli_release_match_flags(self, rel_tmp))
				continue;
			rel = g_object_ref(rel_tmp);
			break;
		}
		if (rel == NULL)
			continue;

		/* something is wrong */
		if (fwupd_device_get_problems(dev) != FWUPD_DEVICE_PROBLEM_NONE) {
			g_ptr_array_add(devices_pending, dev);
			continue;
		}

		ret = fu_cli_update_device_with_release(self, dev, rel, &error_install);
		if (!ret &&
		    g_error_matches(error_install, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
			g_debug("ignoring %s: %s",
				fwupd_device_get_id(dev),
				error_install->message);
			continue;
		}
		if (ret)
			fu_cli_display_current_message(self);

		/* send report if we're supposed to */
		if (!fu_cli_maybe_send_reports(self, rel, &error_report)) {
			/* install failed, report failed */
			if (!ret) {
				g_warning("%s", error_report->message);
				/* install succeeded, but report failed */
			} else {
				g_propagate_error(error, g_steal_pointer(&error_report));
				return FALSE;
			}
		}

		if (!ret) {
			g_propagate_error(error, g_steal_pointer(&error_install));
			return FALSE;
		}
	}

	/* show warnings */
	if (devices_latest->len > 0 && !fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_console_print_literal(priv->console,
					 /* TRANSLATORS: message letting the user know no device
					  * upgrade available */
					 _("Devices with the latest available firmware version:"));
		for (guint i = 0; i < devices_latest->len; i++) {
			FwupdDevice *dev = g_ptr_array_index(devices_latest, i);
			fu_console_print(priv->console, " • %s", fwupd_device_get_name(dev));
		}
	}
	if (devices_unsupported->len > 0 && !fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_console_print_literal(priv->console,
					 /* TRANSLATORS: message letting the user know no
					  * device upgrade available due to missing on LVFS */
					 _("Devices with no available firmware updates:"));
		for (guint i = 0; i < devices_unsupported->len; i++) {
			FwupdDevice *dev = g_ptr_array_index(devices_unsupported, i);
			fu_console_print(priv->console, " • %s", fwupd_device_get_name(dev));
		}
	}
	if (devices_pending->len > 0 && !fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_console_print_literal(priv->console,
					 /* TRANSLATORS: message letting the user there is an update
					  * waiting, but there is a reason it cannot be deployed */
					 _("Devices with firmware updates that need user action:"));
		for (guint i = 0; i < devices_pending->len; i++) {
			FwupdDevice *dev = g_ptr_array_index(devices_pending, i);
			fu_console_print(priv->console, " • %s", fwupd_device_get_name(dev));
			for (guint j = 0; j < 64; j++) {
				FwupdDeviceProblem problem = (guint64)1 << j;
				g_autofree gchar *desc = NULL;
				if (!fwupd_device_has_problem(dev, problem))
					continue;
				desc = fu_cli_device_problem_to_string(priv->client, dev, problem);
				if (desc == NULL)
					continue;
				fu_console_print(priv->console, "   ‣ %s", desc);
			}
		}
	}

	/* no devices supported by LVFS or all are filtered */
	if (!supported) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "No updatable devices");
		return FALSE;
	}

	/* we don't want to ask anything */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_REBOOT_CHECK) ||
	    fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	return fu_cli_prompt_complete(self, TRUE, error);
}

static gboolean
fu_cli_remote_modify(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdRemote) remote = NULL;

	if (g_strv_length(values) < 3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* ensure the remote exists */
	remote = fwupd_client_get_remote_by_id(priv->client, values[0], priv->cancellable, error);
	if (remote == NULL)
		return FALSE;
	if (!fwupd_client_modify_remote(priv->client,
					fwupd_remote_get_id(remote),
					values[1],
					values[2],
					priv->cancellable,
					error))
		return FALSE;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	/* TRANSLATORS: success message for a per-remote setting change */
	fu_console_print_literal(priv->console, _("Successfully modified remote"));
	return TRUE;
}

static gboolean
fu_cli_remote_enable(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdRemote) remote = NULL;

	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}
	remote = fwupd_client_get_remote_by_id(priv->client, values[0], priv->cancellable, error);
	if (remote == NULL)
		return FALSE;
	if (!fu_cli_modify_remote_warning(self,
					  remote,
					  fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES),
					  error))
		return FALSE;
	if (!fwupd_client_modify_remote(priv->client,
					fwupd_remote_get_id(remote),
					"Enabled",
					"true",
					priv->cancellable,
					error))
		return FALSE;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	/* ask for permission to refresh */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_REMOTE_CHECK) ||
	    fwupd_remote_get_kind(remote) != FWUPD_REMOTE_KIND_DOWNLOAD) {
		/* TRANSLATORS: success message */
		fu_console_print_literal(priv->console, _("Successfully enabled remote"));
		return TRUE;
	}
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES)) {
		if (!fu_console_input_bool(priv->console,
					   TRUE,
					   "%s (%s)",
					   /* TRANSLATORS: ask if we can update the metadata */
					   _("Do you want to refresh this remote now?"),
					   /* TRANSLATORS: metadata is downloaded */
					   _("Requires internet connection"))) {
			/* TRANSLATORS: success message */
			fu_console_print_literal(priv->console, _("Successfully enabled remote"));
			return TRUE;
		}
	}
	if (!fwupd_client_refresh_remote(priv->client,
					 remote,
					 priv->download_flags,
					 priv->cancellable,
					 error))
		return FALSE;

	/* TRANSLATORS: success message */
	fu_console_print_literal(priv->console, _("Successfully enabled and refreshed remote"));
	return TRUE;
}

static gboolean
fu_cli_remote_clean(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdRemote) remote = NULL;
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}
	remote = fwupd_client_get_remote_by_id(priv->client, values[0], priv->cancellable, error);
	if (remote == NULL)
		return FALSE;
	if (!fwupd_client_clean_remote(priv->client,
				       fwupd_remote_get_id(remote),
				       priv->cancellable,
				       error))
		return FALSE;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	/* TRANSLATORS: success message */
	fu_console_print_literal(priv->console, _("Successfully cleaned remote"));
	return TRUE;
}

static gboolean
fu_cli_remote_disable(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdRemote) remote = NULL;

	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* ensure the remote exists */
	remote = fwupd_client_get_remote_by_id(priv->client, values[0], priv->cancellable, error);
	if (remote == NULL)
		return FALSE;
	if (!fwupd_client_modify_remote(priv->client,
					values[0],
					"Enabled",
					"false",
					priv->cancellable,
					error))
		return FALSE;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	/* TRANSLATORS: success message */
	fu_console_print_literal(priv->console, _("Successfully disabled remote"));

	/* delete the now-unused cache files? */
	if (fwupd_remote_get_kind(remote) == FWUPD_REMOTE_KIND_DOWNLOAD &&
	    fwupd_remote_get_age(remote) != G_MAXUINT64) {
		if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES) ||
		    fu_console_input_bool(priv->console,
					  FALSE,
					  "%s",
					  /* TRANSLATORS: this is now useless */
					  _("Delete the now-unused remote cache files?"))) {
			if (!fwupd_client_clean_remote(priv->client,
						       values[0],
						       priv->cancellable,
						       error))
				return FALSE;
			fu_console_print_literal(priv->console,
						 /* TRANSLATORS: success message */
						 _("Successfully cleaned remote"));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cli_downgrade(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	gboolean ret;
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(GPtrArray) rels = NULL;
	g_autoptr(GError) error_report = NULL;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ALLOW_REINSTALL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "--allow-reinstall is not supported for this command");
		return FALSE;
	}

	fu_cli_add_filter_device_include(self, FWUPD_DEVICE_FLAG_SUPPORTED);
	dev = fu_cli_get_device_or_prompt(self, values, error);
	if (dev == NULL)
		return FALSE;

	/* get the releases for this device and filter for validity */
	rels = fwupd_client_get_downgrades(priv->client,
					   fwupd_device_get_id(dev),
					   priv->cancellable,
					   error);
	if (rels == NULL) {
		g_autofree gchar *downgrade_str =
		    /* TRANSLATORS: message letting the user know no device downgrade available
		     * %1 is the device name */
		    g_strdup_printf(_("No downgrades for %s"), fwupd_device_get_name(dev));
		g_prefix_error(error, "%s: ", downgrade_str); /* nocheck:error */
		return FALSE;
	}

	/* get the chosen release */
	rel = fu_cli_prompt_for_release(self, rels, error);
	if (rel == NULL)
		return FALSE;

	/* update the console if composite devices are also updated */
	fu_cli_set_current_operation(self, FU_CLI_OPERATION_DOWNGRADE);
	fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_ALLOW_OLDER);
	ret = fu_cli_update_device_with_release(self, dev, rel, error);
	if (ret)
		fu_cli_display_current_message(self);

	/* send report if we're supposed to */
	if (!fu_cli_maybe_send_reports(self, rel, &error_report)) {
		/* install failed, report failed */
		if (!ret) {
			g_warning("%s", error_report->message);
			/* install succeeded, but report failed */
		} else {
			g_propagate_error(error, g_steal_pointer(&error_report));
			return FALSE;
		}
	}

	/* install failed */
	if (!ret)
		return FALSE;

	/* we don't want to ask anything */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_REBOOT_CHECK)) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	return fu_cli_prompt_complete(self, TRUE, error);
}

static gboolean
fu_cli_reinstall(FuCli *self, gchar **values, GError **error)
{
	gboolean ret;
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(GError) error_report = NULL;

	fu_cli_add_filter_device_include(self, FWUPD_DEVICE_FLAG_SUPPORTED);
	dev = fu_cli_get_device_or_prompt(self, values, error);
	if (dev == NULL)
		return FALSE;

	/* try to lookup/match release from client */
	rel =
	    fu_cli_get_release_for_device_version(self, dev, fwupd_device_get_version(dev), error);
	if (rel == NULL)
		return FALSE;

	/* update the console if composite devices are also updated */
	fu_cli_set_current_operation(self, FU_CLI_OPERATION_INSTALL);
	fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_ALLOW_REINSTALL);
	ret = fu_cli_update_device_with_release(self, dev, rel, error);
	if (ret)
		fu_cli_display_current_message(self);

	/* send report if we're supposed to */
	if (!fu_cli_maybe_send_reports(self, rel, &error_report)) {
		/* install failed, report failed */
		if (!ret) {
			g_warning("%s", error_report->message);
			/* install succeeded, but report failed */
		} else {
			g_propagate_error(error, g_steal_pointer(&error_report));
			return FALSE;
		}
	}

	/* install failed */
	if (!ret)
		return FALSE;

	/* we don't want to ask anything */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_REBOOT_CHECK)) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	return fu_cli_prompt_complete(self, TRUE, error);
}

static gboolean
fu_cli_install(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	gboolean ret;
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(GError) error_report = NULL;

	/* fall back for CLI compatibility */
	if (g_strv_length(values) >= 1) {
		if (g_file_test(values[0], G_FILE_TEST_EXISTS) || fu_cli_is_url(values[0]))
			return fu_cli_local_install(self, values, error);
	}

	/* find device */
	fu_cli_add_filter_device_include(self, FWUPD_DEVICE_FLAG_SUPPORTED);
	dev = fu_cli_get_device_or_prompt(self, values, error);
	if (dev == NULL)
		return FALSE;

	/* find release */
	if (g_strv_length(values) >= 2) {
		rel = fu_cli_get_release_for_device_version(self, dev, values[1], error);
		if (rel == NULL)
			return FALSE;
	} else {
		g_autoptr(GPtrArray) rels = NULL;
		rels = fwupd_client_get_releases(priv->client,
						 fwupd_device_get_id(dev),
						 priv->cancellable,
						 error);
		if (rels == NULL)
			return FALSE;
		rel = fu_cli_prompt_for_release(self, rels, error);
		if (rel == NULL)
			return FALSE;
	}

	/* allow all actions */
	fu_cli_set_current_operation(self, FU_CLI_OPERATION_INSTALL);
	ret = fu_cli_update_device_with_release(self, dev, rel, error);
	if (ret)
		fu_cli_display_current_message(self);

	/* send report if we're supposed to */
	if (!fu_cli_maybe_send_reports(self, rel, &error_report)) {
		/* install failed, report failed */
		if (!ret) {
			g_warning("%s", error_report->message);
			/* install succeeded, but report failed */
		} else {
			g_propagate_error(error, g_steal_pointer(&error_report));
			return FALSE;
		}
	}

	/* install failed */
	if (!ret)
		return FALSE;

	/* we don't want to ask anything */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_REBOOT_CHECK)) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	return fu_cli_prompt_complete(self, TRUE, error);
}

static gboolean
_g_str_equal0(gconstpointer str1, gconstpointer str2)
{
	return g_strcmp0(str1, str2) == 0;
}

static gboolean
fu_cli_switch_branch(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	const gchar *branch;
	gboolean ret;
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(GPtrArray) rels = NULL;
	g_autoptr(GPtrArray) branches = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(GError) error_report = NULL;

	/* find the device and check it has multiple branches */
	fu_cli_add_filter_device_include(self, FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES);
	fu_cli_add_filter_device_include(self, FWUPD_DEVICE_FLAG_UPDATABLE);
	dev = fu_cli_get_device_or_prompt(self, values, error);
	if (dev == NULL)
		return FALSE;

	/* get all releases, including the alternate branch versions */
	rels = fwupd_client_get_releases(priv->client,
					 fwupd_device_get_id(dev),
					 priv->cancellable,
					 error);
	if (rels == NULL)
		return FALSE;

	/* get all the unique branches */
	for (guint i = 0; i < rels->len; i++) {
		FwupdRelease *rel_tmp = g_ptr_array_index(rels, i);
		const gchar *branch_tmp = fwupd_release_get_branch(rel_tmp);
		if (!fu_cli_release_match_flags(self, rel_tmp))
			continue;
		if (g_ptr_array_find_with_equal_func(branches, branch_tmp, _g_str_equal0, NULL))
			continue;
		g_ptr_array_add(branches, g_strdup(branch_tmp));
	}

	/* branch name is optional */
	if (g_strv_length(values) > 1) {
		branch = values[1];
	} else if (branches->len == 1) {
		branch = g_ptr_array_index(branches, 0);
	} else {
		guint idx;

		/* TRANSLATORS: this is to abort the interactive prompt */
		fu_console_print(priv->console, "0.\t%s", _("Cancel"));
		for (guint i = 0; i < branches->len; i++) {
			const gchar *branch_tmp = g_ptr_array_index(branches, i);
			fu_console_print(priv->console,
					 "%u.\t%s",
					 i + 1,
					 fu_cli_branch_for_display(branch_tmp));
		}
		/* TRANSLATORS: get interactive prompt, where branch is the
		 * supplier of the firmware, e.g. "non-free" or "free" */
		idx = fu_console_input_uint(priv->console, branches->len, "%s", _("Choose branch"));
		if (idx == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOTHING_TO_DO,
					    "Request canceled");
			return FALSE;
		}
		branch = g_ptr_array_index(branches, idx - 1);
	}

	/* sanity check */
	if (g_strcmp0(branch, fwupd_device_get_branch(dev)) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Device %s is already on branch %s",
			    fwupd_device_get_name(dev),
			    fu_cli_branch_for_display(branch));
		return FALSE;
	}

	/* the releases are ordered by version */
	for (guint j = 0; j < rels->len; j++) {
		FwupdRelease *rel_tmp = g_ptr_array_index(rels, j);
		if (g_strcmp0(fwupd_release_get_branch(rel_tmp), branch) == 0) {
			rel = g_object_ref(rel_tmp);
			break;
		}
	}
	if (rel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "No releases for branch %s",
			    fu_cli_branch_for_display(branch));
		return FALSE;
	}

	/* we're switching branch */
	if (!fu_cli_switch_branch_warning(self,
					  dev,
					  rel,
					  fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES),
					  error))
		return FALSE;

	/* update the console if composite devices are also updated */
	fu_cli_set_current_operation(self, FU_CLI_OPERATION_INSTALL);
	fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_ALLOW_REINSTALL);
	fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_ALLOW_BRANCH_SWITCH);
	ret = fu_cli_update_device_with_release(self, dev, rel, error);
	if (ret)
		fu_cli_display_current_message(self);

	/* send report if we're supposed to */
	if (!fu_cli_maybe_send_reports(self, rel, &error_report)) {
		/* install failed, report failed */
		if (!ret) {
			g_warning("%s", error_report->message);
			/* install succeeded, but report failed */
		} else {
			g_propagate_error(error, g_steal_pointer(&error_report));
			return FALSE;
		}
	}

	/* install failed */
	if (!ret)
		return FALSE;

	/* we don't want to ask anything */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_REBOOT_CHECK)) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	return fu_cli_prompt_complete(self, TRUE, error);
}

static gboolean
fu_cli_activate(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) devices = NULL;
	gboolean has_pending = FALSE;

	/* handle both forms */
	if (g_strv_length(values) == 0) {
		/* activate anything with _NEEDS_ACTIVATION */
		devices = fwupd_client_get_devices(priv->client, priv->cancellable, error);
		if (devices == NULL)
			return FALSE;
		for (guint i = 0; i < devices->len; i++) {
			FwupdDevice *device = g_ptr_array_index(devices, i);
			if (fwupd_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
				has_pending = TRUE;
				break;
			}
		}
	} else if (g_strv_length(values) == 1) {
		FwupdDevice *device = fu_cli_get_device_by_id(self, values[0], error);
		if (device == NULL)
			return FALSE;
		devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		g_ptr_array_add(devices, device);
		if (fwupd_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION))
			has_pending = TRUE;
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments");
		return FALSE;
	}

	/* nothing to do */
	if (!has_pending) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "No firmware to activate");
		return FALSE;
	}

	/* activate anything with _NEEDS_ACTIVATION */
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *device = g_ptr_array_index(devices, i);
		if (!fu_cli_device_match_flags(self, device))
			continue;
		if (!fu_cli_device_match_protocol(self, device))
			continue;
		if (!fwupd_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION))
			continue;
		fu_console_print(
		    priv->console,
		    "%s %s…",
		    /* TRANSLATORS: shown when shutting down to switch to the new version */
		    _("Activating firmware update for"),
		    fwupd_device_get_name(device));
		if (!fwupd_client_activate(priv->client,
					   priv->cancellable,
					   fwupd_device_get_id(device),
					   error))
			return FALSE;
	}

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	/* TRANSLATORS: success message -- where activation is making the new
	 * firmware take effect, usually after updating offline */
	fu_console_print_literal(priv->console, _("Successfully activated all devices"));
	return TRUE;
}

static gboolean
fu_cli_set_approved_firmware(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_auto(GStrv) checksums = NULL;

	/* check args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: filename or list of checksums expected");
		return FALSE;
	}

	/* filename */
	if (g_file_test(values[0], G_FILE_TEST_EXISTS)) {
		g_autofree gchar *data = NULL;
		if (!g_file_get_contents(values[0], &data, NULL, error))
			return FALSE;
		checksums = g_strsplit(data, "\n", -1);
	} else {
		checksums = g_strsplit(values[0], ",", -1);
	}

	/* call into daemon */
	return fwupd_client_set_approved_firmware(priv->client,
						  checksums,
						  priv->cancellable,
						  error);
}

static void
fu_cli_get_checksums_as_json(FuCli *self, gchar **csums)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	for (guint i = 0; csums[i] != NULL; i++)
		fwupd_json_array_add_string(json_arr, csums[i]);
	fwupd_json_object_add_array(json_obj, "Checksums", json_arr);
	fu_cli_print_json_object(priv->console, json_obj);
}

static gboolean
fu_cli_get_approved_firmware(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_auto(GStrv) checksums = NULL;

	/* check args */
	if (g_strv_length(values) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: none expected");
		return FALSE;
	}

	/* call into daemon */
	checksums = fwupd_client_get_approved_firmware(priv->client, priv->cancellable, error);
	if (checksums == NULL)
		return FALSE;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_cli_get_checksums_as_json(self, checksums);
		return TRUE;
	}
	if (g_strv_length(checksums) == 0) {
		/* TRANSLATORS: approved firmware has been checked by
		 * the domain administrator */
		fu_console_print_literal(priv->console, _("There is no approved firmware."));
	} else {
		fu_console_print_literal(
		    priv->console,
		    /* TRANSLATORS: approved firmware has been checked by
		     * the domain administrator */
		    ngettext("Approved firmware:", "Approved firmware:", g_strv_length(checksums)));
		for (guint i = 0; checksums[i] != NULL; i++)
			fu_console_print(priv->console, " * %s", checksums[i]);
	}
	return TRUE;
}

static gboolean
fu_cli_modify_config(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);

	/* check args */
	if (g_strv_length(values) == 3) {
		if (!fwupd_client_modify_config(priv->client,
						values[0],
						values[1],
						values[2],
						priv->cancellable,
						error))
			return FALSE;
	} else if (g_strv_length(values) == 2) {
		if (!fwupd_client_modify_config(priv->client,
						"fwupd",
						values[0],
						values[1],
						priv->cancellable,
						error))
			return FALSE;
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: [SECTION] KEY VALUE expected");
		return FALSE;
	}

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	/* most config settings require a daemon restart */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_USES_DAEMON)) {
		if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES)) {
			if (!fu_console_input_bool(
				priv->console,
				FALSE,
				"%s",
				/* TRANSLATORS: changes only take effect on restart */
				_("Restart the daemon to make the change effective?")))
				return TRUE;
		}
		if (!fu_cli_quit(self, NULL, error))
			return FALSE;
		if (!fwupd_client_connect(priv->client, priv->cancellable, error))
			return FALSE;
	}

	/* TRANSLATORS: success message -- a per-system setting value */
	fu_console_print_literal(priv->console, _("Successfully modified configuration value"));
	return TRUE;
}

static gboolean
fu_cli_reset_config(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	/* check args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments: SECTION expected");
		return FALSE;
	}
	if (!fwupd_client_reset_config(priv->client, values[0], priv->cancellable, error))
		return FALSE;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES)) {
		if (!fu_console_input_bool(priv->console,
					   FALSE,
					   "%s",
					   /* TRANSLATORS: changes only take effect on restart */
					   _("Restart the daemon to make the change effective?")))
			return TRUE;
	}
	if (!fu_cli_quit(self, NULL, error))
		return FALSE;
	if (!fwupd_client_connect(priv->client, priv->cancellable, error))
		return FALSE;

	/* TRANSLATORS: success message -- a per-system setting value */
	fu_console_print_literal(priv->console, _("Successfully reset configuration values"));
	return TRUE;
}

static FwupdRemote *
fu_cli_get_remote_with_report_uri(FuCli *self, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) remotes = NULL;

	/* get all remotes */
	remotes = fwupd_client_get_remotes(priv->client, priv->cancellable, error);
	if (remotes == NULL)
		return NULL;

	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(remotes, i);
		if (!fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED))
			continue;
		if (fwupd_remote_get_report_uri(remote) != NULL)
			return g_object_ref(remote);
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "No remotes specified ReportURI");
	return NULL;
}

static gboolean
fu_cli_upload_security(FuCli *self, GPtrArray *attrs, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *data = NULL;
	g_autofree gchar *report_uri = NULL;
	g_autofree gchar *sig = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GHashTable) metadata = NULL;

	/* can we find a remote with a security attr */
	remote = fu_cli_get_remote_with_report_uri(self, &error_local);
	if (remote == NULL) {
		g_debug("failed to find suitable remote: %s", error_local->message);
		return TRUE;
	}

	/* export as a string */
	metadata = fwupd_client_get_report_metadata(priv->client, priv->cancellable, error);
	if (metadata == NULL)
		return FALSE;
	data = fwupd_client_build_report_security(priv->client, attrs, metadata, error);
	if (data == NULL)
		return FALSE;

	/* ask for permission */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES) &&
	    !fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS)) {
		if (!fu_console_input_bool(priv->console,
					   FALSE,
					   /* TRANSLATORS: ask the user to share, %s is something
					    * like: "Linux Vendor Firmware Service" */
					   _("Upload these anonymous results to the %s to help "
					     "other users?"),
					   fwupd_remote_get_title(remote))) {
			return TRUE;
		}
	}

	/* self sign data */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_SIGN)) {
		sig = fwupd_client_self_sign(priv->client,
					     data,
					     FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP,
					     priv->cancellable,
					     error);
		if (sig == NULL)
			return FALSE;
	}

	/* ask for permission */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES) &&
	    !fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS)) {
		fu_console_print_kv(priv->console,
				    _("Target"),
				    fwupd_remote_get_report_uri(remote));
		fu_console_print_kv(priv->console, _("Payload"), data);
		if (sig != NULL)
			fu_console_print_kv(priv->console, _("Signature"), sig);
		if (!fu_console_input_bool(priv->console, TRUE, "%s", _("Proceed with upload?"))) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_PERMISSION_DENIED,
					    "User declined action");
			return FALSE;
		}
	}

	/* POST request */
	report_uri = fwupd_remote_build_report_uri(remote, error);
	if (report_uri == NULL)
		return FALSE;
	uri = fwupd_client_upload_report(priv->client,
					 report_uri,
					 data,
					 sig,
					 FWUPD_CLIENT_UPLOAD_FLAG_ALWAYS_MULTIPART,
					 priv->cancellable,
					 error);
	if (uri == NULL)
		return FALSE;
	fu_console_print_literal(priv->console,
				 /* TRANSLATORS: success, so say thank you to the user */
				 _("Host Security ID attributes uploaded successfully, thanks!"));

	/* as this worked, ask if the user want to do this every time */
	if (!fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_AUTOMATIC_SECURITY_REPORTS)) {
		if (fu_console_input_bool(priv->console,
					  FALSE,
					  "%s",
					  /* TRANSLATORS: can we JFDI? */
					  _("Automatically upload every time?"))) {
			if (!fwupd_client_modify_remote(priv->client,
							fwupd_remote_get_id(remote),
							"AutomaticSecurityReports",
							"true",
							priv->cancellable,
							error))
				return FALSE;
		}
	}

	return TRUE;
}

static void
fu_cli_security_as_json(FuCli *self, GPtrArray *attrs, GPtrArray *events, GPtrArray *devices)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) devices_issues = NULL;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();

	/* attrs */
	fwupd_codec_array_to_json(attrs, "SecurityAttributes", json_obj, FWUPD_CODEC_FLAG_TRUSTED);

	/* events */
	if (events != NULL && events->len > 0) {
		fwupd_codec_array_to_json(events,
					  "SecurityEvents",
					  json_obj,
					  FWUPD_CODEC_FLAG_TRUSTED);
	}

	/* devices */
	devices_issues = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	for (guint i = 0; devices != NULL && i < devices->len; i++) {
		FwupdDevice *device = g_ptr_array_index(devices, i);
		GPtrArray *issues = fwupd_device_get_issues(device);
		if (issues->len == 0)
			continue;
		g_ptr_array_add(devices_issues, g_object_ref(device));
	}
	if (devices_issues->len > 0) {
		fwupd_codec_array_to_json(devices_issues,
					  "Devices",
					  json_obj,
					  FWUPD_CODEC_FLAG_TRUSTED);
	}
	fu_cli_print_json_object(priv->console, json_obj);
}

static gboolean
fu_cli_sync(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	const gchar *host_bkc = fwupd_client_get_host_bkc(priv->client);
	guint cnt = 0;
	g_autoptr(GPtrArray) devices = NULL;

	/* update the console if composite devices are also updated */
	fu_cli_set_current_operation(self, FU_CLI_OPERATION_INSTALL);
	fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_ALLOW_OLDER);

	devices = fwupd_client_get_devices(priv->client, NULL, error);
	if (devices == NULL)
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		g_autoptr(FwupdRelease) rel = NULL;
		g_autoptr(GError) error_local = NULL;

		/* find the release that matches the tag */
		if (host_bkc != NULL) {
			rel = fu_cli_get_release_with_tag(self, dev, host_bkc, &error_local);
		} else if (fu_device_get_branch(dev) != NULL) {
			rel = fu_cli_get_release_with_branch(self,
							     dev,
							     fu_device_get_branch(dev),
							     &error_local);
		} else {
			g_set_error_literal(&error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "No device branch or system HostBkc set");
			/* nocheck:error-false-return */
		}
		if (rel == NULL) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED) ||
			    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
				g_debug("ignoring %s: %s",
					fwupd_device_get_id(dev),
					error_local->message);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}

		/* ignore if already on that release */
		if (g_strcmp0(fwupd_device_get_version(dev), fwupd_release_get_version(rel)) == 0)
			continue;

		/* install this new release */
		g_debug("need to move %s from %s to %s",
			fwupd_device_get_id(dev),
			fwupd_device_get_version(dev),
			fwupd_release_get_version(rel));
		if (!fu_cli_update_device_with_release(self, dev, rel, &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
				g_debug("ignoring %s: %s",
					fwupd_device_get_id(dev),
					error_local->message);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		fu_cli_display_current_message(self);
		cnt++;
	}

	/* nothing was done */
	if (cnt == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "No devices required modification");
		return FALSE;
	}

	/* we don't want to ask anything */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_REBOOT_CHECK)) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	/* show reboot if needed */
	return fu_cli_prompt_complete(self, TRUE, error);
}

static gboolean
fu_cli_security_fix_attr(FuCli *self, FwupdSecurityAttr *attr, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GString) body = g_string_new(NULL);
	g_autoptr(GString) title = g_string_new(NULL);

	g_string_append_printf(title,
			       "%s: %s",
			       /* TRANSLATORS: title prefix for the BIOS settings dialog */
			       _("Configuration Change Suggested"),
			       fwupd_security_attr_get_title(attr));

	g_string_append(body, fwupd_security_attr_get_description(attr));

	if (fwupd_security_attr_get_bios_setting_id(attr) != NULL &&
	    fwupd_security_attr_get_bios_setting_current_value(attr) != NULL &&
	    fwupd_security_attr_get_bios_setting_target_value(attr) != NULL) {
		g_string_append(body, "\n\n");
		g_string_append_printf(body,
				       /* TRANSLATORS: the %1 is a BIOS setting name.
					* %2 and %3 are the values, e.g. "True" or "Windows10" */
				       _("This tool can change the BIOS setting '%s' from '%s' "
					 "to '%s' automatically, but it will only be active after "
					 "restarting the computer."),
				       fwupd_security_attr_get_bios_setting_id(attr),
				       fwupd_security_attr_get_bios_setting_current_value(attr),
				       fwupd_security_attr_get_bios_setting_target_value(attr));
		g_string_append(body, "\n\n");
		g_string_append(body,
				/* TRANSLATORS: the user has to manually recover; we can't do it */
				_("You should ensure you are comfortable restoring the setting "
				  "from the system firmware setup, as this change may cause the "
				  "system to not boot into Linux or cause other system "
				  "instability."));
	} else if (fwupd_security_attr_get_kernel_target_value(attr) != NULL) {
		g_string_append(body, "\n\n");
		if (fwupd_security_attr_get_kernel_current_value(attr) != NULL) {
			g_string_append_printf(
			    body,
			    /* TRANSLATORS: the %1 is a kernel command line key=value */
			    _("This tool can change the kernel argument from '%s' to '%s', but "
			      "it will only be active after restarting the computer."),
			    fwupd_security_attr_get_kernel_current_value(attr),
			    fwupd_security_attr_get_kernel_target_value(attr));
		} else {
			g_string_append_printf(
			    body,
			    /* TRANSLATORS: the %1 is a kernel command line key=value */
			    _("This tool can add a kernel argument of '%s', but it will "
			      "only be active after restarting the computer."),
			    fwupd_security_attr_get_kernel_target_value(attr));
		}
		g_string_append(body, "\n\n");
		g_string_append(body,
				/* TRANSLATORS: the user has to manually recover; we can't do it */
				_("You should ensure you are comfortable restoring the setting "
				  "from a recovery or installation disk, as this change may cause "
				  "the system to not boot into Linux or cause other system "
				  "instability."));
	}

	fu_console_box(priv->console, title->str, body->str, 80);

	/* TRANSLATORS: prompt to apply the update */
	if (!fu_console_input_bool(priv->console, FALSE, "%s", _("Perform operation?")))
		return TRUE;
	if (!fwupd_client_fix_host_security_attr(priv->client,
						 fwupd_security_attr_get_appstream_id(attr),
						 priv->cancellable,
						 error))
		return FALSE;

	/* do not offer to upload the report */
	priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_REBOOT;
	return TRUE;
}

static gchar *
fu_cli_security_issues_to_string(GPtrArray *devices)
{
	g_autoptr(GString) str = g_string_new(NULL);

	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *device = g_ptr_array_index(devices, i);
		GPtrArray *issues = fwupd_device_get_issues(device);
		if (issues->len == 0)
			continue;
		if (str->len == 0) {
			g_string_append_printf(
			    str,
			    "%s\n",
			    /* TRANSLATORS: now list devices with unfixed high-priority issues */
			    _("There are devices with issues:"));
		}
		g_string_append_printf(str,
				       "\n  %s — %s:\n",
				       fwupd_device_get_vendor(device),
				       fwupd_device_get_name(device));
		for (guint j = 0; j < issues->len; j++) {
			const gchar *issue = g_ptr_array_index(issues, j);
			g_string_append_printf(str, "   • %s\n", issue);
		}
	}

	/* no output required */
	if (str->len == 0)
		return NULL;

	/* success */
	return g_string_free(g_steal_pointer(&str), FALSE);
}

static const gchar *
fu_cli_security_attr_result_to_string(FwupdSecurityAttrResult result)
{
	if (result == FWUPD_SECURITY_ATTR_RESULT_VALID) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Valid");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_VALID) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Invalid");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_ENABLED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Enabled");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Disabled");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_LOCKED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Locked");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Unlocked");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Encrypted");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Unencrypted");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_TAINTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Tainted");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Untainted");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_FOUND) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Found");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Not found");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_SUPPORTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Supported");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Not supported");
	}
	return NULL;
}

static gchar *
fu_cli_security_event_to_string(FwupdSecurityAttr *attr)
{
	const gchar *name;
	struct {
		const gchar *appstream_id;
		FwupdSecurityAttrResult result_old;
		FwupdSecurityAttrResult result_new;
		const gchar *text;
	} items[] = {{FWUPD_SECURITY_ATTR_ID_IOMMU,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("IOMMU device protection enabled")},
		     {FWUPD_SECURITY_ATTR_ID_IOMMU,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND,
		      /* TRANSLATORS: HSI event title */
		      _("IOMMU device protection disabled")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS,
		      FWUPD_SECURITY_ATTR_RESULT_TAINTED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED,
		      NULL},
		     {FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED,
		      FWUPD_SECURITY_ATTR_RESULT_TAINTED,
		      NULL},
		     {FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS,
		      FWUPD_SECURITY_ATTR_RESULT_UNKNOWN,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      NULL},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED,
		      FWUPD_SECURITY_ATTR_RESULT_TAINTED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED,
		      /* TRANSLATORS: HSI event title */
		      _("Kernel is no longer tainted")},
		     {FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED,
		      FWUPD_SECURITY_ATTR_RESULT_TAINTED,
		      /* TRANSLATORS: HSI event title */
		      _("Kernel is tainted")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("Kernel lockdown disabled")},
		     {FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("Kernel lockdown enabled")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("Pre-boot DMA protection is disabled")},
		     {FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("Pre-boot DMA protection is enabled")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("Secure Boot disabled")},
		     {FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("Secure Boot enabled")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR,
		      FWUPD_SECURITY_ATTR_RESULT_UNKNOWN,
		      FWUPD_SECURITY_ATTR_RESULT_VALID,
		      /* TRANSLATORS: HSI event title */
		      _("All TPM PCRs are valid")},
		     {FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR,
		      FWUPD_SECURITY_ATTR_RESULT_VALID,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_VALID,
		      /* TRANSLATORS: HSI event title */
		      _("A TPM PCR is now an invalid value")},
		     {FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_VALID,
		      FWUPD_SECURITY_ATTR_RESULT_VALID,
		      /* TRANSLATORS: HSI event title */
		      _("All TPM PCRs are now valid")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_VALID,
		      /* TRANSLATORS: HSI event title */
		      _("TPM PCR0 reconstruction is invalid")},
		     {FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_VALID,
		      FWUPD_SECURITY_ATTR_RESULT_VALID,
		      /* TRANSLATORS: HSI event title */
		      _("TPM PCR0 reconstruction is now valid")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_UEFI_MEMORY_PROTECTION,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED,
		      FWUPD_SECURITY_ATTR_RESULT_LOCKED,
		      /* TRANSLATORS: HSI event title */
		      _("UEFI memory protection is now locked")},
		     {FWUPD_SECURITY_ATTR_ID_UEFI_MEMORY_PROTECTION,
		      FWUPD_SECURITY_ATTR_RESULT_LOCKED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED,
		      /* TRANSLATORS: HSI event title */
		      _("UEFI memory protection is now unlocked")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_UEFI_NX_COMPAT,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("UEFI NX bootloader protection is now enabled")},
		     {FWUPD_SECURITY_ATTR_ID_UEFI_NX_COMPAT,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("UEFI NX bootloader protection is now disabled")},
		     /* ------------------------------------------*/
		     {FWUPD_SECURITY_ATTR_ID_UEFI_DB,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_VALID,
		      FWUPD_SECURITY_ATTR_RESULT_VALID,
		      /* TRANSLATORS: HSI event title */
		      _("The UEFI certificate store is now up to date")},
		     {FWUPD_SECURITY_ATTR_ID_HP_SURESTART,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED,
		      FWUPD_SECURITY_ATTR_RESULT_ENABLED,
		      /* TRANSLATORS: HSI event title */
		      _("HP SureStart is enabled")},
		     {FWUPD_SECURITY_ATTR_ID_AMD_ENTRY_SIGN,
		      FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED,
		      FWUPD_SECURITY_ATTR_RESULT_LOCKED,
		      /* TRANSLATORS: HSI event title */
		      _("AMD microcode signature vulnerability fixed")},
		     {NULL, 0, 0, NULL}};

	/* sanity check */
	if (fwupd_security_attr_get_appstream_id(attr) == NULL)
		return NULL;
	if (fwupd_security_attr_get_result(attr) == FWUPD_SECURITY_ATTR_RESULT_UNKNOWN &&
	    fwupd_security_attr_get_result_fallback(attr) == FWUPD_SECURITY_ATTR_RESULT_UNKNOWN)
		return NULL;

	/* look for prepared text */
	for (guint i = 0; items[i].appstream_id != NULL; i++) {
		if (g_strcmp0(fwupd_security_attr_get_appstream_id(attr), items[i].appstream_id) ==
			0 &&
		    fwupd_security_attr_get_result(attr) == items[i].result_new &&
		    fwupd_security_attr_get_result_fallback(attr) == items[i].result_old)
			return g_strdup(items[i].text);
	}

	/* disappeared */
	if (fwupd_security_attr_get_result(attr) == FWUPD_SECURITY_ATTR_RESULT_UNKNOWN) {
		name = dgettext(GETTEXT_PACKAGE, fwupd_security_attr_get_name(attr));
		return g_strdup_printf(
		    /* TRANSLATORS: %1 refers to some kind of security test, e.g. "SPI BIOS region".
		       %2 refers to a result value, e.g. "Invalid" */
		    _("%s disappeared: %s"),
		    name,
		    fu_cli_security_attr_result_to_string(
			fwupd_security_attr_get_result_fallback(attr)));
	}

	/* appeared */
	if (fwupd_security_attr_get_result_fallback(attr) == FWUPD_SECURITY_ATTR_RESULT_UNKNOWN) {
		name = dgettext(GETTEXT_PACKAGE, fwupd_security_attr_get_name(attr));
		return g_strdup_printf(
		    /* TRANSLATORS: %1 refers to some kind of security test, e.g. "Encrypted RAM".
		       %2 refers to a result value, e.g. "Invalid" */
		    _("%s appeared: %s"),
		    name,
		    fu_cli_security_attr_result_to_string(fwupd_security_attr_get_result(attr)));
	}

	/* fall back to something sensible */
	name = dgettext(GETTEXT_PACKAGE, fwupd_security_attr_get_name(attr));
	return g_strdup_printf(
	    /* TRANSLATORS: %1 refers to some kind of security test, e.g. "UEFI platform key".
	     * %2 and %3 refer to results value, e.g. "Valid" and "Invalid" */
	    _("%s changed: %s → %s"),
	    name,
	    fu_cli_security_attr_result_to_string(fwupd_security_attr_get_result_fallback(attr)),
	    fu_cli_security_attr_result_to_string(fwupd_security_attr_get_result(attr)));
}

static gchar *
fu_cli_security_events_to_string(GPtrArray *events, FuSecurityAttrToStringFlags strflags)
{
	g_autoptr(GString) str = g_string_new(NULL);

	/* debugging */
	if (g_log_get_debug_enabled()) {
		for (guint i = 0; i < events->len; i++) {
			FwupdSecurityAttr *attr = g_ptr_array_index(events, i);
			g_autofree gchar *tmp = fwupd_codec_to_string(FWUPD_CODEC(attr));
			g_info("%s", tmp);
		}
	}

	for (guint i = 0; i < events->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(events, i);
		g_autoptr(GDateTime) date = NULL;
		g_autofree gchar *dtstr = NULL;
		g_autofree gchar *check = NULL;
		g_autofree gchar *eventstr = NULL;

		/* skip events that have either been added or removed with no prior value */
		if (fwupd_security_attr_get_result(attr) == FWUPD_SECURITY_ATTR_RESULT_UNKNOWN ||
		    fwupd_security_attr_get_result_fallback(attr) ==
			FWUPD_SECURITY_ATTR_RESULT_UNKNOWN)
			continue;

		date = g_date_time_new_from_unix_utc((gint64)fwupd_security_attr_get_created(attr));
		dtstr = g_date_time_format(date, "%F %T");
		eventstr = fu_cli_security_event_to_string(attr);
		if (eventstr == NULL)
			continue;
		if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
			check = fu_console_color_format("✔", FU_CONSOLE_COLOR_GREEN);
		} else {
			check = fu_console_color_format("✘", FU_CONSOLE_COLOR_RED);
		}
		if (str->len == 0) {
			/* TRANSLATORS: title for host security events */
			g_string_append_printf(str, "%s\n", _("Host Security Events"));
		}
		g_string_append_printf(str, "  %s:  %s %s\n", dtstr, check, eventstr);
	}

	/* no output required */
	if (str->len == 0)
		return NULL;

	/* success */
	return g_string_free(g_steal_pointer(&str), FALSE);
}

static const gchar *
fu_cli_security_attr_get_result(FwupdSecurityAttr *attr)
{
	const gchar *tmp;

	/* common case */
	tmp = fu_cli_security_attr_result_to_string(fwupd_security_attr_get_result(attr));
	if (tmp != NULL)
		return tmp;

	/* fallback */
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("OK");
	}

	/* TRANSLATORS: Suffix: the fallback HSI result */
	return _("Unknown");
}

static void
fu_cli_security_attr_append_str(FwupdSecurityAttr *attr,
				GString *str,
				FuSecurityAttrToStringFlags flags)
{
	const gchar *name;

	/* hide obsoletes by default */
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED) &&
	    (flags & FU_SECURITY_ATTR_TO_STRING_FLAG_SHOW_OBSOLETES) == 0)
		return;

	name = dgettext(GETTEXT_PACKAGE, fwupd_security_attr_get_name(attr));
	if (name == NULL)
		name = fwupd_security_attr_get_appstream_id(attr);
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED)) {
		g_string_append(str, "✦ ");
	} else if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		g_string_append(str, "✔ ");
	} else {
		g_string_append(str, "✘ ");
	}
	g_string_append_printf(str, "%s:", name);
	for (guint i = fu_strwidth(name); i < 30; i++)
		g_string_append(str, " ");
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED)) {
		g_autofree gchar *fmt =
		    fu_console_color_format(fu_cli_security_attr_get_result(attr),
					    FU_CONSOLE_COLOR_YELLOW);
		g_string_append(str, fmt);
	} else if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		g_autofree gchar *fmt =
		    fu_console_color_format(fu_cli_security_attr_get_result(attr),
					    FU_CONSOLE_COLOR_GREEN);
		g_string_append(str, fmt);
	} else {
		g_autofree gchar *fmt =
		    fu_console_color_format(fu_cli_security_attr_get_result(attr),
					    FU_CONSOLE_COLOR_RED);
		g_string_append(str, fmt);
	}
	if ((flags & FU_SECURITY_ATTR_TO_STRING_FLAG_SHOW_URLS) > 0 &&
	    fwupd_security_attr_get_url(attr) != NULL) {
		g_string_append_printf(str, ": %s", fwupd_security_attr_get_url(attr));
	}
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED)) {
		/* TRANSLATORS: this is shown as a suffix for obsoleted tests */
		g_string_append_printf(str, " %s", _("(obsoleted)"));
	}
	g_string_append_printf(str, "\n");
}

static gchar *
fu_cli_security_attrs_to_string(GPtrArray *attrs, FuSecurityAttrToStringFlags strflags)
{
	FwupdSecurityAttrFlags flags = FWUPD_SECURITY_ATTR_FLAG_NONE;
	const FwupdSecurityAttrFlags hpi_suffixes[] = {
	    FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE,
	    FWUPD_SECURITY_ATTR_FLAG_NONE,
	};
	GString *str = g_string_new(NULL);
	gboolean low_help = FALSE;
	gboolean runtime_help = FALSE;
	gboolean pcr0_help = FALSE;

	for (guint j = 1; j <= FWUPD_SECURITY_ATTR_LEVEL_LAST; j++) {
		gboolean has_header = FALSE;
		for (guint i = 0; i < attrs->len; i++) {
			FwupdSecurityAttr *attr = g_ptr_array_index(attrs, i);
			if (fwupd_security_attr_get_level(attr) != j)
				continue;
			if (!has_header) {
				g_string_append_printf(str, "\n\033[1mHSI-%u\033[0m\n", j);
				has_header = TRUE;
			}
			fu_cli_security_attr_append_str(attr, str, strflags);
			/* make sure they have at least HSI-1 */
			if (j < FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT &&
			    !fwupd_security_attr_has_flag(attr,
							  FWUPD_SECURITY_ATTR_FLAG_OBSOLETED) &&
			    !fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
				low_help = TRUE;

			/* check for PCR0 not matching */
			if (g_strcmp0(fwupd_security_attr_get_appstream_id(attr),
				      FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0) == 0 &&
			    fwupd_security_attr_get_result(attr) ==
				FWUPD_SECURITY_ATTR_RESULT_NOT_VALID)
				pcr0_help = TRUE;
		}
	}
	for (guint i = 0; i < attrs->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(attrs, i);
		flags |= fwupd_security_attr_get_flags(attr);
	}
	for (guint j = 0; hpi_suffixes[j] != FWUPD_SECURITY_ATTR_FLAG_NONE; j++) {
		if (flags & hpi_suffixes[j]) {
			g_string_append_printf(str,
					       "\n\033[1m%s -%s\033[0m\n",
					       /* TRANSLATORS:  this is the HSI suffix */
					       _("Runtime Suffix"),
					       fwupd_security_attr_flag_to_suffix(hpi_suffixes[j]));
			for (guint i = 0; i < attrs->len; i++) {
				FwupdSecurityAttr *attr = g_ptr_array_index(attrs, i);
				if (!fwupd_security_attr_has_flag(attr, hpi_suffixes[j]))
					continue;
				if (fwupd_security_attr_has_flag(
					attr,
					FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE) &&
				    !fwupd_security_attr_has_flag(attr,
								  FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
					runtime_help = TRUE;
				fu_cli_security_attr_append_str(attr, str, strflags);
			}
		}
	}

	if (low_help) {
		g_string_append_printf(
		    str,
		    "\n%s\n » %s\n",
		    /* TRANSLATORS: this is instructions on how to improve the HSI security level */
		    _("This system has a low HSI security level."),
		    "https://fwupd.github.io/hsi.html#low-security-level");
	}
	if (runtime_help) {
		g_string_append_printf(
		    str,
		    "\n%s\n » %s\n",
		    /* TRANSLATORS: this is instructions on how to improve the HSI suffix */
		    _("This system has HSI runtime issues."),
		    "https://fwupd.github.io/hsi.html#hsi-runtime-suffix");
	}

	if (pcr0_help) {
		g_string_append_printf(
		    str,
		    "\n%s\n » %s\n",
		    /* TRANSLATORS: this is more background on a security measurement problem */
		    _("The TPM PCR0 differs from reconstruction."),
		    "https://fwupd.github.io/hsi.html#pcr0-tpm-event-log-reconstruction");
	}

	return g_string_free(str, FALSE);
}

static gboolean
fu_cli_security(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FuSecurityAttrToStringFlags flags = FU_SECURITY_ATTR_TO_STRING_FLAG_NONE;
	g_autoptr(GPtrArray) attrs = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) events = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *str = NULL;

#ifndef HAVE_HSI
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    /* TRANSLATORS: error message for unsupported feature */
			    _("Host Security ID (HSI) is not supported"));
	return FALSE;
#endif /* HAVE_HSI */

	/* the "why" */
	attrs = fwupd_client_get_host_security_attrs(priv->client, priv->cancellable, error);
	if (attrs == NULL)
		return FALSE;

	/* the "when" */
	events = fwupd_client_get_host_security_events(priv->client,
						       10,
						       priv->cancellable,
						       &error_local);
	if (events == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_debug("ignoring failed events: %s", error_local->message);
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	/* the "also" */
	devices = fwupd_client_get_devices(priv->client, priv->cancellable, &error_local);
	if (devices == NULL) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	/* not for human consumption */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_cli_security_as_json(self, attrs, events, devices);
		return TRUE;
	}

	fu_console_print(priv->console,
			 "%s \033[1m%s\033[0m",
			 /* TRANSLATORS: this is a string like 'HSI:2-U' */
			 _("Host Security ID:"),
			 fwupd_client_get_host_security_id(priv->client));

	/* show or hide different elements */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_SHOW_ALL)) {
		flags |= FU_SECURITY_ATTR_TO_STRING_FLAG_SHOW_OBSOLETES;
		flags |= FU_SECURITY_ATTR_TO_STRING_FLAG_SHOW_URLS;
	}
	str = fu_cli_security_attrs_to_string(attrs, flags);
	fu_console_print_literal(priv->console, str);

	/* events */
	if (events != NULL && events->len > 0) {
		g_autofree gchar *estr = fu_cli_security_events_to_string(events, flags);
		if (estr != NULL)
			fu_console_print_literal(priv->console, estr);
	}

	/* known CVEs */
	if (devices != NULL && devices->len > 0) {
		g_autofree gchar *estr = fu_cli_security_issues_to_string(devices);
		if (estr != NULL)
			fu_console_print_literal(priv->console, estr);
	}

	/* host emulation */
	for (guint j = 0; j < attrs->len; j++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(attrs, j);
		if (g_strcmp0(fwupd_security_attr_get_appstream_id(attr),
			      FWUPD_SECURITY_ATTR_ID_HOST_EMULATION) == 0) {
			fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_UNREPORTED_CHECK);
			break;
		}
	}

	/* any things we can fix? */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_SECURITY_FIX)) {
		for (guint j = 0; j < attrs->len; j++) {
			FwupdSecurityAttr *attr = g_ptr_array_index(attrs, j);
			if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_CAN_FIX) &&
			    !fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
				if (!fu_cli_security_fix_attr(self, attr, error))
					return FALSE;
			}
		}
	}

	/* upload, with confirmation */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_UNREPORTED_CHECK)) {
		if (!fu_cli_upload_security(self, attrs, error))
			return FALSE;
	}

	/* reboot is required? */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_REBOOT_CHECK) &&
	    (priv->completion_flags & FWUPD_DEVICE_FLAG_NEEDS_REBOOT) > 0) {
		if (!fu_cli_prompt_complete(self, TRUE, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

#ifdef HAVE_GIO_UNIX
static gboolean
fu_cli_watch_sigint_cb(gpointer user_data)
{
	FuCli *self = FU_CLI(user_data);
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_debug("handling SIGINT");
	g_cancellable_cancel(priv->cancellable);
	return FALSE;
}
#endif

void
fu_cli_watch_sigint_start(FuCli *self)
{
#ifdef HAVE_GIO_UNIX
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (priv->source_sigint != NULL)
		return;
	priv->source_sigint = g_unix_signal_source_new(SIGINT);
	g_source_set_callback(priv->source_sigint, fu_cli_watch_sigint_cb, self, NULL);
	g_source_attach(priv->source_sigint, priv->main_ctx);
#endif
}

void
fu_cli_watch_sigint_stop(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (priv->source_sigint == NULL)
		return;
	g_source_destroy(priv->source_sigint);
	priv->source_sigint = NULL;
}

void
fu_cli_loop_run(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_main_loop_run(priv->loop);
}

FwupdClient *
fu_cli_get_client(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return priv->client;
}

GMainContext *
fu_cli_get_main_ctx(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return priv->main_ctx;
}

FuConsole *
fu_cli_get_console(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return priv->console;
}

GCancellable *
fu_cli_get_cancellable(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return priv->cancellable;
}

static gint
fu_cli_plugin_name_sort_cb(FwupdPlugin **item1, FwupdPlugin **item2)
{
	return g_strcmp0(fwupd_plugin_get_name(*item1), fwupd_plugin_get_name(*item2));
}

static gchar *
fu_cli_plugin_flag_to_cli_text(FwupdPluginFlags plugin_flag)
{
	g_autofree gchar *plugin_flag_str = fu_cli_plugin_flag_to_string(plugin_flag);
	switch (plugin_flag) {
	case FWUPD_PLUGIN_FLAG_UNKNOWN:
	case FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE:
	case FWUPD_PLUGIN_FLAG_USER_WARNING:
	case FWUPD_PLUGIN_FLAG_NONE:
		return NULL;
	case FWUPD_PLUGIN_FLAG_READY:
	case FWUPD_PLUGIN_FLAG_REQUIRE_HWID:
	case FWUPD_PLUGIN_FLAG_MODULAR:
	case FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY:
	case FWUPD_PLUGIN_FLAG_SECURE_CONFIG:
		return fu_console_color_format(plugin_flag_str, FU_CONSOLE_COLOR_GREEN);
	case FWUPD_PLUGIN_FLAG_DISABLED:
	case FWUPD_PLUGIN_FLAG_NO_HARDWARE:
	case FWUPD_PLUGIN_FLAG_TEST_ONLY:
	case FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION:
		return fu_console_color_format(plugin_flag_str, FU_CONSOLE_COLOR_YELLOW);
	case FWUPD_PLUGIN_FLAG_LEGACY_BIOS:
	case FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED:
	case FWUPD_PLUGIN_FLAG_UNLOCK_REQUIRED:
	case FWUPD_PLUGIN_FLAG_AUTH_REQUIRED:
	case FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED:
	case FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND:
	case FWUPD_PLUGIN_FLAG_ESP_NOT_VALID:
	case FWUPD_PLUGIN_FLAG_KERNEL_TOO_OLD:
		return fu_console_color_format(plugin_flag_str, FU_CONSOLE_COLOR_RED);
	default:
		break;
	}

	/* fall back for unknown types */
	return g_steal_pointer(&plugin_flag_str);
}

static gchar *
fu_cli_plugin_to_string(FwupdPlugin *plugin, guint idt)
{
	GString *str = g_string_new(NULL);
	const gchar *hdr;
	guint64 flags = fwupd_plugin_get_flags(plugin);

	fwupd_codec_string_append(str, idt, fwupd_plugin_get_name(plugin), "");

	/* TRANSLATORS: description of plugin state, e.g. disabled */
	hdr = _("Flags");
	if (flags == 0x0) {
		g_autofree gchar *tmp = fu_cli_plugin_flag_to_cli_text(flags);
		g_autofree gchar *li = g_strdup_printf("• %s", tmp);
		fwupd_codec_string_append(str, idt + 1, hdr, li);
	} else {
		for (guint i = 0; i < 64; i++) {
			g_autofree gchar *li = NULL;
			g_autofree gchar *tmp = NULL;
			if ((flags & ((guint64)1 << i)) == 0)
				continue;
			tmp = fu_cli_plugin_flag_to_cli_text((guint64)1 << i);
			if (tmp == NULL)
				continue;
			li = g_strdup_printf("• %s", tmp);
			fwupd_codec_string_append(str, idt + 1, hdr, li);

			/* clear header */
			hdr = "";
		}
	}

	return g_string_free(str, FALSE);
}

static gboolean
fu_cli_cmd_get_plugins(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) plugins = NULL;

	plugins = fwupd_client_get_plugins(priv->client, priv->cancellable, NULL);
	if (plugins == NULL)
		return FALSE;
	g_ptr_array_sort(plugins, (GCompareFunc)fu_cli_plugin_name_sort_cb);
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
		fwupd_codec_array_to_json(plugins, "Plugins", json_obj, FWUPD_CODEC_FLAG_TRUSTED);
		fu_cli_print_json_object(priv->console, json_obj);
		return TRUE;
	}

	/* print */
	for (guint i = 0; i < plugins->len; i++) {
		FuPlugin *plugin = g_ptr_array_index(plugins, i);
		g_autofree gchar *str = fu_cli_plugin_to_string(FWUPD_PLUGIN(plugin), 0);
		fu_console_print_literal(priv->console, str);
	}

	/* success */
	return TRUE;
}

typedef struct {
	FuCli *self;
	gboolean as_json;
	gboolean allow_branch_switch;
	gboolean allow_older;
	gboolean allow_reinstall;
	gboolean assume_yes;
	gboolean disable_ssl_strict;
	gboolean force;
	gboolean show_all;
	gboolean no_device_prompt;
	gboolean no_reboot_check;
	gboolean no_safety_check;
	gboolean no_unreported_check;
	gboolean no_remote_check;
	gboolean no_security_fix;
	gboolean no_metadata_check;
	gboolean sign;
	gboolean no_history;
	gboolean only_emulated;
	gboolean is_interactive;
	gboolean no_authenticate;
	gboolean only_p2p;
	gboolean version;
	guint download_retries;
	gchar *filter_device;
	gchar *filter_release;
	gchar **filter_protocols;
} FuCliGroupHelper;

static void
fu_cli_group_helper_free(FuCliGroupHelper *helper)
{
	g_object_unref(helper->self);
	g_free(helper->filter_device);
	g_free(helper->filter_release);
	g_strfreev(helper->filter_protocols);
	g_free(helper);
}

static gboolean
fu_cli_parse_filter_device_flags(FuCli *self, const gchar *filter, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FwupdDeviceFlags tmp;
	g_auto(GStrv) strv = g_strsplit(filter, ",", -1);

	g_return_val_if_fail(filter != NULL, FALSE);

	for (guint i = 0; strv[i] != NULL; i++) {
		if (g_str_has_prefix(strv[i], "~")) {
			tmp = fwupd_device_flag_from_string(strv[i] + 1);
			if (tmp == FWUPD_DEVICE_FLAG_UNKNOWN) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Unknown device flag %s",
					    strv[i] + 1);
				return FALSE;
			}
			if ((tmp & priv->filter_device_include) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already included",
					    fwupd_device_flag_to_string(tmp));
				return FALSE;
			}
			if ((tmp & priv->filter_device_exclude) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already excluded",
					    fwupd_device_flag_to_string(tmp));
				return FALSE;
			}
			priv->filter_device_exclude |= tmp;
		} else {
			tmp = fwupd_device_flag_from_string(strv[i]);
			if (tmp == FWUPD_DEVICE_FLAG_UNKNOWN) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Unknown device flag %s",
					    strv[i]);
				return FALSE;
			}
			if ((tmp & priv->filter_device_exclude) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already excluded",
					    fwupd_device_flag_to_string(tmp));
				return FALSE;
			}
			if ((tmp & priv->filter_device_include) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already included",
					    fwupd_device_flag_to_string(tmp));
				return FALSE;
			}
			priv->filter_device_include |= tmp;
		}
	}

	return TRUE;
}

static gboolean
fu_cli_parse_filter_release_flags(FuCli *self, const gchar *filter, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FwupdReleaseFlags tmp;
	g_auto(GStrv) strv = g_strsplit(filter, ",", -1);

	g_return_val_if_fail(filter != NULL, FALSE);

	for (guint i = 0; strv[i] != NULL; i++) {
		if (g_str_has_prefix(strv[i], "~")) {
			tmp = fwupd_release_flag_from_string(strv[i] + 1);
			if (tmp == FWUPD_RELEASE_FLAG_UNKNOWN) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Unknown release flag %s",
					    strv[i] + 1);
				return FALSE;
			}
			if ((tmp & priv->filter_release_include) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already included",
					    fwupd_release_flag_to_string(tmp));
				return FALSE;
			}
			if ((tmp & priv->filter_release_exclude) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already excluded",
					    fwupd_release_flag_to_string(tmp));
				return FALSE;
			}
			priv->filter_release_exclude |= tmp;
		} else {
			tmp = fwupd_release_flag_from_string(strv[i]);
			if (tmp == FWUPD_RELEASE_FLAG_UNKNOWN) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Unknown release flag %s",
					    strv[i]);
				return FALSE;
			}
			if ((tmp & priv->filter_release_exclude) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already excluded",
					    fwupd_release_flag_to_string(tmp));
				return FALSE;
			}
			if ((tmp & priv->filter_release_include) > 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "Filter %s already included",
					    fwupd_release_flag_to_string(tmp));
				return FALSE;
			}
			priv->filter_release_include |= tmp;
		}
	}

	return TRUE;
}

/* a protocol must not contain spaces and must contain at least one dot */
static gboolean
fu_cli_validate_protocol(const gchar *protocol)
{
	if (protocol == NULL)
		return FALSE;
	if (g_strstr_len(protocol, -1, ".") == NULL)
		return FALSE;
	if (g_strstr_len(protocol, -1, " ") != NULL)
		return FALSE;
	return TRUE;
}

static gboolean
fu_cli_parse_filter_protocol_flags(FuCli *self, gchar **filters, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	for (guint i = 0; filters[i] != NULL; i++) {
		const gchar *protocol = filters[i];
		if (g_str_has_prefix(protocol, "~")) {
			if (!fu_cli_validate_protocol(protocol + 1)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid filtered protocol '%s'",
					    protocol + 1);
				return FALSE;
			}
			g_ptr_array_add(priv->filter_protocols_exclude, g_strdup(protocol + 1));
		} else {
			if (!fu_cli_validate_protocol(protocol)) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid filtered protocol '%s'",
					    protocol);
				return FALSE;
			}
			g_ptr_array_add(priv->filter_protocols_include, g_strdup(protocol));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cli_group_pre_parse_cb(GOptionContext *context,
			  GOptionGroup *group,
			  gpointer data,
			  GError **error)
{
	FuCliGroupHelper *helper = (FuCliGroupHelper *)data;
	const GOptionEntry entries[] = {
	    {"version",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->version,
	     /* TRANSLATORS: command line option */
	     N_("Show client and daemon versions"),
	     NULL},
	    {"download-retries",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_INT,
	     &helper->download_retries,
	     /* TRANSLATORS: command line option */
	     N_("Set the download retries for transient errors"),
	     NULL},
	    {"p2p",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->only_p2p,
	     /* TRANSLATORS: command line option */
	     N_("Only use peer-to-peer networking when downloading files"),
	     NULL},
	    {"no-authenticate",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->no_authenticate,
	     /* TRANSLATORS: command line option */
	     N_("Don't prompt for authentication (less details may be shown)"),
	     NULL},
	    {"json",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->as_json,
	     /* TRANSLATORS: command line option */
	     N_("Output in JSON format (disables all interactive prompts)"),
	     NULL},
	    {"allow-reinstall",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->allow_reinstall,
	     /* TRANSLATORS: command line option */
	     N_("Allow reinstalling existing firmware versions"),
	     NULL},
	    {"allow-older",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->allow_older,
	     /* TRANSLATORS: command line option */
	     N_("Allow downgrading firmware versions"),
	     NULL},
	    {"allow-branch-switch",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->allow_branch_switch,
	     /* TRANSLATORS: command line option */
	     N_("Allow switching firmware branch"),
	     NULL},
	    {"force",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->force,
	     /* TRANSLATORS: command line option */
	     N_("Force the action by relaxing some runtime checks"),
	     NULL},
	    {"assume-yes",
	     'y',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->assume_yes,
	     /* TRANSLATORS: command line option */
	     N_("Answer yes to all questions"),
	     NULL},
	    {"sign",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->sign,
	     /* TRANSLATORS: command line option */
	     N_("Sign the uploaded data with the client certificate"),
	     NULL},
	    {"no-unreported-check",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->no_unreported_check,
	     /* TRANSLATORS: command line option */
	     N_("Do not check for unreported history"),
	     NULL},
	    {"no-metadata-check",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->no_metadata_check,
	     /* TRANSLATORS: command line option */
	     N_("Do not check for old metadata"),
	     NULL},
	    {"no-remote-check",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->no_remote_check,
	     /* TRANSLATORS: command line option */
	     N_("Do not check if download remotes should be enabled"),
	     NULL},
	    {"no-reboot-check",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->no_reboot_check,
	     /* TRANSLATORS: command line option */
	     N_("Do not check or prompt for reboot after update"),
	     NULL},
	    {"no-safety-check",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->no_safety_check,
	     /* TRANSLATORS: command line option */
	     N_("Do not perform device safety checks"),
	     NULL},
	    {"no-device-prompt",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->no_device_prompt,
	     /* TRANSLATORS: command line option */
	     N_("Do not prompt for devices"),
	     NULL},
	    {"show-all",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->show_all,
	     /* TRANSLATORS: command line option */
	     N_("Show all results"),
	     NULL},
	    {"show-all-devices",
	     '\0',
	     G_OPTION_FLAG_HIDDEN,
	     G_OPTION_ARG_NONE,
	     &helper->show_all,
	     /* TRANSLATORS: command line option */
	     N_("Show devices that are not updatable"),
	     NULL},
	    {"disable-ssl-strict",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->disable_ssl_strict,
	     /* TRANSLATORS: command line option */
	     N_("Ignore SSL strict checks when downloading files"),
	     NULL},
	    {"no-security-fix",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->no_security_fix,
	     /* TRANSLATORS: command line option */
	     N_("Do not prompt to fix security issues"),
	     NULL},
	    {"only-emulated",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->only_emulated,
	     /* TRANSLATORS: command line option */
	     N_("Only install onto emulated devices"),
	     NULL},
	    {"no-history",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_NONE,
	     &helper->no_history,
	     /* TRANSLATORS: command line option */
	     N_("Do not write to the history database"),
	     NULL},
	    {"filter",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_STRING,
	     &helper->filter_device,
	     /* TRANSLATORS: command line option */
	     N_("Filter with a set of device flags using a ~ prefix to "
		"exclude, e.g. 'internal,~needs-reboot'"),
	     NULL},
	    {"filter-release",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_STRING,
	     &helper->filter_release,
	     /* TRANSLATORS: command line option */
	     N_("Filter with a set of release flags using a ~ prefix to "
		"exclude, e.g. 'trusted-release,~trusted-metadata'"),
	     NULL},
	    {"filter-protocol",
	     '\0',
	     G_OPTION_FLAG_IN_MAIN,
	     G_OPTION_ARG_STRING_ARRAY,
	     &helper->filter_protocols,
	     /* TRANSLATORS: command line option */
	     N_("Filter to specific protocols, e.g. '~org.uefi.capsule' or 'org.nvmexpress'"),
	     NULL},
	    {NULL}};
	g_option_group_add_entries(group, entries);
	return TRUE;
}

static gboolean
fu_cli_group_post_parse_cb(GOptionContext *context,
			   GOptionGroup *group,
			   gpointer data,
			   GError **error)
{
	FuCliGroupHelper *helper = (FuCliGroupHelper *)data;
	FuCli *self = FU_CLI(helper->self);
	FuCliPrivate *priv = GET_PRIVATE(self);

	/* convert to flags */
	if (helper->as_json)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON);
	if (helper->disable_ssl_strict)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_DISABLE_SSL_STRICT);
	if (helper->assume_yes)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES);
	if (helper->show_all)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_SHOW_ALL);
	if (helper->no_remote_check)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_REMOTE_CHECK);
	if (helper->no_metadata_check)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_METADATA_CHECK);
	if (helper->no_reboot_check)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_REBOOT_CHECK);
	if (helper->no_unreported_check)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_UNREPORTED_CHECK);
	if (helper->no_safety_check)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_SAFETY_CHECK);
	if (helper->no_device_prompt)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_DEVICE_PROMPT);
	if (helper->no_security_fix)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_SECURITY_FIX);
	if (helper->sign)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_SIGN);
	if (helper->only_emulated)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_ONLY_EMULATED);
	if (helper->no_history)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_HISTORY);
	if (helper->is_interactive)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_IS_INTERACTIVE);
	if (helper->force)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_FORCE);
	if (helper->no_authenticate)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_AUTHENTICATE);
	if (helper->only_p2p)
		priv->download_flags |= FWUPD_CLIENT_DOWNLOAD_FLAG_ONLY_P2P;
	if (helper->version)
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_VERSION);
	if (helper->download_retries > 0)
		fwupd_client_download_set_retries(priv->client, helper->download_retries);

	/* parse filter flags */
	if (helper->filter_device != NULL) {
		if (!fu_cli_parse_filter_device_flags(self, helper->filter_device, error)) {
			g_autofree gchar *str =
			    /* TRANSLATORS: the user didn't read the man page, %1 is '--filter' */
			    g_strdup_printf(_("Failed to parse flags for %s"), "--filter");
			g_prefix_error(error, "%s: ", str);
			return EXIT_FAILURE;
		}
	}
	if (helper->filter_release != NULL) {
		if (!fu_cli_parse_filter_release_flags(self, helper->filter_release, error)) {
			g_autofree gchar *str =
			    /* TRANSLATORS: the user didn't read the man page,
			     * %1 is '--filter-release' */
			    g_strdup_printf(_("Failed to parse flags for %s"), "--filter-release");
			g_prefix_error(error, "%s: ", str);
			return FALSE;
		}
	}
	if (helper->filter_protocols != NULL) {
		if (!fu_cli_parse_filter_protocol_flags(self, helper->filter_protocols, error)) {
			g_autofree gchar *str =
			    /* TRANSLATORS: the user didn't read the man page,
			     * %1 is '--filter-release' */
			    g_strdup_printf(_("Failed to parse flags for %s"), "--filter-protocol");
			g_prefix_error(error, "%s: ", str);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

GOptionGroup *
fu_cli_get_option_group(FuCli *self)
{
	FuCliGroupHelper *helper = g_new0(FuCliGroupHelper, 1);
	GOptionGroup *group;

	helper->self = g_object_ref(self);
	group = g_option_group_new("self",
				   /* TRANSLATORS: for the --verbose arg */
				   _("CLI Options"),
				   /* TRANSLATORS: for the --verbose arg */
				   _("Show CLI options"),
				   helper,
				   (GDestroyNotify)fu_cli_group_helper_free);
	g_option_group_set_parse_hooks(group,
				       fu_cli_group_pre_parse_cb,
				       fu_cli_group_post_parse_cb);
	return group;
}

typedef struct {
	gchar *name;
	gchar *arguments;
	gchar *description;
	FuCliCmdFlags flags;
	FuCliCmdFunc callback;
} FuCliCmd;

static void
fu_cli_cmd_free(FuCliCmd *item)
{
	g_free(item->name);
	g_free(item->arguments);
	g_free(item->description);
	g_free(item);
}

static gint
fu_cli_cmd_sort_cb(FuCliCmd **item1, FuCliCmd **item2)
{
	return g_strcmp0((*item1)->name, (*item2)->name);
}

void
fu_cli_cmd_array_sort(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_ptr_array_sort(priv->cmd_array, (GCompareFunc)fu_cli_cmd_sort_cb);
}

void
fu_cli_cmd_array_add(FuCli *self,
		     const gchar *name,
		     const gchar *arguments,
		     const gchar *description,
		     FuCliCmdFunc callback)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_auto(GStrv) names = NULL;

	g_return_if_fail(name != NULL);
	g_return_if_fail(description != NULL);
	g_return_if_fail(callback != NULL);

	/* search for existing command */
	for (guint i = 0; i < priv->cmd_array->len; i++) {
		FuCliCmd *item = g_ptr_array_index(priv->cmd_array, i);
		if (g_strcmp0(name, item->name) == 0) {
			g_warning("already added %s, overriding implementation...", name);
			g_ptr_array_remove_index(priv->cmd_array, i);
			break;
		}
	}

	/* add each one */
	names = g_strsplit(name, ",", -1);
	for (guint i = 0; names[i] != NULL; i++) {
		FuCliCmd *item = g_new0(FuCliCmd, 1);
		item->name = g_strdup(names[i]);
		if (i == 0) {
			item->description = g_strdup(description);
		} else {
			/* TRANSLATORS: this is a command alias, e.g. 'get-devices' */
			item->description = g_strdup_printf(_("Alias to %s"), names[0]);
			item->flags |= FU_CLI_CMD_FLAG_IS_ALIAS;
		}
		item->arguments = g_strdup(arguments);
		item->callback = callback;
		g_ptr_array_add(priv->cmd_array, item);
	}
}

gboolean
fu_cli_cmd_array_run(FuCli *self, const gchar *command, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_auto(GStrv) values_copy = g_new0(gchar *, g_strv_length(values) + 1);

	/* clear out bash completion sentinel */
	for (guint i = 0; values[i] != NULL; i++) {
		if (g_strcmp0(values[i], "{") == 0) /* nocheck:depth */
			break;
		values_copy[i] = g_strdup(values[i]);
	}

	/* return all possible actions */
	if (g_strcmp0(command, "get-actions") == 0) {
		for (guint i = 0; i < priv->cmd_array->len; i++) {
			FuCliCmd *item = g_ptr_array_index(priv->cmd_array, i);
			if (item->flags & FU_CLI_CMD_FLAG_IS_ALIAS)
				continue;
			g_print("%s\n", item->name); /* nocheck:print */
		}
		return TRUE;
	}

	/* find command */
	for (guint i = 0; i < priv->cmd_array->len; i++) {
		FuCliCmd *item = g_ptr_array_index(priv->cmd_array, i);
		if (g_strcmp0(item->name, command) == 0)
			return item->callback(self, values_copy, error);
	}

	/* not found */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_ARGS,
			    /* TRANSLATORS: error message */
			    _("Command not found"));
	return FALSE;
}

gchar *
fu_cli_cmd_array_to_string(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	gsize len;
	const gsize max_len = 35;
	GString *string = g_string_new(NULL);

	/* print each command */
	for (guint i = 0; i < priv->cmd_array->len; i++) {
		FuCliCmd *item = g_ptr_array_index(priv->cmd_array, i);
		g_string_append(string, "  ");
		g_string_append(string, item->name);
		len = fu_strwidth(item->name) + 2;
		if (item->arguments != NULL) {
			g_string_append(string, " ");
			g_string_append(string, item->arguments);
			len += fu_strwidth(item->arguments) + 1;
		}
		if (len < max_len) {
			for (gsize j = len; j < max_len + 1; j++)
				g_string_append_c(string, ' ');
			g_string_append(string, item->description);
			g_string_append_c(string, '\n');
		} else {
			g_string_append_c(string, '\n');
			for (gsize j = 0; j < max_len + 1; j++)
				g_string_append_c(string, ' ');
			g_string_append(string, item->description);
			g_string_append_c(string, '\n');
		}
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size(string, string->len - 1);

	return g_string_free(string, FALSE);
}

static void
fu_cli_print_error_as_json(FuCli *self, const GError *error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();
	fwupd_json_object_add_string(json_obj_tmp, "Domain", g_quark_to_string(error->domain));
	fwupd_json_object_add_integer(json_obj_tmp, "Code", error->code);
	fwupd_json_object_add_string(json_obj_tmp, "Message", error->message);
	fwupd_json_object_add_object(json_obj, "Error", json_obj_tmp);
	fu_cli_print_json_object(priv->console, json_obj);
}

void
fu_cli_print_error(FuCli *self, const GError *error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_cli_print_error_as_json(self, error);
		return;
	}
	fu_console_print_full(priv->console, FU_CONSOLE_PRINT_FLAG_STDERR, "%s\n", error->message);
}

FwupdInstallFlags
fu_cli_get_install_flags(FuCli *self)
{
	FwupdInstallFlags install_flags = 0;

	/* set flags */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ALLOW_REINSTALL))
		install_flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ALLOW_OLDER))
		install_flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ALLOW_BRANCH_SWITCH))
		install_flags |= FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ONLY_EMULATED))
		install_flags |= FWUPD_INSTALL_FLAG_ONLY_EMULATED;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_IGNORE_REQUIREMENTS))
		install_flags |= FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_FORCE)) {
		install_flags |= FWUPD_INSTALL_FLAG_FORCE;
		install_flags |= FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS;
	}
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_HISTORY))
		install_flags |= FWUPD_INSTALL_FLAG_NO_HISTORY;

	return install_flags;
}

static gboolean
fu_cli_device_has_any_protocol(FwupdDevice *device, GPtrArray *protocols)
{
	for (guint i = 0; i < protocols->len; i++) {
		const gchar *protocol = g_ptr_array_index(protocols, i);
		if (fwupd_device_has_protocol(device, protocol))
			return TRUE;
	}
	return FALSE;
}

gboolean
fu_cli_device_match_flags(FuCli *self, FwupdDevice *device)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return fwupd_device_match_flags(device,
					priv->filter_device_include,
					priv->filter_device_exclude);
}

gboolean
fu_cli_release_match_flags(FuCli *self, FwupdRelease *release)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return fwupd_release_match_flags(release,
					 priv->filter_release_include,
					 priv->filter_release_exclude);
}

gboolean
fu_cli_device_match_protocol(FuCli *self, FwupdDevice *device)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (priv->filter_protocols_exclude->len > 0 &&
	    fu_cli_device_has_any_protocol(device, priv->filter_protocols_exclude))
		return FALSE;
	if (priv->filter_protocols_include->len > 0 &&
	    !fu_cli_device_has_any_protocol(device, priv->filter_protocols_include))
		return FALSE;

	/* success */
	return TRUE;
}

GPtrArray *
fu_cli_device_array_filter(FuCli *self, GPtrArray *devices, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) devices1 = NULL;
	g_autoptr(GPtrArray) devices2 = NULL;

	devices1 = fwupd_device_array_filter_flags(devices,
						   priv->filter_device_include,
						   priv->filter_device_exclude,
						   error);
	if (devices1 == NULL)
		return NULL;
	if (priv->filter_protocols_include->len > 0 || priv->filter_protocols_exclude->len > 0) {
		devices2 = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
		for (guint i = 0; i < devices1->len; i++) {
			FwupdDevice *device = g_ptr_array_index(devices1, i);
			if (!fu_cli_device_match_protocol(self, device))
				continue;
			g_ptr_array_add(devices2, g_object_ref(device));
		}
		if (devices2->len == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOTHING_TO_DO,
					    "no devices");
			return NULL;
		}
	} else {
		devices2 = g_ptr_array_ref(devices1);
	}
	return g_steal_pointer(&devices2);
}
void
fu_cli_add_filter_device_include(FuCli *self, FwupdDeviceFlags device_flag)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	priv->filter_device_include |= device_flag;
}

void
fu_cli_add_filter_device_exclude(FuCli *self, FwupdDeviceFlags device_flag)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	priv->filter_device_exclude |= device_flag;
}

GPtrArray *
fu_cli_release_array_filter_flags(FuCli *self, GPtrArray *rels, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	return fwupd_release_array_filter_flags(rels,
						priv->filter_release_include,
						priv->filter_release_exclude,
						error);
}

static void
fu_cli_show_plugin_warnings(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FwupdPluginFlags flags = FWUPD_PLUGIN_FLAG_NONE;
	g_autoptr(GPtrArray) plugins = NULL;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return;

	/* get plugins from daemon, ignoring if the daemon is too old */
	plugins = fwupd_client_get_plugins(priv->client, priv->cancellable, NULL);
	if (plugins == NULL)
		return;

	/* get a superset so we do not show the same message more than once */
	for (guint i = 0; i < plugins->len; i++) {
		FwupdPlugin *plugin = g_ptr_array_index(plugins, i);
		if (fwupd_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_DISABLED))
			continue;
		if (!fwupd_plugin_has_flag(plugin, FWUPD_PLUGIN_FLAG_USER_WARNING))
			continue;
		flags |= fwupd_plugin_get_flags(plugin);
	}

	/* never show these, they're way too generic */
	flags &= ~FWUPD_PLUGIN_FLAG_DISABLED;
	flags &= ~FWUPD_PLUGIN_FLAG_NO_HARDWARE;
	flags &= ~FWUPD_PLUGIN_FLAG_REQUIRE_HWID;
	flags &= ~FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY;
	flags &= ~FWUPD_PLUGIN_FLAG_READY;

	/* print */
	for (guint i = 0; i < 64; i++) {
		FwupdPluginFlags flag = (guint64)1 << i;
		g_autofree gchar *tmp = NULL;
		g_autofree gchar *url = NULL;
		g_autoptr(GString) str = g_string_new(NULL);
		if ((flags & flag) == 0)
			continue;
		tmp = fu_cli_plugin_flag_to_string(flag);
		if (tmp == NULL)
			continue;
		g_string_append_printf(str, "%s\n", tmp);
		url = g_strdup_printf("https://github.com/fwupd/fwupd/wiki/PluginFlag:%s",
				      fwupd_plugin_flag_to_string(flag));
		/* TRANSLATORS: %s is a link to a website */
		g_string_append_printf(str, _("See %s for more information."), url);
		fu_console_print_full(priv->console,
				      FU_CONSOLE_PRINT_FLAG_WARNING,
				      "%s\n",
				      str->str);
	}
}

static GHashTable *
fu_cli_bios_settings_parse_argv(gchar **input, GError **error)
{
	GHashTable *bios_settings;

	/* json input */
	if (g_strv_length(input) == 1) {
		g_autofree gchar *data = NULL;
		g_autoptr(FuBiosSettings) new_bios_settings = fu_bios_settings_new(NULL);
		if (!g_file_get_contents(input[0], &data, NULL, error))
			return NULL;
		if (!fwupd_codec_from_json_string(FWUPD_CODEC(new_bios_settings), data, error))
			return NULL;
		return fu_bios_settings_to_hash_kv(new_bios_settings);
	}

	if (g_strv_length(input) == 0 || g_strv_length(input) % 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    /* TRANSLATORS: error message */
				    _("Invalid arguments"));
		return NULL;
	}

	bios_settings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	for (guint i = 0; i < g_strv_length(input); i += 2)
		g_hash_table_insert(bios_settings, g_strdup(input[i]), g_strdup(input[i + 1]));

	return bios_settings;
}

static gboolean
fu_cli_set_bios_setting(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GHashTable) settings = NULL;

	settings = fu_cli_bios_settings_parse_argv(values, error);
	if (settings == NULL)
		return FALSE;
	if (!fwupd_client_modify_bios_setting(priv->client, settings, priv->cancellable, error)) {
		g_prefix_error_literal(error, "failed to set BIOS setting: ");
		return FALSE;
	}

	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		gpointer key;
		gpointer value;
		GHashTableIter iter;

		g_hash_table_iter_init(&iter, settings);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			g_autofree gchar *msg =
			    /* TRANSLATORS: Configured a BIOS setting to a value */
			    g_strdup_printf(_("Set BIOS setting '%s' using '%s'."),
					    (const gchar *)key,
					    (const gchar *)value);
			fu_console_print_literal(priv->console, msg);
		}
	}
	priv->completion_flags |= FWUPD_DEVICE_FLAG_NEEDS_REBOOT;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_REBOOT_CHECK)) {
		g_debug("skipping reboot check");
		return TRUE;
	}

	return fu_cli_prompt_complete(self, TRUE, error);
}

static const gchar *
fu_cli_bios_setting_kind_to_string(FwupdBiosSettingKind kind)
{
	if (kind == FWUPD_BIOS_SETTING_KIND_ENUMERATION) {
		/* TRANSLATORS: The BIOS setting can only be changed to fixed values */
		return _("Enumeration");
	}
	if (kind == FWUPD_BIOS_SETTING_KIND_INTEGER) {
		/* TRANSLATORS: The BIOS setting only accepts integers in a fixed range */
		return _("Integer");
	}
	if (kind == FWUPD_BIOS_SETTING_KIND_STRING) {
		/* TRANSLATORS: The BIOS setting accepts strings */
		return _("String");
	}
	return NULL;
}

static void
fu_cli_bios_setting_update_description(FwupdBiosSetting *setting)
{
	const gchar *tmp = fwupd_bios_setting_get_description(setting);
	const gchar *new = NULL;

	/* try to look it up from translations */
	if (tmp == NULL)
		return;
	new = dgettext(GETTEXT_PACKAGE, tmp);
	if (new != NULL)
		fwupd_bios_setting_set_description(setting, new);
}

static gchar *
fu_cli_bios_setting_to_string(FwupdBiosSetting *setting, guint idt)
{
	const gchar *tmp;
	FwupdBiosSettingKind type;
	g_autofree gchar *debug_str = NULL;
	g_autofree gchar *current_value = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	debug_str = fwupd_codec_to_string(FWUPD_CODEC(setting));
	g_debug("%s", debug_str);
	tmp = fwupd_bios_setting_get_name(setting);
	fwupd_codec_string_append(str, idt, tmp, "");

	type = fwupd_bios_setting_get_kind(setting);
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: type of BIOS setting */
				  _("Setting type"),
				  fu_cli_bios_setting_kind_to_string(type));

	tmp = fwupd_bios_setting_get_current_value(setting);
	if (tmp != NULL) {
		current_value = g_strdup(tmp);
	} else {
		/* TRANSLATORS: tell a user how to get information */
		current_value = g_strdup_printf(_("Run without '%s' to see"), "--no-authenticate");
	}
	/* TRANSLATORS: current value of a BIOS setting */
	fwupd_codec_string_append(str, idt + 1, _("Current Value"), current_value);

	fu_cli_bios_setting_update_description(setting);
	fwupd_codec_string_append(str,
				  idt + 1,
				  /* TRANSLATORS: description of BIOS setting */
				  _("Description"),
				  fwupd_bios_setting_get_description(setting));

	tmp = fwupd_bios_setting_get_appstream_id(setting);
	if (tmp != NULL) {
		/* TRANSLATORS: vendor-neutral AppStream identifier of a BIOS setting */
		fwupd_codec_string_append(str, idt + 1, _("AppStream ID"), tmp);
	}

	tmp = fwupd_bios_setting_get_icon(setting);
	if (tmp != NULL) {
		/* TRANSLATORS: icon name for a BIOS setting */
		fwupd_codec_string_append(str, idt + 1, _("Icon"), tmp);
	}

	if (fwupd_bios_setting_get_read_only(setting)) {
		/* TRANSLATORS: item is TRUE */
		tmp = _("True");
	} else {
		/* TRANSLATORS: item is FALSE */
		tmp = _("False");
	}
	/* TRANSLATORS: BIOS setting is read only */
	fwupd_codec_string_append(str, idt + 1, _("Read Only"), tmp);

	if (type == FWUPD_BIOS_SETTING_KIND_INTEGER || type == FWUPD_BIOS_SETTING_KIND_STRING) {
		g_autofree gchar *lower =
		    g_strdup_printf("%" G_GUINT64_FORMAT,
				    fwupd_bios_setting_get_lower_bound(setting));
		g_autofree gchar *upper =
		    g_strdup_printf("%" G_GUINT64_FORMAT,
				    fwupd_bios_setting_get_upper_bound(setting));
		if (type == FWUPD_BIOS_SETTING_KIND_INTEGER) {
			g_autofree gchar *scalar =
			    g_strdup_printf("%" G_GUINT64_FORMAT,
					    fwupd_bios_setting_get_scalar_increment(setting));
			/* TRANSLATORS: Lowest valid integer for BIOS setting */
			fwupd_codec_string_append(str, idt + 1, _("Minimum value"), lower);
			/* TRANSLATORS: Highest valid integer for BIOS setting */
			fwupd_codec_string_append(str, idt + 1, _("Maximum value"), upper);
			fwupd_codec_string_append(
			    str,
			    idt + 1,
			    /* TRANSLATORS: Scalar increment for integer BIOS setting */
			    _("Scalar Increment"),
			    scalar);
		} else {
			/* TRANSLATORS: Shortest valid string for BIOS setting */
			fwupd_codec_string_append(str, idt + 1, _("Minimum length"), lower);
			/* TRANSLATORS: Longest valid string for BIOS setting */
			fwupd_codec_string_append(str, idt + 1, _("Maximum length"), upper);
		}
	} else if (type == FWUPD_BIOS_SETTING_KIND_ENUMERATION) {
		GPtrArray *values = fwupd_bios_setting_get_possible_values(setting);
		if (values != NULL && values->len > 0) {
			/* TRANSLATORS: Possible values for a bios setting */
			fwupd_codec_string_append(str, idt + 1, _("Possible Values"), NULL);
			for (guint i = 0; i < values->len; i++) {
				const gchar *possible = g_ptr_array_index(values, i);
				g_autofree gchar *index = g_strdup_printf("%u", i);
				fwupd_codec_string_append(str, idt + 2, index, possible);
			}
		}
	}
	return g_string_free(g_steal_pointer(&str), FALSE);
}

static gboolean
fu_cli_bios_setting_matches_args(FwupdBiosSetting *setting, gchar **values)
{
	/* no arguments set */
	if (g_strv_length(values) == 0)
		return TRUE;

	/* check all arguments */
	for (guint j = 0; j < g_strv_length(values); j++) {
		if (g_strcmp0(fwupd_bios_setting_get_name(setting), values[j]) == 0)
			return TRUE;
		if (g_strcmp0(fwupd_bios_setting_get_id(setting), values[j]) == 0)
			return TRUE;
		if (g_strcmp0(fwupd_bios_setting_get_appstream_id(setting), values[j]) == 0)
			return TRUE;
	}
	return FALSE;
}

static void
fu_cli_bios_setting_console_print(FuConsole *console, gchar **values, GPtrArray *settings)
{
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	for (guint i = 0; i < settings->len; i++) {
		FwupdBiosSetting *setting = g_ptr_array_index(settings, i);
		if (fu_cli_bios_setting_matches_args(setting, values)) {
			g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();
			fu_cli_bios_setting_update_description(setting);
			fwupd_codec_to_json(FWUPD_CODEC(setting),
					    json_obj_tmp,
					    FWUPD_CODEC_FLAG_NONE);
			fwupd_json_array_add_object(json_arr, json_obj_tmp);
		}
	}
	fwupd_json_object_add_array(json_obj, "BiosSettings", json_arr);
	fu_cli_print_json_object(console, json_obj);
}

static gboolean
fu_cli_get_bios_setting(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) attrs = NULL;
	gboolean found = FALSE;

	attrs = fwupd_client_get_bios_settings(priv->client, priv->cancellable, error);
	if (attrs == NULL)
		return FALSE;
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_cli_bios_setting_console_print(priv->console, values, attrs);
		return TRUE;
	}

	for (guint i = 0; i < attrs->len; i++) {
		FwupdBiosSetting *attr = g_ptr_array_index(attrs, i);
		if (fu_cli_bios_setting_matches_args(attr, values)) {
			g_autofree gchar *tmp = fu_cli_bios_setting_to_string(attr, 0);
			fu_console_print_literal(priv->console, tmp);
			found = TRUE;
		}
	}
	if (attrs->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    /* TRANSLATORS: error message */
				    _("This system doesn't support firmware settings"));
		return FALSE;
	}
	if (!found) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    /* TRANSLATORS: error message */
				    _("Unable to find attribute"));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_cli_security_fix(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
#ifndef HAVE_HSI
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    /* TRANSLATORS: error message for unsupported feature */
			    _("Host Security ID (HSI) is not supported"));
	return FALSE;
#endif /* HAVE_HSI */

	if (g_strv_length(values) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    /* TRANSLATOR: This is the error message for
				     * incorrect parameter */
				    _("Invalid arguments, expected an AppStream ID"));
		return FALSE;
	}
	if (!fwupd_client_fix_host_security_attr(priv->client, values[0], priv->cancellable, error))
		return FALSE;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	/* TRANSLATORS: we've fixed a security problem on the machine */
	fu_console_print_literal(priv->console, _("Fixed successfully"));
	return TRUE;
}

static void
fu_cli_hwids_as_json(FuCli *self, GStrv hwids_keys, GStrv hwids_values)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	for (guint i = 0; hwids_keys[i] != NULL; i++)
		fwupd_json_object_add_string(json_obj, hwids_keys[i], hwids_values[i]);
	fu_cli_print_json_object(priv->console, json_obj);
}

static gboolean
fu_cli_hwids(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_auto(GStrv) hwids_keys = NULL;
	g_auto(GStrv) hwids_values = NULL;

	fwupd_client_get_hwids(priv->client, &hwids_keys, &hwids_values);
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_cli_hwids_as_json(self, hwids_keys, hwids_values);
		return TRUE;
	}

	/* show debug output */
	fu_console_print_literal(priv->console, "Computer Information");
	fu_console_print_literal(priv->console, "--------------------");
	for (guint i = 0; hwids_keys[i] != NULL; i++) {
		if (fwupd_guid_is_valid(hwids_values[i]))
			continue;
		fu_console_print(priv->console, "%s: %s", hwids_keys[i], hwids_values[i]);
	}

	/* show GUIDs */
	fu_console_print_literal(priv->console, "Hardware IDs");
	fu_console_print_literal(priv->console, "------------");
	for (guint i = 0; hwids_keys[i] != NULL; i++) {
		g_autofree gchar *hwids_keys_real = NULL;
		g_auto(GStrv) hwids_keys_strv = NULL;
		if (!fwupd_guid_is_valid(hwids_values[i]))
			continue;
		hwids_keys_strv = g_strsplit(hwids_keys[i], "&", -1);
		hwids_keys_real = g_strjoinv(" + ", hwids_keys_strv);
		fu_console_print(priv->console, "{%s}   <- %s", hwids_values[i], hwids_keys_real);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cli_report_devices(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *data = NULL;
	g_autofree gchar *report_uri = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GHashTable) metadata = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* we only know how to upload to the LVFS */
	remote = fwupd_client_get_remote_by_id(priv->client, "lvfs", priv->cancellable, error);
	if (remote == NULL)
		return FALSE;
	report_uri = fwupd_remote_build_report_uri(remote, error);
	if (report_uri == NULL)
		return FALSE;

	/* include all the devices */
	devices = fwupd_client_get_devices(priv->client, priv->cancellable, error);
	if (devices == NULL)
		return FALSE;
	metadata = fwupd_client_get_report_metadata(priv->client, priv->cancellable, error);
	if (metadata == NULL)
		return FALSE;
	data = fwupd_client_build_report_devices(priv->client, devices, metadata, error);
	if (data == NULL)
		return FALSE;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_ARGS,
					    "pass --yes to enable uploads");
			return FALSE;
		}
	} else {
		/* show the user the entire data blob */
		fu_console_print_kv(priv->console, _("Target"), report_uri);
		fu_console_print_kv(priv->console, _("Payload"), data);
		fu_console_print(
		    priv->console,
		    /* TRANSLATORS: explain why we want to upload */
		    _("Uploading a device list allows the %s team to know what hardware "
		      "exists, and allows us to put pressure on vendors that do not upload "
		      "firmware updates for their hardware."),
		    fwupd_remote_get_title(remote));
		if (!fu_console_input_bool(priv->console,
					   TRUE,
					   "%s (%s)",
					   /* TRANSLATORS: ask the user to upload */
					   _("Upload data now?"),
					   /* TRANSLATORS: metadata is downloaded */
					   _("Requires internet connection"))) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOTHING_TO_DO,
					    "Declined upload");
			return FALSE;
		}
	}

	/* send to the LVFS */
	uri = fwupd_client_upload_report(priv->client,
					 report_uri,
					 data,
					 NULL,
					 FWUPD_CLIENT_UPLOAD_FLAG_ALWAYS_MULTIPART,
					 priv->cancellable,
					 error);
	if (uri == NULL)
		return FALSE;

	/* success */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_console_print_literal(priv->console,
					 /* TRANSLATORS: success, so say thank you to the user */
					 _("Device list uploaded successfully, thanks!"));
	}

	return TRUE;
}

static gboolean
fu_cli_security_undo(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
#ifndef HAVE_HSI
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    /* TRANSLATORS: error message for unsupported feature */
			    _("Host Security ID (HSI) is not supported"));
	return FALSE;
#endif /* HAVE_HSI */

	if (g_strv_length(values) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    /* TRANSLATOR: This is the error message for
				     * incorrect parameter */
				    _("Invalid arguments, expected an AppStream ID"));
		return FALSE;
	}
	if (!fwupd_client_undo_host_security_attr(priv->client,
						  values[0],
						  priv->cancellable,
						  error))
		return FALSE;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return TRUE;

	/* TRANSLATORS: we've fixed a security problem on the machine */
	fu_console_print_literal(priv->console, _("Fix reverted successfully"));
	return TRUE;
}

static gboolean
fu_cli_emulation_tag(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdDevice) dev = NULL;

	/* set the flag */
	fu_cli_add_filter_device_include(self, FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	dev = fu_cli_get_device_or_prompt(self, values, error);
	if (dev == NULL)
		return FALSE;
	return fwupd_client_modify_device(priv->client,
					  fwupd_device_get_id(dev),
					  "Flags",
					  "emulation-tag",
					  priv->cancellable,
					  error);
}

static gboolean
fu_cli_emulation_untag(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdDevice) dev = NULL;

	/* set the flag */
	fu_cli_add_filter_device_include(self, FWUPD_DEVICE_FLAG_EMULATION_TAG);
	dev = fu_cli_get_device_or_prompt(self, values, error);
	if (dev == NULL)
		return FALSE;
	return fwupd_client_modify_device(priv->client,
					  fwupd_device_get_id(dev),
					  "Flags",
					  "~emulation-tag",
					  priv->cancellable,
					  error);
}

static gboolean
fu_cli_emulation_save(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);

	/* check args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments, expected FILENAME");
		return FALSE;
	}

	/* file already exists */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_FORCE) &&
	    g_file_test(values[0], G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Filename already exists");
		return FALSE;
	}

	/* save */
	return fwupd_client_emulation_save(priv->client, values[0], priv->cancellable, error);
}

static gboolean
fu_cli_emulation_load(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);

	/* check args */
	if (g_strv_length(values) != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    "Invalid arguments, expected FILENAME");
		return FALSE;
	}
	return fwupd_client_emulation_load(priv->client, values[0], priv->cancellable, error);
}

static gboolean
fu_cli_enable_remote_auth(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	const gchar *remote_id = "lvfs-embargo";
	g_autoptr(FwupdRemote) remote = NULL;
	g_autofree gchar *username = NULL;
	g_autofree gchar *password = NULL;
	g_autoptr(GError) error_local = NULL;

	/* find remote */
	if (g_strv_length(values) > 0)
		remote_id = values[0];
	remote = fwupd_client_get_remote_by_id(priv->client, remote_id, priv->cancellable, error);
	if (remote == NULL)
		return FALSE;

	/* get username */
	if (g_strv_length(values) > 1) {
		username = g_strdup(values[1]);
	} else {
		username = fu_console_input_string(priv->console,
						   /* TRANSLATORS: remote refers to a website
						      distributing fw, %1 is a name e.g. 'lvfs' */
						   _("Enter your email address for remote '%s'"),
						   remote_id);
		if (username == NULL || username[0] == '\0') {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_ARGS,
					    "A username is required");
			return FALSE;
		}
	}

	/* get password */
	if (g_strv_length(values) > 2) {
		password = g_strdup(values[2]);
	} else {
		if (g_strcmp0(remote_id, "lvfs-embargo") == 0) {
			const gchar *uri = "https://fwupd.org/lvfs/profile";
			fu_console_print(
			    priv->console,
			    /* TRANSLATORS: token refers to a password thing; %1 is a URL */
			    _("To get a new token please go to %s and click 'Generate Token'"),
			    uri);
		}
		password = fu_console_input_string(priv->console,
						   /* TRANSLATORS: remote refers to a website
						      distributing fw, %1 is a name e.g. 'lvfs' */
						   _("Enter your token for remote '%s'"),
						   remote_id);
		if (password == NULL || password[0] == '\0') {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_ARGS,
					    "A password is required");
			return FALSE;
		}
	}

	/* check this works */
	fwupd_remote_set_username(remote, username);
	fwupd_remote_set_password(remote, password);
	if (!fwupd_client_refresh_remote(priv->client,
					 remote,
					 priv->download_flags,
					 priv->cancellable,
					 &error_local)) {
		if (!fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED) &&
		    !g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		g_debug("ignoring: %s", error_local->message);
	}

	/* save secrets and enable remote */
	if (!fu_cli_modify_remote_warning(self,
					  remote,
					  fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_ASSUME_YES),
					  error))
		return FALSE;
	if (!fwupd_remote_save_user_secrets(remote, error))
		return FALSE;
	if (!fwupd_client_modify_remote(priv->client,
					fwupd_remote_get_id(remote),
					"Enabled",
					"true",
					priv->cancellable,
					error))
		return FALSE;
	if (!fwupd_client_refresh_remote(priv->client,
					 remote,
					 priv->download_flags,
					 priv->cancellable,
					 error))
		return FALSE;

	/* TRANSLATORS: we've gained access to a new firmware remote */
	fu_console_print_literal(priv->console, _("Remote enabled and refreshed successfully"));
	return TRUE;
}

static gchar *
fu_cli_parse_project_dependency(const gchar *str, FuCliDependencyKind *dependency_kind)
{
	g_return_val_if_fail(str != NULL, NULL);
	if (g_str_has_prefix(str, "RuntimeVersion(")) {
		gsize strsz = strlen(str);
		if (dependency_kind != NULL)
			*dependency_kind = FU_CLI_DEPENDENCY_KIND_RUNTIME;
		return g_strndup(str + 15, strsz - 16);
	}
	if (g_str_has_prefix(str, "CompileVersion(")) {
		gsize strsz = strlen(str);
		if (dependency_kind != NULL)
			*dependency_kind = FU_CLI_DEPENDENCY_KIND_COMPILE;
		return g_strndup(str + 15, strsz - 16);
	}
	return g_strdup(str);
}

static gboolean
fu_cli_print_version_key_valid(const gchar *key)
{
	g_return_val_if_fail(key != NULL, FALSE);
	if (g_str_has_prefix(key, "RuntimeVersion"))
		return TRUE;
	if (g_str_has_prefix(key, "CompileVersion"))
		return TRUE;
	return FALSE;
}

static void
fu_cli_project_versions_as_json(FuCli *self, GHashTable *metadata)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	GHashTableIter iter;
	const gchar *key;
	const gchar *value;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	g_hash_table_iter_init(&iter, metadata);
	while (g_hash_table_iter_next(&iter, (gpointer *)&key, (gpointer *)&value)) {
		FuCliDependencyKind dependency_kind = FU_CLI_DEPENDENCY_KIND_UNKNOWN;
		g_autofree gchar *project = NULL;
		g_autoptr(FwupdJsonObject) json_obj_tmp = fwupd_json_object_new();

		/* add version keys */
		if (!fu_cli_print_version_key_valid(key))
			continue;
		project = fu_cli_parse_project_dependency(key, &dependency_kind);
		if (dependency_kind != FU_CLI_DEPENDENCY_KIND_UNKNOWN) {
			fwupd_json_object_add_string(
			    json_obj_tmp,
			    "Type",
			    fu_cli_dependency_kind_to_string(dependency_kind));
		}
		fwupd_json_object_add_string(json_obj_tmp, "AppstreamId", project);
		fwupd_json_object_add_string(json_obj_tmp, "Version", value);
		fwupd_json_array_add_object(json_arr, json_obj_tmp);
	}
	fwupd_json_object_add_array(json_obj, "Versions", json_arr);
	fu_cli_print_json_object(priv->console, json_obj);
}

static gboolean
fu_cli_version(FuCli *self, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	GHashTableIter iter;
	const gchar *key;
	const gchar *value;
	g_autoptr(GHashTable) metadata = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* get metadata */
	metadata = fwupd_client_get_report_metadata(priv->client, priv->cancellable, error);
	if (metadata == NULL)
		return FALSE;

	/* dump to the screen in the most appropriate format */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		fu_cli_project_versions_as_json(self, metadata);
		return TRUE;
	}

	g_hash_table_iter_init(&iter, metadata);
	while (g_hash_table_iter_next(&iter, (gpointer *)&key, (gpointer *)&value)) {
		FuCliDependencyKind dependency_kind = FU_CLI_DEPENDENCY_KIND_UNKNOWN;
		g_autofree gchar *project = NULL;

		/* print version keys */
		if (!fu_cli_print_version_key_valid(key))
			continue;
		project = fu_cli_parse_project_dependency(key, &dependency_kind);
		g_string_append_printf(str,
				       "%-10s%-30s%s\n",
				       fu_cli_dependency_kind_to_string(dependency_kind),
				       project,
				       value);
	}
	fu_console_print_literal(priv->console, str->str);
	return TRUE;
}

static gboolean
fu_cli_setup_interactive(FuCli *self, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "using --json");
		return FALSE;
	}
	return fu_console_setup(priv->console, error);
}

static void
fu_cli_cancelled_cb(GCancellable *cancellable, gpointer user_data)
{
	FuCli *self = FU_CLI(user_data);
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (!g_main_loop_is_running(priv->loop))
		return;
	/* TRANSLATORS: this is from ctrl+c */
	fu_console_print_literal(priv->console, _("Cancelled"));
	g_main_loop_quit(priv->loop);
}

static void
fu_cli_device_added_cb(FwupdClient *client, FwupdDevice *device, gpointer user_data)
{
	FuCli *self = FU_CLI(user_data);
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *tmp = NULL;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return;

	tmp = fu_cli_device_to_string(priv->client, device, 0);

	/* TRANSLATORS: this is when a device is hotplugged */
	fu_console_print(priv->console, "%s\n%s", _("Device added:"), tmp);
}

static void
fu_cli_device_removed_cb(FwupdClient *client, FwupdDevice *device, gpointer user_data)
{
	FuCli *self = FU_CLI(user_data);
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *tmp = NULL;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return;

	tmp = fu_cli_device_to_string(priv->client, device, 0);

	/* TRANSLATORS: this is when a device is hotplugged */
	fu_console_print(priv->console, "%s\n%s", _("Device removed:"), tmp);
}

static void
fu_cli_device_changed_cb(FwupdClient *client, FwupdDevice *device, gpointer user_data)
{
	FuCli *self = FU_CLI(user_data);
	FuCliPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *tmp = NULL;

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return;

	tmp = fu_cli_device_to_string(priv->client, device, 0);

	/* TRANSLATORS: this is when a device has been updated */
	fu_console_print(priv->console, "%s\n%s", _("Device changed:"), tmp);
}

static void
fu_cli_changed_cb(FwupdClient *client, gpointer user_data)
{
	FuCli *self = FU_CLI(user_data);
	FuCliPrivate *priv = GET_PRIVATE(self);

	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON))
		return;

	/* TRANSLATORS: this is when the daemon state changes */
	fu_console_print_literal(priv->console, _("Changed"));
}

static gboolean
fu_cli_monitor(FuCli *self, gchar **values, GError **error)
{
	FuCliPrivate *priv = GET_PRIVATE(self);

	/* watch for any hotplugged device */
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "changed",
			 G_CALLBACK(fu_cli_changed_cb),
			 self);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "device-added",
			 G_CALLBACK(fu_cli_device_added_cb),
			 self);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "device-removed",
			 G_CALLBACK(fu_cli_device_removed_cb),
			 self);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "device-changed",
			 G_CALLBACK(fu_cli_device_changed_cb),
			 self);
	g_signal_connect(G_CANCELLABLE(priv->cancellable),
			 "cancelled",
			 G_CALLBACK(fu_cli_cancelled_cb),
			 self);
	fu_cli_watch_sigint_start(self);
	fu_cli_loop_run(self);
	return TRUE;
}

void
fu_cli_cmd_array_add_common(FuCli *self)
{
	fu_cli_cmd_array_add(self,
			     "get-plugins",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Get all enabled plugins registered with the system"),
			     fu_cli_cmd_get_plugins);
	fu_cli_cmd_array_add(self,
			     "check-reboot-needed",
			     _("[DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Check if any devices are pending a reboot to complete update"),
			     fu_cli_check_reboot_needed);
	fu_cli_cmd_array_add(self,
			     "get-devices,get-topology",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Get all devices that support firmware updates"),
			     fu_cli_get_devices);
	fu_cli_cmd_array_add(self,
			     "get-history",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Show history of firmware updates"),
			     fu_cli_get_history);
	fu_cli_cmd_array_add(self,
			     "report-history",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Share firmware history with the developers"),
			     fu_cli_report_history);
	fu_cli_cmd_array_add(self,
			     "report-export",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Export firmware history for manual upload"),
			     fu_cli_report_export);
	fu_cli_cmd_array_add(self,
			     "install",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID] [VERSION]"),
			     /* TRANSLATORS: command description */
			     _("Install a specific firmware file on all devices that match"),
			     fu_cli_install);
	fu_cli_cmd_array_add(self,
			     "local-install",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILE [DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Install a firmware file in cabinet format on this hardware"),
			     fu_cli_local_install);
	fu_cli_cmd_array_add(self,
			     "get-details",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILE"),
			     /* TRANSLATORS: command description */
			     _("Gets details about a firmware file"),
			     fu_cli_get_details);
	fu_cli_cmd_array_add(self,
			     "get-updates,get-upgrades",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Gets the list of updates for all specified devices, or all "
			       "devices if unspecified"),
			     fu_cli_get_updates);
	fu_cli_cmd_array_add(self,
			     "update,upgrade",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Updates all specified devices to latest firmware version, or all "
			       "devices if unspecified"),
			     fu_cli_update);
	fu_cli_cmd_array_add(self,
			     "verify",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Checks cryptographic hash matches firmware"),
			     fu_cli_verify);
	fu_cli_cmd_array_add(self,
			     "unlock",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("DEVICE-ID|GUID"),
			     /* TRANSLATORS: command description */
			     _("Unlocks the device for firmware access"),
			     fu_cli_unlock);
	fu_cli_cmd_array_add(self,
			     "clear-results",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("DEVICE-ID|GUID"),
			     /* TRANSLATORS: command description */
			     _("Clears the results from the last update"),
			     fu_cli_clear_results);
	fu_cli_cmd_array_add(self,
			     "get-results",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("DEVICE-ID|GUID"),
			     /* TRANSLATORS: command description */
			     _("Gets the results from the last update"),
			     fu_cli_get_results);
	fu_cli_cmd_array_add(self,
			     "get-releases",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Gets the releases for a device"),
			     fu_cli_get_releases);
	fu_cli_cmd_array_add(self,
			     "search",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("WORD"),
			     /* TRANSLATORS: command description */
			     _("Finds firmware releases from the metadata"),
			     fu_cli_search);
	fu_cli_cmd_array_add(self,
			     "get-remotes",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Gets the configured remotes"),
			     fu_cli_get_remotes);
	fu_cli_cmd_array_add(self,
			     "downgrade",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Downgrades the firmware on a device"),
			     fu_cli_downgrade);
	fu_cli_cmd_array_add(self,
			     "refresh",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[FILE FILE_SIG REMOTE-ID]"),
			     /* TRANSLATORS: command description */
			     _("Refresh metadata from remote server"),
			     fu_cli_refresh);
	fu_cli_cmd_array_add(self,
			     "verify-update",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Update the stored cryptographic hash with current ROM contents"),
			     fu_cli_verify_update);
	fu_cli_cmd_array_add(self,
			     "modify-remote",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("REMOTE-ID KEY VALUE"),
			     /* TRANSLATORS: command description */
			     _("Modifies a given remote"),
			     fu_cli_remote_modify);
	fu_cli_cmd_array_add(self,
			     "enable-remote",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("REMOTE-ID"),
			     /* TRANSLATORS: command description */
			     _("Enables a given remote"),
			     fu_cli_remote_enable);
	fu_cli_cmd_array_add(self,
			     "clean-remote",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("REMOTE-ID"),
			     /* TRANSLATORS: command description */
			     _("Cleans a given remote"),
			     fu_cli_remote_clean);
	fu_cli_cmd_array_add(self,
			     "disable-remote",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("REMOTE-ID"),
			     /* TRANSLATORS: command description */
			     _("Disables a given remote"),
			     fu_cli_remote_disable);
	fu_cli_cmd_array_add(self,
			     "enable-remote-auth",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[REMOTE-ID] [USERNAME] [TOKEN]"),
			     /* TRANSLATORS: command description */
			     _("Enable an authenticated remote"),
			     fu_cli_enable_remote_auth);
	fu_cli_cmd_array_add(self,
			     "activate",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Activate devices"),
			     fu_cli_activate);
	fu_cli_cmd_array_add(self,
			     "get-approved-firmware",
			     NULL,
			     /* TRANSLATORS: firmware approved by the admin */
			     _("Gets the list of approved firmware"),
			     fu_cli_get_approved_firmware);
	fu_cli_cmd_array_add(self,
			     "set-approved-firmware",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME|CHECKSUM1[,CHECKSUM2][,CHECKSUM3]"),
			     /* TRANSLATORS: firmware approved by the admin */
			     _("Sets the list of approved firmware"),
			     fu_cli_set_approved_firmware);
	fu_cli_cmd_array_add(self,
			     "modify-config",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[SECTION] KEY VALUE"),
			     /* TRANSLATORS: sets something in the daemon configuration file */
			     _("Modifies a daemon configuration value"),
			     fu_cli_modify_config);
	fu_cli_cmd_array_add(self,
			     "reset-config",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("SECTION"),
			     /* TRANSLATORS: sets something in the daemon configuration file */
			     _("Resets a daemon configuration section"),
			     fu_cli_reset_config);
	fu_cli_cmd_array_add(self,
			     "reinstall",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Reinstall current firmware on the device"),
			     fu_cli_reinstall);
	fu_cli_cmd_array_add(self,
			     "switch-branch",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID] [BRANCH]"),
			     /* TRANSLATORS: command description */
			     _("Switch the firmware branch on the device"),
			     fu_cli_switch_branch);
	fu_cli_cmd_array_add(self,
			     "security",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Gets the host security attributes"),
			     fu_cli_security);
	fu_cli_cmd_array_add(self,
			     "sync,sync-bkc",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Sync firmware versions to the chosen configuration"),
			     fu_cli_sync);
	fu_cli_cmd_array_add(self,
			     "download",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("LOCATION"),
			     /* TRANSLATORS: command description */
			     _("Download a file"),
			     fu_cli_download);
	fu_cli_cmd_array_add(self,
			     "device-test",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[FILENAME1] [FILENAME2]"),
			     /* TRANSLATORS: command description */
			     _("Test a device using a JSON manifest"),
			     fu_cli_device_test);
	fu_cli_cmd_array_add(self,
			     "device-emulate",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[FILENAME1] [FILENAME2]"),
			     /* TRANSLATORS: command description */
			     _("Emulate a device using a JSON manifest"),
			     fu_cli_device_emulate);
	fu_cli_cmd_array_add(self,
			     "inhibit",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[REASON] [TIMEOUT]"),
			     /* TRANSLATORS: command description */
			     _("Inhibit the system to prevent upgrades"),
			     fu_cli_inhibit);
	fu_cli_cmd_array_add(self,
			     "uninhibit",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("INHIBIT-ID"),
			     /* TRANSLATORS: command description */
			     _("Uninhibit the system to allow upgrades"),
			     fu_cli_uninhibit);
	fu_cli_cmd_array_add(self,
			     "device-wait",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("GUID|DEVICE-ID"),
			     /* TRANSLATORS: command description */
			     _("Wait for a device to appear"),
			     fu_cli_device_wait);
	fu_cli_cmd_array_add(self,
			     "quit",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Asks the daemon to quit"),
			     fu_cli_quit);
	fu_cli_cmd_array_add(
	    self,
	    "get-bios-settings,get-bios-setting",
	    /* TRANSLATORS: command argument: uppercase, spaces->dashes */
	    _("[SETTING1] [SETTING2] [--no-authenticate]"),
	    /* TRANSLATORS: command description */
	    _("Retrieve BIOS settings.  If no arguments are passed all settings are returned"),
	    fu_cli_get_bios_setting);
	fu_cli_cmd_array_add(self,
			     "set-bios-setting",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("SETTING1 VALUE1 [SETTING2] [VALUE2]"),
			     /* TRANSLATORS: command description */
			     _("Sets one or more BIOS settings"),
			     fu_cli_set_bios_setting);
	fu_cli_cmd_array_add(self,
			     "emulation-load",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME"),
			     /* TRANSLATORS: command description */
			     _("Load device emulation data"),
			     fu_cli_emulation_load);
	fu_cli_cmd_array_add(self,
			     "emulation-save",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("FILENAME"),
			     /* TRANSLATORS: command description */
			     _("Save device emulation data"),
			     fu_cli_emulation_save);
	fu_cli_cmd_array_add(self,
			     "emulation-tag",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Adds devices to watch for future emulation"),
			     fu_cli_emulation_tag);
	fu_cli_cmd_array_add(self,
			     "emulation-untag",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[DEVICE-ID|GUID]"),
			     /* TRANSLATORS: command description */
			     _("Removes devices to watch for future emulation"),
			     fu_cli_emulation_untag);
	fu_cli_cmd_array_add(self,
			     "security-fix",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[APPSTREAM_ID]"),
			     /* TRANSLATORS: command description */
			     _("Fix a specific host security attribute"),
			     fu_cli_security_fix);
	fu_cli_cmd_array_add(self,
			     "security-undo",
			     /* TRANSLATORS: command argument: uppercase, spaces->dashes */
			     _("[APPSTREAM_ID]"),
			     /* TRANSLATORS: command description */
			     _("Undo the host security attribute fix"),
			     fu_cli_security_undo);
	fu_cli_cmd_array_add(self,
			     "report-devices",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Upload the list of updatable devices to a remote server"),
			     fu_cli_report_devices);
	fu_cli_cmd_array_add(self,
			     "hwids",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Return all the hardware IDs for the machine"),
			     fu_cli_hwids);
	fu_cli_cmd_array_add(self,
			     "monitor",
			     NULL,
			     /* TRANSLATORS: command description */
			     _("Monitor the daemon for events"),
			     fu_cli_monitor);

	/* sort by command name */
	fu_cli_cmd_array_sort(self);
}

gint
fu_cli_main(FuCli *self, gint argc, gchar **argv)
{
	FuCliPrivate *priv = GET_PRIVATE(self);
	FwupdFeatureFlags feature_flags =
	    FWUPD_FEATURE_FLAG_CAN_REPORT | FWUPD_FEATURE_FLAG_SWITCH_BRANCH |
	    FWUPD_FEATURE_FLAG_FDE_WARNING | FWUPD_FEATURE_FLAG_COMMUNITY_TEXT |
	    FWUPD_FEATURE_FLAG_SHOW_PROBLEMS;
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error_console = NULL;
	g_autoptr(GDateTime) dt_now = g_date_time_new_now_utc();

	/* non-TTY consoles cannot answer questions */
	if (!fu_cli_setup_interactive(self, &error_console)) {
		g_info("failed to initialize interactive console: %s", error_console->message);
		fu_console_set_interactive(priv->console, FALSE);
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_UNREPORTED_CHECK);
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_METADATA_CHECK);
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_REBOOT_CHECK);
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_SAFETY_CHECK);
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_REMOTE_CHECK);
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_DEVICE_PROMPT);
		fu_cli_add_arg_flag(self, FU_CLI_ARG_FLAG_NO_SECURITY_FIX);
	}

	/* allow disabling SSL strict mode for broken corporate proxies */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_DISABLE_SSL_STRICT)) {
		fu_console_print_full(priv->console,
				      FU_CONSOLE_PRINT_FLAG_WARNING,
				      "%s\n",
				      /* TRANSLATORS: try to help */
				      _("Ignoring SSL strict checks, "
					"to do this automatically in the future "
					"export DISABLE_SSL_STRICT in your environment"));
		(void)g_setenv("DISABLE_SSL_STRICT", "1", TRUE);
	}

	/* this doesn't have to be precise (e.g. using the build-year) as we just
	 * want to check the clock is not set to the default of 1970-01-01... */
	if (g_date_time_get_year(dt_now) < 2021) {
		fu_console_print_full(
		    priv->console,
		    FU_CONSOLE_PRINT_FLAG_WARNING,
		    "%s\n",
		    /* TRANSLATORS: try to help */
		    _("The system clock has not been set correctly and downloading "
		      "files may fail."));
	}

	/* set up ctrl+c */
	fu_cli_watch_sigint_start(self);

	/* show a warning if the daemon is tainted */
	if (!fwupd_client_connect(priv->client, priv->cancellable, &error)) {
#ifdef _WIN32
		fu_console_print_literal(
		    priv->console,
		    /* TRANSLATORS: error message for Windows */
		    _("Failed to connect to Windows service, please ensure it's running."));
		g_debug("%s", error->message);
#else
		/* TRANSLATORS: could not contact the fwupd service over D-Bus */
		g_prefix_error(&error, "%s: ", _("Failed to connect to daemon"));
		fu_cli_print_error(self, error);
#endif
		return EXIT_FAILURE;
	}

	if (fwupd_client_get_tainted(priv->client)) {
		fu_console_print_full(priv->console,
				      FU_CONSOLE_PRINT_FLAG_WARNING,
				      "%s\n",
				      /* TRANSLATORS: the user is SOL for support... */
				      _("The daemon has loaded 3rd party code and "
					"is no longer supported by the upstream developers!"));
	}

	/* just show versions and exit */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_VERSION)) {
		if (!fu_cli_version(self, &error)) {
			fu_cli_print_error(self, error);
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}

	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON)) {
		/* show user-visible warnings from the plugins */
		fu_cli_show_plugin_warnings(self);

		/* show any unsupported warnings */
		fu_cli_show_unsupported_warning(self);
	}

	/* we know the runtime daemon version now */
	fwupd_client_set_user_agent_for_package(priv->client, g_get_prgname(), PACKAGE_VERSION);

	/* check that we have at least this version daemon running */
	if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_FORCE) &&
	    !fu_cli_check_daemon_version(self, &error)) {
		fu_cli_print_error(self, error);
		return EXIT_FAILURE;
	}

	/* send our implemented feature set */
	if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_IS_INTERACTIVE)) {
		feature_flags |=
		    FWUPD_FEATURE_FLAG_REQUESTS | FWUPD_FEATURE_FLAG_REQUESTS_NON_GENERIC |
		    FWUPD_FEATURE_FLAG_UPDATE_ACTION | FWUPD_FEATURE_FLAG_DETACH_ACTION;
		if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_NO_AUTHENTICATE))
			feature_flags |= FWUPD_FEATURE_FLAG_ALLOW_AUTHENTICATION;
	}
	if (!fwupd_client_set_feature_flags(priv->client,
					    feature_flags,
					    priv->cancellable,
					    &error)) {
		/* TRANSLATORS: a feature is something like "can show an image" */
		g_prefix_error(&error, "%s: ", _("Failed to set front-end features"));
		fu_cli_print_error(self, error);
		return EXIT_FAILURE;
	}

	/* run the specified command */
	if (!fu_cli_cmd_array_run(self, argv[1], &argv[2], &error)) {
#ifdef SUPPORTED_BUILD
		/* sanity check */
		if (error == NULL) {
			g_critical("exec failed but no error set!");
			return EXIT_FAILURE;
		}
#endif
		fu_cli_print_error(self, error);
		if (!fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_AS_JSON) &&
		    g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_ARGS)) {
			g_autofree gchar *cmd = g_strdup_printf("%s --help", g_get_prgname());
			g_autoptr(GString) str = g_string_new("\n");
			/* TRANSLATORS: explain how to get help,
			 * where $1 is something like 'fwupdmgr --help' */
			g_string_append_printf(str, _("Use %s for help"), cmd);
			fu_console_print_literal(priv->console, str->str);
		} else if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
			return EXIT_NOTHING_TO_DO;
		} else if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_REACHABLE)) {
			return EXIT_NOT_REACHABLE;
		} else if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			return EXIT_NOT_FOUND;
		}
#ifdef HAVE_GETUID
		/* if not root, then notify users on the error path */
		if (fu_cli_has_arg_flag(self, FU_CLI_ARG_FLAG_IS_INTERACTIVE) &&
		    (getuid() != 0 || geteuid() != 0)) {
			fu_console_print_full(fu_cli_get_console(FU_CLI(self)),
					      FU_CONSOLE_PRINT_FLAG_STDERR |
						  FU_CONSOLE_PRINT_FLAG_WARNING,
					      "%s\n",
					      /* TRANSLATORS: we're poking around as a power user */
					      _("This program may only work correctly as root"));
		}
#endif
		return EXIT_FAILURE;
	}

	/* success */
	return EXIT_SUCCESS;
}

static void
fu_cli_init(FuCli *self)
{
	FuCliPrivate *priv = GET_PRIVATE(self);

	/* connect to the daemon */
	priv->client = fwupd_client_new();
	priv->main_ctx = g_main_context_new();
	fwupd_client_set_main_context(priv->client, priv->main_ctx);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "notify::percentage",
			 G_CALLBACK(fu_cli_client_notify_cb),
			 self);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "notify::status",
			 G_CALLBACK(fu_cli_client_notify_cb),
			 self);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "device-changed",
			 G_CALLBACK(fu_cli_update_device_changed_cb),
			 self);
	g_signal_connect(FWUPD_CLIENT(priv->client),
			 "device-request",
			 G_CALLBACK(fu_cli_update_device_request_cb),
			 self);
	priv->cancellable = g_cancellable_new();
	g_signal_connect(G_CANCELLABLE(priv->cancellable),
			 "cancelled",
			 G_CALLBACK(fu_cli_cancelled_cb),
			 self);
	priv->loop = g_main_loop_new(priv->main_ctx, FALSE);
	priv->post_requests = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	priv->cmd_array = g_ptr_array_new_with_free_func((GDestroyNotify)fu_cli_cmd_free);
	priv->console = fu_console_new();
	fu_console_set_main_context(priv->console, priv->main_ctx);
	priv->filter_protocols_include = g_ptr_array_new_with_free_func(g_free);
	priv->filter_protocols_exclude = g_ptr_array_new_with_free_func(g_free);
}

static void
fu_cli_finalize(GObject *obj)
{
	FuCli *self = FU_CLI(obj);
	FuCliPrivate *priv = GET_PRIVATE(self);
	if (priv->client != NULL) {
		/* when destroying GDBusProxy in a custom GMainContext, the context must be
		 * iterated enough after finalization of the proxies that any pending D-Bus traffic
		 * can be freed */
		fwupd_client_disconnect(priv->client, NULL);
		while (g_main_context_iteration(priv->main_ctx, FALSE)) {
			/* nothing needs to be done here */
		};
		g_object_unref(priv->client);
	}
	if (priv->current_device != NULL)
		g_object_unref(priv->current_device);
	if (priv->source_sigint != NULL)
		g_source_destroy(priv->source_sigint);
	g_ptr_array_unref(priv->post_requests);
	g_main_loop_unref(priv->loop);
	g_main_context_unref(priv->main_ctx);
	g_object_unref(priv->cancellable);
	g_ptr_array_unref(priv->filter_protocols_include);
	g_ptr_array_unref(priv->filter_protocols_exclude);
	g_object_unref(priv->console);
	g_ptr_array_unref(priv->cmd_array);
	G_OBJECT_CLASS(fu_cli_parent_class)->finalize(obj);
}

static void
fu_cli_class_init(FuCliClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_cli_finalize;
}
