/*
 * Copyright 2026 NVIDIA Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * FuDevice subclass for OOB-managed firmware components;
 * each instance binds a Redfish FirmwareInventory URI to an fwupd
 * device, with GUIDs derived from SoftwareId or @odata.id path
 */

#include "config.h"

#include "fu-nvidia-oob-device.h"
#include "fu-nvidia-oob-plugin.h"

struct _FuNvidiaOobDevice {
	FuDevice parent_instance;
	FuNvidiaOobRedfishClient *client; /* borrowed from plugin */
	gchar *inventory_uri;
	gchar *software_id;
	gboolean needs_ac_cycle; /* BMC told us this component requires AC */
};

G_DEFINE_TYPE(FuNvidiaOobDevice, fu_nvidia_oob_device, FU_TYPE_DEVICE)

/* helpers */

static void
fu_nvidia_oob_device_populate_from_inventory(FuNvidiaOobDevice *self, FwupdJsonObject *obj)
{
	FuDevice *device = FU_DEVICE(self);
	const gchar *name = NULL;
	const gchar *version = NULL;
	const gchar *manufacturer = NULL;
	g_autofree gchar *inventory_id = NULL;

	if (fwupd_json_object_has_node(obj, "Name"))
		name = fwupd_json_object_get_string(obj, "Name", NULL);
	if (fwupd_json_object_has_node(obj, "Version"))
		version = fwupd_json_object_get_string(obj, "Version", NULL);
	if (fwupd_json_object_has_node(obj, "Manufacturer"))
		manufacturer = fwupd_json_object_get_string(obj, "Manufacturer", NULL);

	if (fwupd_json_object_has_node(obj, "SoftwareId"))
		self->software_id = g_strdup(fwupd_json_object_get_string(obj, "SoftwareId", NULL));

	/* every OOB-managed component on the GB300 activates on AC power
	 * cycle, but we deliberately do NOT set FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN
	 * (or NEEDS_REBOOT) -- both flags would drive fwupdmgr to prompt the
	 * user, and the prompt's accept action calls logind PowerOff/Reboot,
	 * neither of which actually AC-cycles a server (the aux rail stays
	 * alive); attach() handles activation directly via the BMC's OEM
	 * AuxPowerReset action; on failure, the helper script
	 * /usr/local/bin/nvidia-oob-aux-cycle is the documented manual
	 * fallback, and surfacing a misleading prompt would only push users
	 * toward the wrong action */
	/* use the inventory ID (last URI path segment, e.g. FW_BMC_0,
	 * FW_GPU_0, FW_CPU_0) as the device name -- the BMC's JSON "Name"
	 * field is a generic human label ("Software Inventory") and the
	 * BMC returns it for every entry in FirmwareInventory, so all
	 * 12 OOB-managed devices rendered with the same name in
	 * fwupdmgr get-devices and were only distinguishable by GUID;
	 * the inventory ID is unique per component and matches the entries
	 * in the LVFS bringup guide's FirmwareInventory table, which is the
	 * label an operator naturally reaches for when picking a device
	 * to update; JSON Name is kept as a fallback for the unlikely
	 * case where we couldn't parse a usable basename out of the URI */
	inventory_id = self->inventory_uri != NULL ? g_path_get_basename(self->inventory_uri)
						   : NULL;
	if (inventory_id != NULL && inventory_id[0] != '\0' && g_strcmp0(inventory_id, ".") != 0 &&
	    g_strcmp0(inventory_id, "/") != 0) {
		fu_device_set_name(device, inventory_id);
	} else if (name != NULL && name[0] != '\0') {
		fu_device_set_name(device, name);
	}
	fu_device_set_summary(device, "OOB-managed firmware (activates on AC cycle)");
	self->needs_ac_cycle = TRUE;

	if (version != NULL)
		fu_device_set_version(device, version);
	if (manufacturer != NULL)
		fu_device_set_vendor(device, manufacturer);
	else
		fu_device_set_vendor(device, "NVIDIA");

	/* declare protocol & update state per LVFS convention */
	fu_device_add_protocol(device, "org.dmtf.redfish");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);

	/* tell fwupdmgr how long an OOB update typically takes so it computes
	 * a realistic ETA in its progress display -- without this, fwupdmgr
	 * uses a generic short default and shows a misleading "Less than one
	 * minute remaining" almost immediately, even though the BMC's PLDM
	 * verification + per-component fan-out runs ~10 minutes for a full
	 * bundle; picked 900 s (15 min) -- a small overestimate is far less
	 * confusing than a small underestimate, and the BMC may legitimately
	 * take longer when more components change */
	fu_device_set_install_duration(device, 900);

	/* identity / GUIDs:
	 * two complementary identities per device --
	 *   NVIDIA_OOB\URI_<inventory_uri>  (always emitted, unique per entry)
	 *   NVIDIA_OOB\SWID_<software_id>  (emitted when BMC reports a SoftwareId)
	 * the NvidiaOob\Name= instance ID is the quirk-file matcher hook */
	if (self->inventory_uri != NULL && self->inventory_uri[0] != '\0') {
		g_autofree gchar *iid_uri =
		    g_strdup_printf("NVIDIA_OOB\\URI_%s", self->inventory_uri);
		fu_device_add_instance_id(device, iid_uri);
	}
	if (self->software_id != NULL && self->software_id[0] != '\0') {
		g_autofree gchar *iid_swid =
		    g_strdup_printf("NVIDIA_OOB\\SWID_%s", self->software_id);
		fu_device_add_instance_id(device, iid_swid);
	}
	if (name != NULL) {
		g_autofree gchar *iid_name = g_strdup_printf("NvidiaOob\\Name=%s", name);
		fu_device_add_instance_id(device, iid_name);
	}
}

