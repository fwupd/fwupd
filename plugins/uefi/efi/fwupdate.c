/*
 * Copyright (C) 2014-2018 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <efi.h>
#include <efilib.h>

#include "fwup-cleanups.h"
#include "fwup-common.h"
#include "fwup-efi.h"
#include "fwup-debug.h"

#define UNUSED __attribute__((__unused__))
#define GNVN_BUF_SIZE			1024
#define FWUP_NUM_CAPSULE_UPDATES_MAX	128

typedef struct {
	CHAR16			*name;
	UINT32			 attrs;
	UINTN			 size;
	FWUP_UPDATE_INFO	*info;
} FWUP_UPDATE_TABLE;

static VOID
fwup_update_table_free(FWUP_UPDATE_TABLE *update)
{
	FreePool(update->info);
	FreePool(update->name);
	FreePool(update);
}

_DEFINE_CLEANUP_FUNCTION0(FWUP_UPDATE_TABLE *, _fwup_update_table_free_p, fwup_update_table_free)
#define _cleanup_update_table __attribute__ ((cleanup(_fwup_update_table_free_p)))

#define SECONDS 1000000

static INTN
fwup_dp_size(EFI_DEVICE_PATH *dp, INTN limit)
{
	INTN ret = 0;
	while (1) {
		if (limit < 4)
			break;
		INTN nodelen = DevicePathNodeLength(dp);
		if (nodelen > limit)
			break;
		limit -= nodelen;
		ret += nodelen;

		if (IsDevicePathEnd(dp))
			return ret;
		dp = NextDevicePathNode(dp);
	}
	return -1;
}

static EFI_STATUS
fwup_populate_update_info(CHAR16 *name, FWUP_UPDATE_TABLE *info_out)
{
	EFI_STATUS rc;
	FWUP_UPDATE_INFO *info = NULL;
	UINTN info_size = 0;
	UINT32 attrs = 0;
	VOID *info_ptr = NULL;

	rc = fwup_get_variable(name, &fwupdate_guid, &info_ptr, &info_size, &attrs);
	if (EFI_ERROR(rc))
		return rc;
	info = (FWUP_UPDATE_INFO *)info_ptr;

	if (info_size < sizeof(*info)) {
		fwup_warning(L"Update '%s' is is too small", name);
		return EFI_INVALID_PARAMETER;
	}

	if (info_size - sizeof(EFI_DEVICE_PATH) <= sizeof(*info)) {
		fwup_warning(L"Update '%s' is malformed, "
			     L"and cannot hold a file path", name);
		return EFI_INVALID_PARAMETER;
	}

	EFI_DEVICE_PATH *hdr = (EFI_DEVICE_PATH *)&info->dp;
	INTN is = EFI_FIELD_OFFSET(FWUP_UPDATE_INFO, dp);
	if (is > (INTN)info_size) {
		fwup_warning(L"Update '%s' has an invalid file path, "
			     L"device path offset is %d, but total size is %d",
			     name, is, info_size);
		return EFI_INVALID_PARAMETER;
	}

	is = info_size - is;
	INTN sz = fwup_dp_size(hdr, info_size);
	if (sz < 0 || is > (INTN)info_size || is != sz) {
		fwup_warning(L"Update '%s' has an invalid file path, "
			     L"update info size: %d dp size: %d size for dp: %d",
			     name, info_size, sz, is);
		return EFI_INVALID_PARAMETER;
	}

	info_out->info = info;
	info_out->size = info_size;
	info_out->attrs = attrs;
	info_out->name = StrDuplicate(name);
	if (info_out->name == NULL) {
		fwup_warning(L"Could not allocate %d", StrSize(name));
		return EFI_OUT_OF_RESOURCES;
	}

	return EFI_SUCCESS;
}

static EFI_STATUS
fwup_populate_update_table(FWUP_UPDATE_TABLE **updates, UINTN *n_updates_out)
{
	EFI_GUID vendor_guid = empty_guid;
	EFI_STATUS rc;
	UINTN n_updates = 0;
	_cleanup_free CHAR16 *variable_name = NULL;

	/* How much do we trust "size of the VariableName buffer" to mean
	 * sizeof(vn) and not sizeof(vn)/sizeof(vn[0]) ? */
	variable_name = fwup_malloc0(GNVN_BUF_SIZE * 2);
	if (variable_name == NULL)
		return EFI_OUT_OF_RESOURCES;

	while (1) {
		UINTN variable_name_size = GNVN_BUF_SIZE;
		rc = uefi_call_wrapper(RT->GetNextVariableName, 3,
				       &variable_name_size, variable_name,
				       &vendor_guid);
		if (rc == EFI_NOT_FOUND)
			break;

		/* ignore any huge names */
		if (rc == EFI_BUFFER_TOO_SMALL)
			continue;
		if (EFI_ERROR(rc)) {
			fwup_warning(L"Could not get variable name: %r", rc);
			return rc;
		}

		/* not one of our state variables */
		if (CompareGuid(&vendor_guid, &fwupdate_guid))
			continue;

		/* ignore debugging settings */
		if (StrCmp(variable_name, L"FWUPDATE_VERBOSE") == 0 ||
		    StrCmp(variable_name, L"FWUPDATE_DEBUG_LOG") == 0)
			continue;

		if (n_updates > FWUP_NUM_CAPSULE_UPDATES_MAX) {
			fwup_warning(L"Ignoring update %s", variable_name);
			continue;
		}

		fwup_info(L"Found update %s", variable_name);
		_cleanup_update_table FWUP_UPDATE_TABLE *update = fwup_malloc0(sizeof(FWUP_UPDATE_TABLE));
		if (update == NULL)
			return EFI_OUT_OF_RESOURCES;
		rc = fwup_populate_update_info(variable_name, update);
		if (EFI_ERROR(rc)) {
			fwup_delete_variable(variable_name, &fwupdate_guid);
			fwup_warning(L"Could not populate update info for '%s'", variable_name);
			return rc;
		}
		if (update->info->status & FWUPDATE_ATTEMPT_UPDATE) {
			fwup_time(&update->info->time_attempted);
			update->info->status = FWUPDATE_ATTEMPTED;
			updates[n_updates++] = _steal_pointer(&update);
		}
	}

	*n_updates_out = n_updates;
	return EFI_SUCCESS;
}

