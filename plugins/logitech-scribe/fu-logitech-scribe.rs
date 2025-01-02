// Copyright 2024 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

enum FuLogitechScribeUsbCmd {
    CheckBuffersize = 0xCC00,
    Init            = 0xCC01,
    StartTransfer   = 0xCC02,
    DataTransfer    = 0xCC03,
    EndTransfer     = 0xCC04,
    Uninit          = 0xCC05,
    BufferRead      = 0xCC06,
    BufferWrite     = 0xCC07,
    UninitBuffer    = 0xCC08,
    Ack             = 0xFF01,
    Timeout         = 0xFF02,
    Nack            = 0xFF03,
}
