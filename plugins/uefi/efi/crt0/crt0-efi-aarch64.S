/*
 * crt0-efi-aarch64.S - PE/COFF header for AArch64 EFI applications
 *
 * Copright (C) 2014 Linaro Ltd. <ard.biesheuvel@linaro.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice and this list of conditions, without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later version.
 */

	.section	.text.head

	/*
	 * Magic "MZ" signature for PE/COFF
	 */
	.globl	ImageBase
ImageBase:
	.ascii	"MZ"
	.skip	58				// 'MZ' + pad + offset == 64
	.long	pe_header - ImageBase		// Offset to the PE header.
pe_header:
	.ascii	"PE"
	.short 	0
coff_header:
	.short	0xaa64				// AArch64
	.short	4				// nr_sections
	.long	0 				// TimeDateStamp
	.long	0				// PointerToSymbolTable
	.long	1				// NumberOfSymbols
	.short	section_table - optional_header	// SizeOfOptionalHeader
	.short	0x206				// Characteristics.
						// IMAGE_FILE_DEBUG_STRIPPED |
						// IMAGE_FILE_EXECUTABLE_IMAGE |
						// IMAGE_FILE_LINE_NUMS_STRIPPED
optional_header:
	.short	0x20b				// PE32+ format
	.byte	0x02				// MajorLinkerVersion
	.byte	0x14				// MinorLinkerVersion
	.long	_text_size			// SizeOfCode
	.long	_alldata_size			// SizeOfInitializedData
	.long	0				// SizeOfUninitializedData
	.long	_start - ImageBase		// AddressOfEntryPoint
	.long	_start - ImageBase		// BaseOfCode

extra_header_fields:
	.quad	0				// ImageBase
	.long	0x1000				// SectionAlignment
	.long	0x200				// FileAlignment
	.short	0				// MajorOperatingSystemVersion
	.short	0				// MinorOperatingSystemVersion
	.short	0				// MajorImageVersion
	.short	0				// MinorImageVersion
	.short	0				// MajorSubsystemVersion
	.short	0				// MinorSubsystemVersion
	.long	0				// Win32VersionValue

	.long	_erodata - ImageBase		// SizeOfImage

	// Everything before the kernel image is considered part of the header
	.long	_start - ImageBase		// SizeOfHeaders
	.long	0				// CheckSum
	.short	EFI_SUBSYSTEM			// Subsystem
	.short	0				// DllCharacteristics
	.quad	0				// SizeOfStackReserve
	.quad	0				// SizeOfStackCommit
	.quad	0				// SizeOfHeapReserve
	.quad	0				// SizeOfHeapCommit
	.long	0				// LoaderFlags
	.long	0x6				// NumberOfRvaAndSizes

	.quad	0				// ExportTable
	.quad	0				// ImportTable
	.quad	0				// ResourceTable
	.quad	0				// ExceptionTable
	.quad	0				// CertificationTable
	.quad	0				// BaseRelocationTable

	// Section table
section_table:
	.ascii	".text\0\0\0"
	.long	_evtext - _start	// VirtualSize
	.long	_start - ImageBase	// VirtualAddress
	.long	_etext - _start		// SizeOfRawData
	.long	_start - ImageBase	// PointerToRawData

	.long	0		// PointerToRelocations (0 for executables)
	.long	0		// PointerToLineNumbers (0 for executables)
	.short	0		// NumberOfRelocations  (0 for executables)
	.short	0		// NumberOfLineNumbers  (0 for executables)
	/*
	 * EFI_IMAGE_SCN_MEM_READ | EFI_IMAGE_SCN_MEM_EXECUTE | EFI_IMAGE_SCN_CNT_CODE
	 */
	.long	0x60000020	// Characteristics (section flags)

	.ascii	".data\0\0\0"
	.long	_data_vsize		// VirtualSize
	.long	_data - ImageBase	// VirtualAddress
	.long	_data_size		// SizeOfRawData
	.long	_data - ImageBase	// PointerToRawData

	.long	0		// PointerToRelocations (0 for executables)
	.long	0		// PointerToLineNumbers (0 for executables)
	.short	0		// NumberOfRelocations  (0 for executables)
	.short	0		// NumberOfLineNumbers  (0 for executables)
	/*
	 * EFI_IMAGE_SCN_MEM_WRITE | EFI_IMAGE_SCN_MEM_READ | EFI_IMAGE_SCN_CNT_INITIALIZED_DATA
	 */
	.long	0xc0000040	// Characteristics (section flags)

	.ascii	".sbat\0\0\0"
	.long	_sbat_vsize		// VirtualSize
	.long	_sbat - ImageBase	// VirtualAddress
	.long	_sbat_size		// SizeOfRawData
	.long	_sbat - ImageBase	// PointerToRawData

	.long	0		// PointerToRelocations (0 for executables)
	.long	0		// PointerToLineNumbers (0 for executables)
	.short	0		// NumberOfRelocations  (0 for executables)
	.short	0		// NumberOfLineNumbers  (0 for executables)
	/*
	 * EFI_IMAGE_SCN_MEM_READ | EFI_IMAGE_SCN_ALIGN_8BYTES | EFI_IMAGE_SCN_CNT_INITIALIZED_DATA
	 */
	.long	0x40400040	// Characteristics (section flags)

	.ascii  ".rodata\0"
	.long	_rodata_vsize		// VirtualSize
	.long	_rodata - ImageBase	// VirtualAddress
	.long	_rodata_size		// SizeOfRawData
	.long	_rodata - ImageBase	// PointerToRawData

	.long	0		// PointerToRelocations (0 for executables)
	.long	0		// PointerToLineNumbers (0 for executables)
	.short	0		// NumberOfRelocations  (0 for executables)
	.short	0		// NumberOfLineNumbers  (0 for executables)
	/*
	 * EFI_IMAGE_SCN_MEM_READ | EFI_IMAGE_SCN_ALIGN_8BYTES | EFI_IMAGE_SCN_CNT_INITIALIZED_DATA
	 */
	.long	0x40400040	// Characteristics (section flags)

	.align		12
_start:
	stp		x29, x30, [sp, #-32]!
	mov		x29, sp

	stp		x0, x1, [sp, #16]
	mov		x2, x0
	mov		x3, x1
	adr		x0, ImageBase
	adrp		x1, _DYNAMIC
	add		x1, x1, #:lo12:_DYNAMIC
	bl		_relocate
	cbnz		x0, 0f

	ldp		x0, x1, [sp, #16]
	bl		efi_main

0:	ldp		x29, x30, [sp], #32
	ret