static EFI_STATUS
fwup_search_file(EFI_DEVICE_PATH **file_dp, EFI_FILE_HANDLE *fh)
{
	EFI_DEVICE_PATH *dp, *parent_dp;
	EFI_GUID sfsp = SIMPLE_FILE_SYSTEM_PROTOCOL;
	EFI_GUID dpp = DEVICE_PATH_PROTOCOL;
	UINTN n_handles, count;
	EFI_STATUS rc;
	_cleanup_free EFI_FILE_HANDLE *devices = NULL;

	rc = uefi_call_wrapper(BS->LocateHandleBuffer, 5, ByProtocol, &sfsp,
			       NULL, &n_handles, (EFI_HANDLE **)&devices);
	if (EFI_ERROR(rc)) {
		fwup_warning(L"Could not find handles");
		return rc;
	}

	dp = *file_dp;

	fwup_debug(L"Searching Device Path: %s...", DevicePathToStr(dp));
	parent_dp = DuplicateDevicePath(dp);
	if (parent_dp == NULL)
		return EFI_INVALID_PARAMETER;

	dp = parent_dp;
	count = 0;
	while (1) {
		if (IsDevicePathEnd(dp))
			return EFI_INVALID_PARAMETER;

		if (DevicePathType(dp) == MEDIA_DEVICE_PATH &&
		    DevicePathSubType(dp) == MEDIA_FILEPATH_DP)
			break;

		dp = NextDevicePathNode(dp);
		++count;
	}

	SetDevicePathEndNode(dp);
	fwup_debug(L"Device Path prepared: %s", DevicePathToStr(parent_dp));

	for (UINTN i = 0; i < n_handles; i++) {
		EFI_DEVICE_PATH *path;

		rc = uefi_call_wrapper(BS->HandleProtocol, 3, devices[i], &dpp,
				       (VOID **)&path);
		if (EFI_ERROR(rc))
			continue;

		fwup_debug(L"Device supporting SFSP: %s", DevicePathToStr(path));

		while (!IsDevicePathEnd(path)) {
			fwup_debug(L"Comparing: %s and %s",
			           DevicePathToStr(parent_dp),
			           DevicePathToStr(path));

			if (LibMatchDevicePaths(path, parent_dp) == TRUE) {
				*fh = devices[i];
				for (UINTN j = 0; j < count; j++)
					*file_dp = NextDevicePathNode(*file_dp);

				fwup_debug(L"Match up! Returning %s",
					   DevicePathToStr(*file_dp));
				return EFI_SUCCESS;
			}

			path = NextDevicePathNode(path);
		}
	}

	fwup_warning(L"Failed to find '%s' DevicePath", DevicePathToStr(*file_dp));
	return EFI_UNSUPPORTED;
}

