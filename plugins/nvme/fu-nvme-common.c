/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-nvme-common.h"

const gchar *
fu_nvme_status_to_string (guint32 status)
{
	switch (status) {
	case NVME_SC_SUCCESS:
		return "Command completed successfully";
	case NVME_SC_INVALID_OPCODE:
		return "Associated command opcode field is not valid";
	case NVME_SC_INVALID_FIELD:
		return "Unsupported value in a defined field";
	case NVME_SC_CMDID_CONFLICT:
		return "Command identifier is already in use";
	case NVME_SC_DATA_XFER_ERROR:
		return "Error while trying to transfer the data or metadata";
	case NVME_SC_POWER_LOSS:
		return "Command aborted due to power loss notification";
	case NVME_SC_INTERNAL:
		return "Internal error";
	case NVME_SC_ABORT_REQ:
		return "Command Abort request";
	case NVME_SC_ABORT_QUEUE:
		return "Delete I/O Submission Queue request";
	case NVME_SC_FUSED_FAIL:
		return "Other command in a fused operation failing";
	case NVME_SC_FUSED_MISSING:
		return "Missing Fused Command";
	case NVME_SC_INVALID_NS:
		return "Namespace or the format of that namespace is invalid";
	case NVME_SC_CMD_SEQ_ERROR:
		return "Protocol violation in a multicommand sequence";
	case NVME_SC_SANITIZE_FAILED:
		return "No recovery actions has been successfully completed";
	case NVME_SC_SANITIZE_IN_PROGRESS:
		return "A sanitize operation is in progress";
	case NVME_SC_LBA_RANGE:
		return "LBA exceeds the size of the namespace";
	case NVME_SC_NS_WRITE_PROTECTED:
		return "Namespace is write protected by the host";
	case NVME_SC_CAP_EXCEEDED:
		return "Capacity of the namespace to be exceeded";
	case NVME_SC_NS_NOT_READY:
		return "Namespace is not ready to be accessed";
	case NVME_SC_RESERVATION_CONFLICT:
		return "Conflict with a reservation on the accessed namespace";
	case NVME_SC_CQ_INVALID:
		return "Completion Queue does not exist";
	case NVME_SC_QID_INVALID:
		return "Invalid queue identifier specified";
	case NVME_SC_QUEUE_SIZE:
		return "Invalid queue size";
	case NVME_SC_ABORT_LIMIT:
		return "Outstanding Abort commands has exceeded the limit";
	case NVME_SC_ABORT_MISSING:
		return "Abort command is missing";
	case NVME_SC_ASYNC_LIMIT:
		return "Outstanding Async commands has been exceeded";
	case NVME_SC_FIRMWARE_SLOT:
		return "Slot is invalid or read only";
	case NVME_SC_FIRMWARE_IMAGE:
		return "Image specified for activation is invalid";
	case NVME_SC_INVALID_VECTOR:
		return "Creation failed due to an invalid interrupt vector";
	case NVME_SC_INVALID_LOG_PAGE:
		return "Log page indicated is invalid";
	case NVME_SC_INVALID_FORMAT:
		return "LBA Format specified is not supported";
	case NVME_SC_FW_NEEDS_CONV_RESET:
		return "commit was successful, but activation requires reset";
	case NVME_SC_INVALID_QUEUE:
		return "Failed to delete the I/O Completion Queue specified";
	case NVME_SC_FEATURE_NOT_SAVEABLE:
		return "Feature Identifier does not support a saveable value";
	case NVME_SC_FEATURE_NOT_CHANGEABLE:
		return "Feature Identifier is not able to be changed";
	case NVME_SC_FEATURE_NOT_PER_NS:
		return "Feature Identifier specified is not namespace specific";
	case NVME_SC_FW_NEEDS_SUBSYS_RESET:
		return "Commit was successful, activation requires NVM Subsystem";
	case NVME_SC_FW_NEEDS_RESET:
		return "Commit was successful, activation requires a reset";
	case NVME_SC_FW_NEEDS_MAX_TIME:
		return "Would exceed the Maximum Time for Firmware Activation";
	case NVME_SC_FW_ACIVATE_PROHIBITED:
		return "Image specified is being prohibited from activation";
	case NVME_SC_OVERLAPPING_RANGE:
		return "Image has overlapping ranges";
	case NVME_SC_NS_INSUFFICENT_CAP:
		return "Requires more free space than is currently available";
	case NVME_SC_NS_ID_UNAVAILABLE:
		return "Number of namespaces supported has been exceeded";
	case NVME_SC_NS_ALREADY_ATTACHED:
		return "Controller is already attached to the namespace";
	case NVME_SC_NS_IS_PRIVATE:
		return "Namespace is private";
	case NVME_SC_NS_NOT_ATTACHED:
		return "Controller is not attached to the namespace";
	case NVME_SC_THIN_PROV_NOT_SUPP:
		return "Thin provisioning is not supported by the controller";
	case NVME_SC_CTRL_LIST_INVALID:
		return "Controller list provided is invalid";
	case NVME_SC_BP_WRITE_PROHIBITED:
		return "Trying to modify a Boot Partition while it is locked";
	case NVME_SC_BAD_ATTRIBUTES:
		return "Bad attributes";
	case NVME_SC_WRITE_FAULT:
		return "Write data could not be committed to the media";
	case NVME_SC_READ_ERROR:
		return "Read data could not be recovered from the media";
	case NVME_SC_GUARD_CHECK:
		return "End-to-end guard check failure";
	case NVME_SC_APPTAG_CHECK:
		return "End-to-end application tag check failure";
	case NVME_SC_REFTAG_CHECK:
		return "End-to-end reference tag check failure";
	case NVME_SC_COMPARE_FAILED:
		return "Miscompare during a Compare command";
	case NVME_SC_ACCESS_DENIED:
		return "Access denied";
	case NVME_SC_UNWRITTEN_BLOCK:
		return "Read from an LBA range containing a unwritten block";
	case NVME_SC_ANA_PERSISTENT_LOSS:
		return "Namespace is in the ANA Persistent Loss state";
	case NVME_SC_ANA_INACCESSIBLE:
		return "Namespace being in the ANA Inaccessible state";
	case NVME_SC_ANA_TRANSITION:
		return "Namespace transitioning between Async Access states";
	default:
		return "Unknown";
	}
}
