# Copyright 2025 lazro <2059899519@qq.com>
# SPDX-License-Identifier: LGPL-2.1-or-later

cargs = ['-DG_LOG_DOMAIN="FuPluginLegionHid"']

plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('legion-hid.quirk')

plugin_builtins += static_library('fu_plugin_legion_hid',
  rustgen.process('fu-legion-hid.rs'),
  sources: [
    'fu-legion-hid-plugin.c',
    'fu-legion-hid-device.c',
    'fu-legion-hid-firmware.c'
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
