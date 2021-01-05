/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <efi.h>
#include <efilib.h>

#include "fwup-debug.h"
#include "fwup-common.h"

VOID
fwup_msleep(unsigned long msecs)
{
	BS->Stall(msecs);
}

/*
 * Allocate some raw pages that aren't part of the pool allocator.
 */
VOID *
fwup_malloc_raw(UINTN size)
{
	UINTN pages = size / 4096 + ((size % 4096) ? 1 : 0); /* page size is always 4096 */
	EFI_STATUS rc;
	EFI_PHYSICAL_ADDRESS pageaddr = 0;
	EFI_ALLOCATE_TYPE type = AllocateAnyPages;

	if (sizeof(VOID *) == 4) {
		pageaddr = 0xffffffffULL - 8192;
		type = AllocateMaxAddress;
	}

	rc = uefi_call_wrapper(BS->AllocatePages, 4, type,
			       EfiLoaderData, pages,
			       &pageaddr);
	if (EFI_ERROR(rc)) {
		fwup_warning(L"Could not allocate %d", size);
		return NULL;
	}
	if (sizeof(VOID *) == 4 && pageaddr > 0xffffffffULL) {
		uefi_call_wrapper(BS->FreePages, 2, pageaddr, pages);
		fwup_warning(L"Got bad allocation at 0x%016x", (UINT64)pageaddr);
		return NULL;
	}
	return (VOID *)(UINTN)pageaddr;
}

/*
 * Free our raw page allocations.
 */
static EFI_STATUS
fwup_free_raw(VOID *addr, UINTN size)
{
	UINTN pages = size / 4096 + ((size % 4096) ? 1 : 0);
	return uefi_call_wrapper(BS->FreePages, 2,
				 (EFI_PHYSICAL_ADDRESS)(UINTN)addr, pages);
}

VOID *
fwup_malloc (UINTN size)
{
	VOID *addr = AllocatePool(size);
	if (addr == NULL)
		fwup_warning(L"Could not allocate %d", size);
	return addr;
}

VOID *
fwup_malloc0 (UINTN size)
{
	VOID *addr = AllocateZeroPool(size);
	if (addr == NULL)
		fwup_warning(L"Could not allocate %d", size);
	return addr;
}

EFI_STATUS
fwup_time(EFI_TIME *ts)
{
	EFI_TIME_CAPABILITIES timecaps = { 0, };
	return uefi_call_wrapper(RT->GetTime, 2, ts, &timecaps);
}

EFI_STATUS
fwup_read_file(EFI_FILE_HANDLE fh, UINT8 **buf_out, UINTN *buf_size_out)
{
	const UINTN bs = 512;
	UINTN i = 0;
	UINTN n_blocks = 4096;
	UINT8 *buf = NULL;

	while (1) {
		VOID *newb = NULL;
		UINTN news = n_blocks * bs * 2;

		newb = fwup_malloc_raw(news);
		if (newb == NULL)
			return EFI_OUT_OF_RESOURCES;
		if (buf != NULL) {
			CopyMem(newb, buf, bs * n_blocks);
			fwup_free_raw(buf, bs * n_blocks);
		}
		buf = newb;
		n_blocks *= 2;

		for (; i < n_blocks; i++) {
			EFI_STATUS rc;
			UINTN sz = bs;

			rc = uefi_call_wrapper(fh->Read, 3, fh, &sz, &buf[i * bs]);
			if (EFI_ERROR(rc)) {
				fwup_free_raw(buf, bs * n_blocks);
				fwup_warning(L"Could not read file: %r", rc);
				return rc;
			}

			if (sz != bs) {
				*buf_size_out = bs * i + sz;
				*buf_out = buf;
				return EFI_SUCCESS;
			}
		}
	}
	return EFI_SUCCESS;
}
