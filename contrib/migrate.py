#!/usr/bin/python3
#
# Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#

# import os
import sys
import glob


if __name__ == "__main__":

    fns = []

    if len(sys.argv) > 1:
        fns.extend(sys.argv[1:])
    else:
        exts = ["c", "h", "map"]
        for ext in exts:
            for fn in glob.glob("**/*.{}".format(ext), recursive=True):
                if fn.startswith("build"):
                    continue
                if fn.startswith("subprojects"):
                    continue
                if fn.startswith(".git"):
                    continue
                fns.append(fn)

    for fn in fns:
        modified: bool = False
        with open(fn, "r") as f:
            buf = f.read()
        for old, new in {
            "fu_common_sum8": "fu_sum8",
            "fu_common_sum8_bytes": "fu_sum8_bytes",
            "fu_common_sum16": "fu_sum16",
            "fu_common_sum16_bytes": "fu_sum16_bytes",
            "fu_common_sum16w": "fu_sum16w",
            "fu_common_sum16w_bytes": "fu_sum16w_bytes",
            "fu_common_sum32": "fu_sum32",
            "fu_common_sum32_bytes": "fu_sum32_bytes",
            "fu_common_sum32w": "fu_sum32w",
            "fu_common_sum32w_bytes": "fu_sum32w_bytes",
            "fu_common_crc8": "fu_crc8",
            "fu_common_crc8_full": "fu_crc8_full",
            "fu_common_crc16": "fu_crc16",
            "fu_common_crc16_full": "fu_crc16_full",
            "fu_common_crc32": "fu_crc32",
            "fu_common_crc32_full": "fu_crc32_full",
            "fu_byte_array_set_size_full": "fu_byte_array_set_size",
            "fu_common_string_replace": "fu_string_replace",
            "fu_common_string_append_kv": "fu_string_append",
            "fu_common_string_append_ku": "fu_string_append_ku",
            "fu_common_string_append_kx": "fu_string_append_kx",
            "fu_common_string_append_kb": "fu_string_append_kb",
            "fu_common_strnsplit": "fu_strsplit",
            "fu_common_strnsplit_full": "fu_strsplit_full",
            "fu_common_strjoin_array": "fu_strjoin",
            "fu_common_strsafe": "fu_strsafe",
            "fu_common_strwidth": "fu_strwidth",
            "fu_common_strstrip": "fu_strstrip",
            "fu_common_strtoull": "fu_strtoull",
            "fu_common_strtoull_full": "fu_strtoull",
            "FuCommonStrsplitFunc": "FuStrsplitFunc",
            "fu_common_bytes_pad": "fu_bytes_pad",
            "fu_common_bytes_new_offset": "fu_bytes_new_offset",
            "fu_common_bytes_align": "fu_bytes_align",
            "fu_common_bytes_is_empty": "fu_bytes_is_empty",
            "fu_common_bytes_compare(": "fu_bytes_compare(",
            "fu_common_set_contents_bytes": "fu_bytes_set_contents",
            "fu_common_get_contents_bytes": "fu_bytes_get_contents",
            "fu_common_get_contents_stream": "fu_bytes_get_contents_stream",
            "fu_common_get_contents_fd": "fu_bytes_get_contents_fd",
            "fu_common_read_uint8_safe": "fu_memread_uint8_safe",
            "fu_common_read_uint16_safe": "fu_memread_uint16_safe",
            "fu_common_read_uint32_safe": "fu_memread_uint32_safe",
            "fu_common_read_uint64_safe": "fu_memread_uint64_safe",
            "fu_common_write_uint8_safe": "fu_memwrite_uint8_safe",
            "fu_common_write_uint16_safe": "fu_memwrite_uint16_safe",
            "fu_common_write_uint32_safe": "fu_memwrite_uint32_safe",
            "fu_common_write_uint64_safe": "fu_memwrite_uint64_safe",
            "fu_common_write_uint16": "fu_memwrite_uint16",
            "fu_common_write_uint24": "fu_memwrite_uint24",
            "fu_common_write_uint32": "fu_memwrite_uint32",
            "fu_common_write_uint64": "fu_memwrite_uint64",
            "fu_common_read_uint16": "fu_memread_uint16",
            "fu_common_read_uint24": "fu_memread_uint24",
            "fu_common_read_uint32": "fu_memread_uint32",
            "fu_common_read_uint64": "fu_memread_uint64",
            "fu_common_bytes_compare_raw": "fu_memcmp_safe",
            "FuOutputHandler": "FuSpawnOutputHandler",
            "fu_common_spawn_sync": "fu_spawn_sync",
            "fu_common_kernel_locked_down": "fu_kernel_locked_down",
            "fu_common_check_kernel_version": "fu_kernel_check_version",
            "fu_common_get_firmware_search_path": "fu_kernel_get_firmware_search_path",
            "fu_common_set_firmware_search_path": "fu_kernel_set_firmware_search_path",
            "fu_common_reset_firmware_search_path": "fu_kernel_reset_firmware_search_path",
            "fu_common_firmware_builder": "fu_firmware_builder_process",
            "fu_common_uri_get_scheme": "fu_release_uri_get_scheme",
            "fu_common_dump_raw": "fu_dump_raw",
            "fu_common_dump_full": "fu_dump_full",
            "fu_common_dump_bytes": "fu_dump_bytes",
            "fu_common_error_array_get_best": "fu_engine_error_array_get_best",
            "fu_common_get_path": "fu_path_from_kind",
            "fu_common_filename_glob": "fu_path_glob",
            "fu_common_fnmatch": "fu_path_fnmatch",
            "fu_common_rmtree": "fu_path_rmtree",
            "fu_common_get_files_recursive": "fu_path_get_files",
            "fu_common_mkdir": "fu_path_mkdir",
            "fu_common_mkdir_parent": "fu_path_mkdir_parent",
            "fu_common_find_program_in_path": "fu_path_find_program",
            "fu_common_cpuid": "fu_cpuid",
            "fu_common_get_cpu_vendor": "fu_cpu_get_vendor",
            "fu_common_vercmp_full": "fu_version_compare",
            "fu_common_version_ensure_semver_full": "fu_version_ensure_semver",
            "fu_common_version_from_uint16": "fu_version_from_uint16",
            "fu_common_version_from_uint32": "fu_version_from_uint32",
            "fu_common_version_from_uint64": "fu_version_from_uint64",
            "fu_common_version_guess_format": "fu_version_guess_format",
            "fu_common_version_parse_from_format": "fu_version_parse_from_format",
            "fu_common_version_verify_format": "fu_version_verify_format",
            "fu_common_get_volumes_by_kind": "fu_volume_new_by_kind",
            "fu_common_get_volume_by_device": "fu_volume_new_by_device",
            "fu_common_get_volume_by_devnum": "fu_volume_new_by_devnum",
            "fu_common_get_esp_for_path": "fu_volume_new_esp_for_path",
            "fu_common_get_esp_default": "fu_volume_new_esp_default",
            "fu_smbios_to_string": "fu_firmware_to_string",
            "fu_i2c_device_read_full": "fu_i2c_device_read",
            "fu_i2c_device_write_full": "fu_i2c_device_write",
        }.items():
            if buf.find(old) == -1:
                continue
            buf = buf.replace(old, new)
            modified = True
        if modified:
            print("MODIFIED: {}".format(fn))
            with open(fn, "w") as f:
                f.write(buf)

    sys.exit(0)
