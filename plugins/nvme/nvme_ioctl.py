#!/usr/bin/env python3
#
# Copyright 2024 Mario Limonciello <mario.limonciello@amd.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import ctypes
from fwupd_ioc import IOWR


class nvme_passthru_cmd(ctypes.Structure):
    """creates a struct to match nvme_passthru_cmd"""

    _fields_ = [
        ("opcode", ctypes.c_byte),
        ("flags", ctypes.c_byte),
        ("rsvd1", ctypes.c_short),
        ("nsid", ctypes.c_int),
        ("cdw2", ctypes.c_int),
        ("cdw3", ctypes.c_int),
        ("metadata", ctypes.c_longlong),
        ("addr", ctypes.c_longlong),
        ("metadata_len", ctypes.c_int),
        ("data_len", ctypes.c_int),
        ("cdw10", ctypes.c_int),
        ("cdw11", ctypes.c_int),
        ("cdw12", ctypes.c_int),
        ("cdw13", ctypes.c_int),
        ("cdw14", ctypes.c_int),
        ("cdw15", ctypes.c_int),
        ("timeout_ms", ctypes.c_int),
        ("result", ctypes.c_int),
    ]


NVME_IOCTL_ADMIN_CMD = IOWR(ord("N"), 0x41, nvme_passthru_cmd)


def handle_identify_ctrl(data):
    """Handle the identify ctrl command."""
    # check if it's an identify ctrl command
    in_data = nvme_passthru_cmd.from_buffer_copy(data.retrieve())
    if in_data.opcode != 0x06:
        return False
    if in_data.nsid != 0x00:
        return False
    if in_data.data_len != 4096:
        return False
    if in_data.cdw10 != 0x01:
        return False
    if in_data.cdw11 != 0x00:
        return False

    # return exactly enough fake identify ctrl data
    cns = bytearray(in_data.data_len)
    cns[4:23] = bytearray("223361440214", "ascii")  # sn
    cns[24:63] = bytearray("WD PC SN740 SDDPNQD-256G", "ascii")  # mn
    cns[64:71] = bytearray("73110000", "ascii")  # sr
    cns[260] = 0x14  # slot information
    addr_data = data.resolve(nvme_passthru_cmd.addr.offset, in_data.data_len)
    addr_data.update(0, cns)

    return True


def handle_fw_download(data):
    """Handle the firmware download command."""
    # check if it's an firmware download command
    in_data = nvme_passthru_cmd.from_buffer_copy(data.retrieve())
    if in_data.opcode != 0x11:
        return False
    if in_data.addr != 0x00:
        return False

    return True


def handle_fw_commit(data):
    """Handle the firmware commit command."""
    # check if it's an firmware commit command
    in_data = nvme_passthru_cmd.from_buffer_copy(data.retrieve())
    if in_data.opcode != 0x10:
        return False

    return True


def handle_ioctl(handler, client):
    """Handle the ioctl."""
    if client.get_request() != NVME_IOCTL_ADMIN_CMD:
        return False

    func = None
    arg = client.get_arg()
    data = arg.resolve(0, ctypes.sizeof(nvme_passthru_cmd()))
    in_data = nvme_passthru_cmd.from_buffer_copy(data.retrieve())
    if in_data.opcode == 0x06:
        func = handle_identify_ctrl
    elif in_data.opcode == 0x10:
        func = handle_fw_commit
    elif in_data.opcode == 0x11:
        func = handle_fw_download

    if not func or not func(data):
        return False

    client.complete(0, 0)
    return True