/* vfuncs */

static gboolean
fu_nvidia_oob_device_setup(FuDevice *device, GError **error)
{
	FuNvidiaOobDevice *self = FU_NVIDIA_OOB_DEVICE(device);

	/* refresh version on each setup so fwupdmgr get-updates sees
	 * the latest state (e.g. after a partial in-flight activation) */
	g_autoptr(FwupdJsonNode) node =
	    fu_nvidia_oob_redfish_client_get(self->client, self->inventory_uri, error);
	g_autoptr(FwupdJsonObject) obj = NULL;
	if (node == NULL)
		return FALSE;
	obj = fwupd_json_node_get_object(node, error);
	if (obj == NULL)
		return FALSE;
	fu_nvidia_oob_device_populate_from_inventory(self, obj);
	return TRUE;
}

static FuFirmware *
fu_nvidia_oob_device_prepare_firmware(FuDevice *device,
				      GInputStream *stream,
				      FuProgress *progress,
				      FuFirmwareParseFlags flags,
				      GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	gsize stream_size = 0;

	/* the CAB payload for OOB targets is either a signed PLDM bundle
	 * (NVIDIA HPM) or a raw BMC/SBIOS image -- the BMC validates the
	 * signature and PLDM headers -- but we enforce a size upper bound
	 * to catch mis-packaged CABs */
	if (!fu_input_stream_size(stream, &stream_size, error))
		return NULL;
	if (stream_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware payload is empty");
		return NULL;
	}
	if (stream_size > (gsize)(256 * 1024 * 1024)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware payload %" G_GSIZE_FORMAT
			    " bytes exceeds 256 MiB BMC staging limit",
			    stream_size);
		return NULL;
	}
	blob = fu_input_stream_read_bytes(stream, 0, stream_size, progress, error);
	if (blob == NULL)
		return NULL;
	return fu_firmware_new_from_bytes(blob);
}

