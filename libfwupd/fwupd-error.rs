// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// The error code.
// Since: 0.1.1
enum FwupdError {
	// Internal error.
	Internal,
	// Installed newer firmware version.
	VersionNewer,
	// Installed same firmware version.
	VersionSame,
	// Already set to be installed offline.
	AlreadyPending,
	// Failed to get authentication.
	AuthFailed,
	// Failed to read from device.
	Read,
	// Failed to write to the device.
	Write,
	// Invalid file format.
	InvalidFile,
	// No matching device exists.
	NotFound,
	// Nothing to do.
	NothingToDo,
	// Action was not possible.
	NotSupported,
	// Signature was invalid.
	// Since: 0.1.2
	SignatureInvalid,
	// AC power was required.
	// Since: 0.8.0
	AcPowerRequired,
	// Permission was denied.
	// Since: 0.9.8
	PermissionDenied,
	// User has configured their system in a broken way.
	// Since: 1.2.8
	BrokenSystem,
	// The system battery level is too low.
	// Since: 1.2.10
	BatteryLevelTooLow,
	// User needs to do an action to complete the update.
	// Since: 1.3.3
	NeedsUserAction,
	// Failed to get auth as credentials have expired.
	// Since: 1.7.5
	AuthExpired,
	// Invalid data.
	// Since: 2.0.0
	InvalidData,
	// The request timed out.
	// Since: 2.0.0
	TimedOut,
	// The device is busy.
	// Since: 2.0.0
	Busy,
	// The network is not reachable.
	// Since: 2.0.4
	NotReachable,
}
