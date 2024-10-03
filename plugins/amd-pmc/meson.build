if enable_udev and (host_cpu == 'x86' or host_cpu == 'x86_64')
cargs = ['-DG_LOG_DOMAIN="FuPluginAmdPmc"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('amd-pmc.quirk')
plugin_builtins += static_library('fu_plugin_amd_pmc',
  sources: [
    'fu-amd-pmc-plugin.c',
    'fu-amd-pmc-device.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
umockdev_tests += files('amd_pmc_test.py')
endif