static EFI_STATUS
fwup_open_file(EFI_DEVICE_PATH *dp, EFI_FILE_HANDLE *fh)
{
	CONST UINTN devpath_max_size = 1024; /* arbitrary limit */
	EFI_DEVICE_PATH *file_dp = dp;
	EFI_GUID sfsp = SIMPLE_FILE_SYSTEM_PROTOCOL;
	EFI_FILE_HANDLE device;
	EFI_FILE_IO_INTERFACE *drive;
	EFI_FILE_HANDLE root;
	EFI_STATUS rc;

	rc = uefi_call_wrapper(BS->LocateDevicePath, 3, &sfsp, &file_dp,
			       (EFI_HANDLE *)&device);
	if (EFI_ERROR(rc)) {
		rc = fwup_search_file(&file_dp, &device);
		if (EFI_ERROR(rc)) {
			fwup_warning(L"Could not locate device handle: %r", rc);
			return rc;
		}
	}

	if (DevicePathType(file_dp) != MEDIA_DEVICE_PATH ||
			   DevicePathSubType(file_dp) != MEDIA_FILEPATH_DP) {
		fwup_warning(L"Could not find appropriate device");
		return EFI_UNSUPPORTED;
	}

	UINT16 sz16;
	UINTN sz;
	CopyMem(&sz16, &file_dp->Length[0], sizeof(sz16));
	sz = sz16;
	sz -= 4;
	if (sz <= 6 || sz % 2 != 0 ||
	    sz > devpath_max_size * sizeof(CHAR16)) {
		fwup_warning(L"Invalid file device path of size %d", sz);
		return EFI_INVALID_PARAMETER;
	}

	_cleanup_free CHAR16 *filename = fwup_malloc0(sz + sizeof(CHAR16));
	CopyMem(filename, (UINT8 *)file_dp + 4, sz);

	rc = uefi_call_wrapper(BS->HandleProtocol, 3, device, &sfsp,
			       (VOID **)&drive);
	if (EFI_ERROR(rc)) {
		fwup_warning(L"Could not open device interface: %r", rc);
		return rc;
	}
	fwup_debug(L"Found device");

	rc = uefi_call_wrapper(drive->OpenVolume, 2, drive, &root);
	if (EFI_ERROR(rc)) {
		fwup_warning(L"Could not open volume: %r", rc);
		return rc;
	}
	fwup_debug(L"Found volume");

	rc = uefi_call_wrapper(root->Open, 5, root, fh, filename,
			       EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(rc)) {
		fwup_warning(L"Could not open file '%s': %r", filename, rc);
		return rc;
	}
	fwup_debug(L"Found file");

	return EFI_SUCCESS;
}

