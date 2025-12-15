// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// SREC record kind.
enum FuFirmwareSrecRecordKind {
    S0Header,
    S1Data_16,
    S2Data_24,
    S3Data_32,
    S4Reserved,
    S5Count_16,
    S6Count_24,
    S7Count_32,
    S8Termination_24,
    S9Termination_16,
}