static gboolean
fu_nvidia_oob_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuNvidiaOobDevice *self = FU_NVIDIA_OOB_DEVICE(device);
	g_autoptr(GBytes) blob = NULL;
	g_autofree gchar *task_uri = NULL;
	FuProgress *child = NULL;
	guint last_percent = 0;
	gboolean polled_ok = FALSE;
	const guint poll_interval_ms = 2000;
	const guint max_attempts = 1800; /* 1h at 2s intervals */

	fu_progress_set_id(progress, G_STRLOC);
	/* the actual upload (~117 MB over USB Redfish) finishes in ~30 s, while
	 * the BMC's PLDM verification + per-component fan-out runs ~10 min;
	 * weighting the upload at 40% and the apply at 60% causes fwupdmgr's
	 * rate-based ETA extrapolation to fire as soon as the upload is done,
	 * showing a misleading "Less than one minute remaining" for the rest
	 * of the (long) BMC-apply phase -- match wall-clock proportions instead:
	 * upload 5%, apply 95%; now the rate-based ETA at end-of-upload is
	 * roughly (5% / 30 s) -> ~10 min for the remaining 95%, which lines
	 * up with the install_duration-based estimate from
	 * populate_from_inventory() */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "upload");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 95, "bmc-apply");

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;

	/* step 1: POST multipart/form-data to UpdateService */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	task_uri = fu_nvidia_oob_redfish_client_multipart_push(self->client,
							       self->inventory_uri,
							       blob,
							       error);
	if (task_uri == NULL)
		return FALSE;
	fu_progress_step_done(progress);

	/* step 2: poll the TaskMonitor until completion -- acquire the
	 * child progress for this step (bmc-apply) so PercentComplete
	 * values feed the right progress bar */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_VERIFY);
	child = fu_progress_get_child(progress);
	for (guint attempt = 0; attempt < max_attempts; attempt++) {
		guint percent = 0;
		g_autofree gchar *msg = NULL;
		g_autoptr(GError) error_local = NULL;
		/* poll_task() may rewrite task_uri in place if the BMC reaps the
		 * Monitor sub-resource and we have to fall back to /Tasks/<id> */
		FuOobTaskState state = fu_nvidia_oob_redfish_client_poll_task(self->client,
									      &task_uri,
									      &percent,
									      &msg,
									      &error_local);
		if (state == FU_OOB_TASK_STATE_UNKNOWN && error_local != NULL) {
			/* if we'd previously polled the task successfully, the BMC
			 * accepted our upload and started the update -- a subsequent
			 * 404 or empty body from BOTH the Monitor and the /Tasks/<id>
			 * fallback means the BMC has reaped the entire task lifecycle
			 * (its documented behaviour after PLDM activation completes);
			 * without an explicit failure signal, treat resource-gone as
			 * success (without this guard, the GB300 BMC's habit of
			 * returning HTTP 200 + empty body after activation surfaced
			 * as "empty body from BMC (Redfish response carried no JSON
			 * payload)" even though the apply / activate phases logged
			 * "Component apply complete" and "Firmware update time: ..."
			 * on the BMC side) */
			gboolean resource_gone =
			    g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
			    g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
			if (polled_ok && resource_gone) {
				g_debug("BMC reaped task resources after successful polling; "
					"treating as Completed");
				fu_progress_set_percentage(child, 100);
				break;
			}
			/* transient network blip during BMC reset -- be forgiving
			 * for the first few attempts but propagate persistent errors */
			if (attempt < 5) {
				g_usleep(poll_interval_ms * 1000);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}

		polled_ok = TRUE;

		/* prevent progress bar regression */
		if (percent > last_percent) {
			fu_progress_set_percentage(child, percent);
			last_percent = percent;
		}

		if (state == FU_OOB_TASK_STATE_COMPLETED) {
			fu_progress_set_percentage(child, 100);
			break;
		}
		if (state == FU_OOB_TASK_STATE_EXCEPTION || state == FU_OOB_TASK_STATE_CANCELLED) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "BMC task %s failed: %s",
				    task_uri,
				    msg != NULL ? msg : "no detail");
			return FALSE;
		}
		g_usleep(poll_interval_ms * 1000);

		if (attempt == max_attempts - 1) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "timed out waiting for BMC task %s after %u seconds",
				    task_uri,
				    max_attempts * poll_interval_ms / 1000);
			return FALSE;
		}
	}
	fu_progress_step_done(progress);
	return TRUE;
}

static gboolean
fu_nvidia_oob_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	/* staged firmware on GB300 OOB components only becomes active after
	 * an AC (aux-rail) power cycle -- use fwupd's canonical "activation
	 * pending" mechanism rather than trying to emit a free-text message:
	 *
	 *   - FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION is the standard signal
	 *     that says "this device is staged but not yet running the new
	 *     firmware"; fwupdmgr renders a clear post-install line about
	 *     activation when it sees this flag
	 *   - update_message is also set as a hint; fwupdmgr prints it
	 *     alongside the activation notice
	 *   - the actual activation work (POST AuxPowerReset to BMC) lives
	 *     in the activate() vfunc below, dispatched by `fwupdmgr
	 *     activate`; that keeps the install/activate phases cleanly
	 *     separated and lets the user choose when to AC-cycle */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	fu_device_set_update_message(
	    device,
	    "An AC power cycle is required to activate the staged firmware:\n"
	    "  1. sudo poweroff\n"
	    "  2. After the host is fully off, physically unplug AC for\n"
	    "     at least 30 seconds\n"
	    "  3. Reconnect AC and power on — staged firmware will be\n"
	    "     active on next boot.\n"
	    "(`fwupdmgr activate` will print the same instructions; the\n"
	    " activation requires a physical AC cycle and cannot be\n"
	    " sequenced automatically from the host.)");
	return TRUE;
}

/* fwupd dispatches plugin->activate() when the user runs
 * `sudo fwupdmgr activate` against a device with NEEDS_ACTIVATION set
 *
 * on the GB300 we cannot actually drive the activation from here:
 *   - the BMC's OEM AuxPowerReset is gated on the chassis being
 *     powered off first (HTTP 400 ChassisPowerStateOffRequired)
 *   - powering the chassis off severs the host's own power, so fwupd
 *     dies along with the host before a follow-up AuxPowerReset POST
 *     can fire
 *   - there is no single BMC action that combines "poweroff + drop
 *     aux" atomically from the host's network endpoint
 *
 * so activate() doesn't try; it returns FWUPD_ERROR_NEEDS_USER_ACTION
 * with a clear, terminal-printable description of the manual procedure;
 * fwupdmgr surfaces this as an error message; the user does the work
 * out of band */