static EFI_STATUS
fwup_get_gop_mode(UINT32 *mode, EFI_HANDLE loaded_image)
{
	EFI_HANDLE *handles, gop_handle;
	UINTN num_handles;
	EFI_STATUS status;
	EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
	VOID *iface;

	status = LibLocateHandle(ByProtocol, &gop_guid, NULL, &num_handles,
				 &handles);
	if (EFI_ERROR(status))
		return status;

	if (handles == NULL || num_handles == 0)
		return EFI_UNSUPPORTED;

	for (UINTN i = 0; i < num_handles; i++) {
		gop_handle = handles[i];

		status = uefi_call_wrapper(BS->OpenProtocol, 6,
					   gop_handle, &gop_guid, &iface,
					   loaded_image, 0,
					   EFI_OPEN_PROTOCOL_GET_PROTOCOL);
		if (EFI_ERROR(status))
		    continue;

		gop = (EFI_GRAPHICS_OUTPUT_PROTOCOL *)iface;

		*mode = gop->Mode->Mode;
		return EFI_SUCCESS;
	}

	return EFI_UNSUPPORTED;
}

static inline void
fwup_update_ux_capsule_checksum(UX_CAPSULE_HEADER *payload_hdr)
{
	UINT8 *buf = (UINT8 *)payload_hdr;
	UINT8 sum = 0;

	payload_hdr->checksum = 0;
	for (UINTN i = 0; i < sizeof(*payload_hdr); i++)
		sum = (UINT8) (sum + buf[i]);
	payload_hdr->checksum = sum;
}

static EFI_STATUS
fwup_check_gop_for_ux_capsule(EFI_HANDLE loaded_image,
			      EFI_CAPSULE_HEADER *capsule)
{
	UX_CAPSULE_HEADER *payload_hdr;
	EFI_STATUS rc;

	payload_hdr = (UX_CAPSULE_HEADER *) (((UINT8 *) capsule) + capsule->HeaderSize);
	rc = fwup_get_gop_mode(&payload_hdr->mode, loaded_image);
	if (EFI_ERROR(rc))
		return EFI_UNSUPPORTED;

	fwup_update_ux_capsule_checksum(payload_hdr);

	return EFI_SUCCESS;
}

static EFI_STATUS
fwup_add_update_capsule(FWUP_UPDATE_TABLE *update, EFI_CAPSULE_HEADER **capsule_out,
			EFI_CAPSULE_BLOCK_DESCRIPTOR *cbd_out, EFI_HANDLE loaded_image)
{
	EFI_STATUS rc;
	EFI_FILE_HANDLE fh = NULL;
	UINT8 *fbuf = NULL;
	UINTN fsize = 0;
	EFI_CAPSULE_HEADER *capsule;

	UINTN cbd_len;
	EFI_PHYSICAL_ADDRESS cbd_data;
	EFI_CAPSULE_HEADER *cap_out;

	rc = fwup_open_file((EFI_DEVICE_PATH *)update->info->dp_buf, &fh);
	if (EFI_ERROR(rc))
		return rc;

	rc = fwup_read_file(fh, &fbuf, &fsize);
	if (EFI_ERROR(rc))
		return rc;

	uefi_call_wrapper(fh->Close, 1, fh);

	if (fsize < sizeof(EFI_CAPSULE_HEADER)) {
		fwup_warning(L"Invalid capsule size %d", fsize);
		return EFI_INVALID_PARAMETER;
	}

	fwup_debug(L"Read file; %d bytes", fsize);
	fwup_debug(L"updates guid: %g", &update->info->guid);
	fwup_debug(L"File guid: %g", fbuf);

	cbd_len = fsize;
	cbd_data = (EFI_PHYSICAL_ADDRESS)(UINTN)fbuf;
	capsule = cap_out = (EFI_CAPSULE_HEADER *)fbuf;
	if (cap_out->Flags == 0 &&
	    CompareGuid(&update->info->guid, &ux_capsule_guid) != 0) {
#if defined(__aarch64__)
		cap_out->Flags |= update->info->capsule_flags;
#else
		cap_out->Flags |= update->info->capsule_flags |
					CAPSULE_FLAGS_PERSIST_ACROSS_RESET |
					CAPSULE_FLAGS_INITIATE_RESET;
#endif
	}

	if (CompareGuid(&update->info->guid, &ux_capsule_guid) == 0) {
		fwup_debug(L"Checking GOP for ux capsule");
		rc = fwup_check_gop_for_ux_capsule(loaded_image, capsule);
		if (EFI_ERROR(rc))
			return EFI_UNSUPPORTED;
	}

	cbd_out->Length = cbd_len;
	cbd_out->Union.DataBlock = cbd_data;
	*capsule_out = cap_out;

	return EFI_SUCCESS;
}

