#!/usr/bin/env python3
#
# Copyright 2022 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
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
            for fn in glob.glob(f"**/*.{ext}", recursive=True):
                if fn.startswith("build"):
                    continue
                if fn.startswith("subprojects"):
                    continue
                if fn.startswith(".git"):
                    continue
                fns.append(fn)

    for fn in fns:
        modified: bool = False
        with open(fn) as f:
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
            "fu_common_crc8_full": "fu_crc8",
            "fu_common_crc16": "fu_crc16",
            "fu_common_crc16_full": "fu_crc16",
            "fu_common_crc32": "fu_crc32",
            "fu_common_crc32_full": "fu_crc32",
            "fu_byte_array_set_size_full": "fu_byte_array_set_size",
            "fu_common_string_replace": "g_string_replace",
            "fu_common_string_append_kv": "fwupd_codec_string_append",
            "fu_common_string_append_ku": "fwupd_codec_string_append_int",
            "fu_common_string_append_kx": "fwupd_codec_string_append_hex",
            "fu_common_string_append_kb": "fwupd_codec_string_append_bool",
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
            "fu_common_get_contents_stream": "fu_input_stream_read_bytes",
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
            "fu_common_fnmatch": "g_pattern_match_simple",
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
            "fu_common_get_esp_default": "fu_context_get_esp_volumes",
            "fu_smbios_to_string": "fu_firmware_to_string",
            "fu_i2c_device_read_full": "fu_i2c_device_read",
            "fu_i2c_device_write_full": "fu_i2c_device_write",
            "fu_path_fnmatch": "g_pattern_match_simple",
            "fu_string_replace": "g_string_replace",
            "fu_efi_firmware_decompress_lzma": "fu_lzma_decompress_bytes",
            "fu_device_build_instance_id_quirk": "fu_device_build_instance_id_full",
            "fwupd_bios_setting_array_from_variant": "fwupd_codec_from_variant",
            "fwupd_bios_setting_from_json": "fwupd_codec_from_json",
            "fwupd_bios_setting_from_variant": "fwupd_codec_from_variant",
            "fwupd_bios_setting_to_json": "fwupd_codec_to_json",
            "fwupd_bios_setting_to_string": "fwupd_codec_to_string",
            "fwupd_bios_setting_to_variant": "fwupd_codec_to_variant",
            "fwupd_device_array_from_variant": "fwupd_codec_from_variant",
            "fwupd_device_from_json": "fwupd_codec_from_json",
            "fwupd_device_from_variant": "fwupd_codec_from_variant",
            "fwupd_device_to_json_full": "fwupd_codec_to_json",
            "fwupd_device_to_json": "fwupd_codec_to_json",
            "fwupd_device_to_string": "fwupd_codec_to_string",
            "fwupd_device_to_variant_full": "fwupd_codec_to_variant",
            "fwupd_device_to_variant": "fwupd_codec_to_variant",
            "fwupd_plugin_array_from_variant": "fwupd_codec_from_variant",
            "fwupd_plugin_from_variant": "fwupd_codec_from_variant",
            "fwupd_plugin_to_json": "fwupd_codec_to_json",
            "fwupd_plugin_to_string": "fwupd_codec_to_string",
            "fwupd_plugin_to_variant": "fwupd_codec_to_variant",
            "fwupd_release_array_from_variant": "fwupd_codec_from_variant",
            "fwupd_release_from_variant": "fwupd_codec_from_variant",
            "fwupd_release_to_json": "fwupd_codec_to_json",
            "fwupd_release_to_string": "fwupd_codec_to_string",
            "fwupd_release_to_variant": "fwupd_codec_to_variant",
            "fwupd_remote_array_from_variant": "fwupd_codec_from_variant",
            "fwupd_remote_from_variant": "fwupd_codec_from_variant",
            "fwupd_remote_to_json": "fwupd_codec_to_json",
            "fwupd_remote_to_variant": "fwupd_codec_to_variant",
            "fwupd_report_from_variant": "fwupd_codec_from_variant",
            "fwupd_report_to_json": "fwupd_codec_to_json",
            "fwupd_report_to_string": "fwupd_codec_to_string",
            "fwupd_report_to_variant": "fwupd_codec_to_variant",
            "fwupd_request_from_variant": "fwupd_codec_from_variant",
            "fwupd_request_to_string": "fwupd_codec_to_string",
            "fwupd_request_to_variant": "fwupd_codec_to_variant",
            "fwupd_security_attr_array_from_variant": "fwupd_codec_from_variant",
            "fwupd_security_attr_from_json": "fwupd_codec_from_json",
            "fwupd_security_attr_from_variant": "fwupd_codec_from_variant",
            "fwupd_security_attr_to_json": "fwupd_codec_to_json",
            "fwupd_security_attr_to_string": "fwupd_codec_to_string",
            "fwupd_security_attr_to_variant": "fwupd_codec_to_variant",
        }.items():
            if buf.find(old) == -1:
                continue
            buf = buf.replace(old, new)
            modified = True
        if modified:
            print(f"MODIFIED: {fn}")
            with open(fn, "w") as f:
                f.write(buf)

    sys.exit(0)
