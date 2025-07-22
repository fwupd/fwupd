libmm_glib = dependency('mm-glib', version: '>= 1.22.0', required: get_option('plugin_modem_manager'))
libqmi_glib = dependency('qmi-glib', version: '>= 1.32.0', required: get_option('plugin_modem_manager'))
libmbim_glib = dependency('mbim-glib', version: '>= 1.28.0', required: get_option('plugin_modem_manager'))

libmm_glib.found() or subdir_done()
libqmi_glib.found() or subdir_done()
libmbim_glib.found() or subdir_done()
get_option('plugin_modem_manager').allowed() or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginMm"']
cargs +=['-DMM_REQUIRED_VERSION="1.10.0"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('modem-manager.quirk')

shared_module('fu_plugin_modem_manager',
  rustgen.process(
    'fu-mm-fdl.rs',
  ),
  sources: [
    'fu-mm-backend.c',
    'fu-mm-common.c',
    'fu-mm-device.c',
    'fu-mm-dfota-device.c',
    'fu-mm-fastboot-device.c',
    'fu-mm-fdl-device.c',
    'fu-mm-firehose-device.c',
    'fu-mm-mbim-device.c',
    'fu-mm-mhi-qcdm-device.c',
    'fu-mm-plugin.c',
    'fu-mm-qcdm-device.c',
    'fu-mm-qdu-mbim-device.c',
    'fu-mm-qmi-device.c',
  ],
  include_directories: plugin_incdirs,
  install: true,
  install_rpath: libdir_pkg,
  install_dir: libdir_pkg,
  c_args: cargs,
  link_with: plugin_libs,
  dependencies: [
    plugin_deps,
    libmm_glib,
    libqmi_glib,
    libmbim_glib,
  ],
)

device_tests += files(
  'tests/foxconn-t99w373-mbim.json',
  'tests/qc-eg25ggc-fastboot.json',
)
enumeration_data += files(
  'tests/foxconn-t99w373-mbim-setup.json',
  'tests/qc-eg25ggc-fastboot-setup.json',
)

if get_option('tests')
  e = executable(
    'modem-manager-self-test',
    sources: [
      'fu-self-test.c',
      'fu-mm-common.c',
      'fu-mm-device.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      plugin_deps,
      libmm_glib,
      libqmi_glib,
      libmbim_glib,
    ],
    link_with: [
      plugin_libs,
    ],
    c_args: [
      cargs,
    ],
  )
  test('modem-manager-self-test', e)
endif