static EFI_STATUS
fwup_apply_capsules(EFI_CAPSULE_HEADER **capsules,
		    EFI_CAPSULE_BLOCK_DESCRIPTOR *cbd,
		    UINTN num_updates, EFI_RESET_TYPE *reset)
{
	UINT64 max_capsule_size;
	EFI_STATUS rc;

	rc = uefi_call_wrapper(RT->QueryCapsuleCapabilities, 4, capsules,
				num_updates, &max_capsule_size, reset);
	if (EFI_ERROR(rc)) {
		fwup_warning(L"Could not query capsule capabilities: %r", rc);
		return rc;
	}
	fwup_debug(L"QueryCapsuleCapabilities: %r max: %ld reset:%d",
	           rc, max_capsule_size, *reset);
	fwup_debug(L"Capsules: %d", num_updates);

	fwup_msleep(1 * SECONDS);
	rc = uefi_call_wrapper(RT->UpdateCapsule, 3, capsules, num_updates,
			       (EFI_PHYSICAL_ADDRESS)(UINTN)cbd);
	if (EFI_ERROR(rc)) {
		fwup_warning(L"Could not apply capsule update: %r", rc);
		return rc;
	}

	return EFI_SUCCESS;
}

static EFI_STATUS
fwup_set_update_statuses(FWUP_UPDATE_TABLE **updates)
{
	EFI_STATUS rc;
	for (UINTN i = 0; i < FWUP_NUM_CAPSULE_UPDATES_MAX; i++) {
		if (updates[i] == NULL || updates[i]->name == NULL)
			break;
		rc = fwup_set_variable(updates[i]->name, &fwupdate_guid,
				       updates[i]->info, updates[i]->size,
				       updates[i]->attrs);
		if (EFI_ERROR(rc)) {
			fwup_warning(L"Could not update variable status for '%s': %r",
				     updates[i]->name, rc);
			return rc;
		}
	}
	return EFI_SUCCESS;
}

EFI_GUID SHIM_LOCK_GUID =
 {0x605dab50,0xe046,0x4300,{0xab,0xb6,0x3d,0xd8,0x10,0xdd,0x8b,0x23}};

static VOID
__attribute__((__optimize__("0")))
fwup_debug_hook(VOID)
{
	EFI_GUID guid = SHIM_LOCK_GUID;
	UINTN data = 0;
	UINTN data_size = 1;
	EFI_STATUS efi_status;
	UINT32 attrs;
	register volatile int x = 0;
	extern char _text UNUSED, _data UNUSED;

	/* shim has done whatever is needed to get a debugger attached */
	efi_status = uefi_call_wrapper(RT->GetVariable, 5, L"SHIM_DEBUG",
				       &guid, &attrs,  &data_size, &data);
	if (EFI_ERROR(efi_status) || data != 1) {
		efi_status = uefi_call_wrapper(RT->GetVariable, 5,
					       L"FWUPDATE_VERBOSE",
					       &fwupdate_guid, &attrs,
					       &data_size, &data);
		if (EFI_ERROR(efi_status) || data != 1)
			return;
		fwup_debug_set_enabled(TRUE);
		return;
	}

	fwup_debug_set_enabled(TRUE);
	if (x)
		return;

	x = 1;
	fwup_info(L"add-symbol-file "DEBUGDIR
		  L"fwupdate.efi.debug %p -s .data %p",
		  &_text, &_data);
}

