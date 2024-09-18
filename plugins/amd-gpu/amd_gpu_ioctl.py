#!/usr/bin/env python3
#
# Copyright 2024 Mario Limonciello <mario.limonciello@amd.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import ctypes
from fwupd_ioc import IOWR, IOW


class drm_amdgpu_info_vbios(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char * 64),
        ("vbios_png", ctypes.c_char * 64),
        ("version", ctypes.c_uint32),
        ("pad", ctypes.c_uint32),
        ("vbios_ver_str", ctypes.c_char * 32),
        ("date", ctypes.c_char * 32),
    ]


class drm_amdgpu_info_union(ctypes.Union):
    class mode_crtc(ctypes.Structure):
        _fields_ = [
            ("id", ctypes.c_uint),
            ("_pad", ctypes.c_uint),
        ]

    class query_hw_ip(ctypes.Structure):
        _fields_ = [
            ("type", ctypes.c_uint),
            ("ip_instances", ctypes.c_uint),
        ]

    class read_mmr_reg(ctypes.Structure):
        _fields_ = [
            ("dword_offset", ctypes.c_uint),
            ("count", ctypes.c_uint),
            ("instance", ctypes.c_uint),
            ("flags", ctypes.c_uint),
        ]

    class vbios_info(ctypes.Structure):
        _fields_ = [
            ("type", ctypes.c_uint),
            ("offset", ctypes.c_uint),
        ]

    class sensor_info(ctypes.Structure):
        _fields_ = [
            ("type", ctypes.c_uint),
        ]

    class video_cap(ctypes.Structure):
        _fields_ = [
            ("type", ctypes.c_uint),
        ]

    _fields_ = [
        ("mode_crtc", mode_crtc),
        ("query_hw_ip", query_hw_ip),
        ("read_mmr_reg", read_mmr_reg),
        ("vbios_info", vbios_info),
        ("sensor_info", sensor_info),
        ("video_cap", video_cap),
    ]


class drm_amdgpu_info_request(ctypes.Structure):
    _fields_ = [
        ("return_pointer", ctypes.c_ulonglong),
        ("return_size", ctypes.c_uint),
        ("query", ctypes.c_uint),
        ("union", drm_amdgpu_info_union),
    ]


class drm_client(ctypes.Structure):
    _fields_ = [
        ("idx", ctypes.c_int),
        ("auth", ctypes.c_int),
        ("pid", ctypes.c_ulong),
        ("uid", ctypes.c_ulong),
        ("magic", ctypes.c_ulong),
        ("iocs", ctypes.c_ulong),
    ]


class drm_version(ctypes.Structure):
    _fields_ = [
        ("version_major", ctypes.c_int),
        ("version_minor", ctypes.c_int),
        ("version_patchlevel", ctypes.c_int),
        ("name_len", ctypes.c_ulong),
        ("name", ctypes.c_char_p),
        ("date_len", ctypes.c_ulong),
        ("date", ctypes.c_char_p),
        ("desc_len", ctypes.c_ulong),
        ("desc", ctypes.c_char_p),
    ]


DRM_IOCTL_BASE = ord("d")
DRM_COMMAND_BASE = 0x40
DRM_AMDGPU_INFO = 0x05
DRM_IOCTL_VERSION = IOWR(DRM_IOCTL_BASE, 0x00, drm_version)
DRM_IOCTL_GET_CLIENT = IOWR(DRM_IOCTL_BASE, 0x05, drm_client)
DRM_IOCTL_AMDGPU_INFO = IOW(
    DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_AMDGPU_INFO, drm_amdgpu_info_request
)

AMDGPU_INFO_VBIOS = 0x1B
AMDGPU_INFO_VBIOS_SIZE = 0x1
AMDGPU_INFO_VBIOS_IMAGE = 0x2
AMDGPU_INFO_VBIOS_INFO = 0x3


def handle_vbios_info(data):
    """Handle the vbios info."""
    in_data = drm_amdgpu_info_request.from_buffer_copy(data.retrieve())
    out_data = drm_amdgpu_info_vbios()
    out_data.name = b"AMD AMD_PHOENIX_GENERIC"
    out_data.vbios_png = b"113-PHXGENERIC-001"
    out_data.vbios_ver_str = b"022.012.000.027.000001"
    out_data.date = b"2020/06/03"
    addr_data = data.resolve(
        drm_amdgpu_info_request.return_pointer.offset, in_data.return_size
    )
    addr_data.update(0, bytearray(out_data))
    return True


def handle_ioctl_amdgpu_info(client):
    """Handle the ioctl amdgpu info."""
    func = None
    arg = client.get_arg()
    data = arg.resolve(0, ctypes.sizeof(drm_amdgpu_info_request()))
    in_data = drm_amdgpu_info_request.from_buffer_copy(data.retrieve())
    if (
        in_data.query == AMDGPU_INFO_VBIOS
        and in_data.union.vbios_info.type == AMDGPU_INFO_VBIOS_INFO
    ):
        func = handle_vbios_info
    if not func or not func(data):
        return False

    client.complete(0, 0)
    return True


def handle_get_client(client):
    """Handle the get client."""
    arg = client.get_arg()
    data = arg.resolve(0, ctypes.sizeof(drm_client()))
    client_data = drm_client.from_buffer_copy(data.retrieve())
    client_data.auth = 1
    data.update(0, bytearray(client_data))
    client.complete(0, 0)
    return True


def handle_get_version(client):
    """Handle the get version."""
    arg = client.get_arg()
    data = arg.resolve(0, ctypes.sizeof(drm_version()))
    version_data = drm_version.from_buffer_copy(data.retrieve())
    version_data.version_major = 3
    version_data.version_minor = 0
    version_data.version_patchlevel = 0
    version_data.name_len = 0
    version_data.date_len = 0
    version_data.desc_len = 0
    data.update(0, bytearray(version_data))
    client.complete(0, 0)
    return True


def handle_ioctl(handler, client):
    """Handle the ioctl."""
    if client.get_request() == DRM_IOCTL_AMDGPU_INFO:
        return handle_ioctl_amdgpu_info(client)
    # TODO: something is wrong with interactions with optional libdrm interactions
    # elif client.get_request() == DRM_IOCTL_GET_CLIENT:
    #     return handle_get_client(client)
    # elif client.get_request() == DRM_IOCTL_VERSION:
    #     return handle_get_version(client)
    # else:
    #     print("Unknown ioctl: %d" % client.get_request())

    return False
