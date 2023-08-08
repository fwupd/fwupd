// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString, FromString)]
enum HidItemTag {
	Unknown			= 0b0,
	// Main
	Input			= 0b1000_00,
	Output			= 0b1001_00,
	Feature			= 0b1011_00,
	Collection		= 0b1010_00,
	EndCollection		= 0b1100_00,
	// Global
	UsagePage		= 0b0000_01,
	LogicalMinimum		= 0b0001_01,
	LogicalMaximum		= 0b0010_01,
	PhysicalMinimum		= 0b0011_01,
	PhysicalMaximum		= 0b0100_01,
	Unit			= 0b0101_01,
	ReportSize		= 0b0111_01,
	ReportId		= 0b1000_01,
	ReportCount		= 0b1001_01,
	Push			= 0b1010_01,
	Pop			= 0b1011_01,
	// Local
	Usage			= 0b0000_10,
	UsageMinimum		= 0b0001_10,
	UsageMaximum		= 0b0010_10,
	DesignatorIndex		= 0b0011_10,
	DesignatorMinimum	= 0b0100_10,
	DesignatorMaximum	= 0b0101_10,
	StringIndex		= 0b0111_10,
	StringMinimum		= 0b1000_10,
	StringMaximum		= 0b1001_10,
	// 'just' supported
	Long			= 0b1111,
}

#[derive(ToString)]
enum HidItemKind {
	Main,
	Global,
	Local,
}