EFI_STATUS
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab)
{
	EFI_STATUS rc;
	UINTN i, n_updates = 0;
	EFI_RESET_TYPE reset_type = EfiResetWarm;
	_cleanup_free FWUP_UPDATE_TABLE **updates = NULL;

	InitializeLib(image, systab);

	/* if SHIM_DEBUG is set, fwup_info info for our attached debugger */
	fwup_debug_hook();

	/* step 1: find and validate update state variables */
	/* XXX TODO:
	 * 1) survey the reset types first, and separate into groups
	 *    according to them
	 * 2) if there's more than one, mirror BootCurrent back into BootNext
	 *    so we can do multiple runs
	 * 3) only select the ones from one type for the first go
	 */
	updates = fwup_new0(FWUP_UPDATE_TABLE *, FWUP_NUM_CAPSULE_UPDATES_MAX);
	if (updates == NULL)
		return EFI_OUT_OF_RESOURCES;
	rc = fwup_populate_update_table(updates, &n_updates);
	if (EFI_ERROR(rc)) {
		fwup_warning(L"Could not find updates: %r", rc);
		return rc;
	}
	if (n_updates == 0) {
		fwup_warning(L"No updates to process.  Called in error?");
		return EFI_INVALID_PARAMETER;
	}

	/* step 2: Build our data structure and add the capsules to it */
	_cleanup_free EFI_CAPSULE_HEADER **capsules = NULL;
	capsules = fwup_new0(EFI_CAPSULE_HEADER *, n_updates + 1);
	EFI_CAPSULE_BLOCK_DESCRIPTOR *cbd_data;
	UINTN j = 0;
	cbd_data = fwup_malloc_raw(sizeof(EFI_CAPSULE_BLOCK_DESCRIPTOR)*(n_updates+1));
	if (cbd_data == NULL)
		return EFI_OUT_OF_RESOURCES;
	for (i = 0; i < n_updates; i++) {
		fwup_info(L"Adding new capsule");
		rc = fwup_add_update_capsule(updates[i], &capsules[j], &cbd_data[j], image);
		if (EFI_ERROR(rc)) {
			/* ignore a failing capsule */
			fwup_warning(L"Could not add capsule with guid %g for update: %r",
				     updates[i]->info->guid, rc);
			continue;
		}
		j++;
	}

	if (j == 0) {
		fwup_warning(L"Could not build update list: %r\n", rc);
		return rc;
	}
	n_updates = j;
	fwup_debug(L"n_updates: %d", n_updates);

	cbd_data[i].Length = 0;
	cbd_data[i].Union.ContinuationPointer = 0;

	/* step 3: update the state variables */
	rc = fwup_set_update_statuses(updates);
	if (EFI_ERROR(rc)) {
		fwup_warning(L"Could not set update status: %r", rc);
		return rc;
	}

	/* step 4: apply the capsules */
	rc = fwup_apply_capsules(capsules, cbd_data, n_updates, &reset_type);
	if (EFI_ERROR(rc)) {
		fwup_warning(L"Could not apply capsules: %r", rc);
		return rc;
	}

	/* step 5: if #4 didn't reboot us, do it manually */
	fwup_info(L"Reset System");
	fwup_msleep(5 * SECONDS);
	if (fwup_debug_get_enabled())
		fwup_msleep(30 * SECONDS);
	uefi_call_wrapper(RT->ResetSystem, 4, reset_type, EFI_SUCCESS, 0, NULL);

	return EFI_SUCCESS;
}