static gboolean
fu_nvidia_oob_device_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NEEDS_USER_ACTION,
			    "Activation cannot be performed automatically on this hardware. "
			    "An AC (aux-rail) power cycle is required and must be done out "
			    "of band:\n"
			    "  1. sudo poweroff\n"
			    "  2. After the host is fully off, physically unplug AC for at "
			    "least 30 seconds\n"
			    "  3. Reconnect AC and power on — staged firmware will be active "
			    "on next boot.\n"
			    "Reason: AC power cycle requires the chassis to be powered off, "
			    "which terminates this process before automatic activation "
			    "can complete.");
	return FALSE;
}

static void
fu_nvidia_oob_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuNvidiaOobDevice *self = FU_NVIDIA_OOB_DEVICE(device);
	fwupd_codec_string_append(str, idt, "PluginVersion", NVIDIA_OOB_PLUGIN_VERSION);
	fwupd_codec_string_append(str, idt, "InventoryURI", self->inventory_uri);
	if (self->software_id != NULL)
		fwupd_codec_string_append(str, idt, "SoftwareId", self->software_id);
	fwupd_codec_string_append_bool(str, idt, "NeedsAcCycle", self->needs_ac_cycle);
}

/* GObject boilerplate */

static void
fu_nvidia_oob_device_finalize(GObject *object)
{
	FuNvidiaOobDevice *self = FU_NVIDIA_OOB_DEVICE(object);
	g_free(self->inventory_uri);
	g_free(self->software_id);
	/* client is borrowed from plugin; do not unref */
	G_OBJECT_CLASS(fu_nvidia_oob_device_parent_class)->finalize(object);
}

static void
fu_nvidia_oob_device_class_init(FuNvidiaOobDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_nvidia_oob_device_finalize;
	device_class->setup = fu_nvidia_oob_device_setup;
	device_class->prepare_firmware = fu_nvidia_oob_device_prepare_firmware;
	device_class->write_firmware = fu_nvidia_oob_device_write_firmware;
	device_class->attach = fu_nvidia_oob_device_attach;
	device_class->activate = fu_nvidia_oob_device_activate;
	device_class->to_string = fu_nvidia_oob_device_to_string;
}

static void
fu_nvidia_oob_device_init(FuNvidiaOobDevice *self)
{
	/* vendor IDs let fwupd verify that LVFS-published CABs are permitted to
	 * target this device -- without these, fwupd refuses to install any
	 * firmware on OOB-managed components with "Update Error: No vendor ID
	 * set"; DMI:NVIDIA pairs with the GB300 system's SMBIOS identity and
	 * with the project's LVFS vendor namespace allowlist; OEM:NVIDIA is the
	 * generic non-bus fallback since OOB-managed components have no
	 * PCI/USB/NVMe identity visible to the host */
	fu_device_add_vendor_id(FU_DEVICE(self), "DMI:NVIDIA");
	fu_device_add_vendor_id(FU_DEVICE(self), "OEM:NVIDIA");
}

FuNvidiaOobDevice *
fu_nvidia_oob_device_new(FuContext *ctx,
			 FuNvidiaOobRedfishClient *client,
			 const gchar *inventory_uri,
			 FwupdJsonObject *inventory_obj)
{
	FuNvidiaOobDevice *self = g_object_new(FU_TYPE_NVIDIA_OOB_DEVICE, "context", ctx, NULL);
	self->client = client; /* borrowed */
	self->inventory_uri = g_strdup(inventory_uri);
	fu_device_set_physical_id(FU_DEVICE(self), inventory_uri);
	fu_device_set_logical_id(FU_DEVICE(self), inventory_uri);
	if (inventory_obj != NULL)
		fu_nvidia_oob_device_populate_from_inventory(self, inventory_obj);
	return self;
}

const gchar *
fu_nvidia_oob_device_get_inventory_uri(FuNvidiaOobDevice *self)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_DEVICE(self), NULL);
	return self->inventory_uri;
}

FuNvidiaOobRedfishClient *
fu_nvidia_oob_device_get_client(FuNvidiaOobDevice *self)
{
	g_return_val_if_fail(FU_IS_NVIDIA_OOB_DEVICE(self), NULL);
	return self->client;
}
