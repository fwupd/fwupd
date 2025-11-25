host_machine.system() == 'linux' or subdir_done()
host_cpu in ['x86', 'x86_64'] or subdir_done()

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
enumeration_data += files('tests/amd-pmc-setup.json')
device_tests += files('tests/amd-pmc.json')
