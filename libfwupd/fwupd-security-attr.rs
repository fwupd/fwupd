// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// The flags available for HSI attributes.
// Since: 1.5.0
enum FwupdSecurityAttrFlags {
	// No flags set.
	None = 0,
	// Success.
	Success = 1 << 0,
	// Obsoleted by another attribute.
	Obsoleted = 1 << 1,
	// Missing data.
	MissingData = 1 << 2,
	// Suffix `U`.
	RuntimeUpdates = 1 << 8,
	// Suffix `A`.
	RuntimeAttestation = 1 << 9,
	// Suffix `!`.
	RuntimeIssue = 1 << 10,
	// Contact the firmware vendor for a update.
	ActionContactOem = 1 << 11,
	// Failure may be fixed by changing FW config.
	ActionConfigFw = 1 << 12,
	// Failure may be fixed by changing OS config.
	ActionConfigOs = 1 << 13,
	// The failure can be automatically fixed.
	CanFix = 1 << 14,
	// The fix can be automatically reverted.
	CanUndo = 1 << 15,
}

// The HSI level.
// Since: 1.5.0
enum FwupdSecurityAttrLevel {
	// Very few detected firmware protections.
	None,
	// The most basic of security protections.
	Critical,
	// Firmware security issues considered important.
	Important,
	// Firmware security issues that pose a theoretical concern.
	Theoretical,
	// Out-of-band protection of the system firmware.
	SystemProtection,
	// Out-of-band attestation of the system firmware.
	SystemAttestation,
}

// The HSI result.
// Since: 1.5.0
enum FwupdSecurityAttrResult {
	// Not known.
	Unknown,
	// Enabled.
	Enabled,
	// Not enabled.
	NotEnabled,
	// Valid.
	Valid,
	// Not valid.
	NotValid,
	// Locked.
	Locked,
	// Not locked.
	NotLocked,
	// Encrypted.
	Encrypted,
	// Not encrypted.
	NotEncrypted,
	// Tainted.
	Tainted,
	// Not tainted.
	NotTainted,
	// Found.
	Found,
	// Not found.
	NotFound,
	// Supported.
	Supported,
	// Not supported.
	NotSupported,
}
