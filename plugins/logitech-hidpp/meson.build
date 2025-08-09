host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginLogitechHidpp"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('logitech-hidpp.quirk')
plugin_builtin_logitech_hidpp = static_library('fu_plugin_logitech_hidpp',
  rustgen.process('fu-logitech-hidpp.rs'),
  rustgen.process('fu-logitech-hidpp-rdfu.rs'),
  sources: [
    'fu-logitech-hidpp-plugin.c',
    'fu-logitech-hidpp-bootloader.c',
    'fu-logitech-hidpp-bootloader-nordic.c',
    'fu-logitech-hidpp-bootloader-texas.c',
    'fu-logitech-hidpp-common.c',
    'fu-logitech-hidpp-hidpp.c',
    'fu-logitech-hidpp-device.c',
    'fu-logitech-hidpp-hidpp-msg.c',
    'fu-logitech-hidpp-runtime.c',
    'fu-logitech-hidpp-runtime-unifying.c',
    'fu-logitech-hidpp-runtime-bolt.c',
    'fu-logitech-hidpp-radio.c',
    'fu-logitech-rdfu-firmware.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_builtins += plugin_builtin_logitech_hidpp

device_tests += files(
  'tests/logitech-bolt-receiver.json',
  'tests/logitech-k780.json',
  'tests/logitech-m650.json',
  'tests/logitech-m750.json',
  'tests/logitech-mr0077.json',
  'tests/logitech-rqr12.json',
  'tests/logitech-rqr12-signed.json',
  'tests/logitech-rqr24.json',
  'tests/logitech-rqr24-signed.json',
)
enumeration_data += files(
  'tests/logitech-bolt-receiver-setup.json',
)

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'logitech-hidpp-self-test',
    sources: [
      'fu-logitech-hidpp-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: plugin_deps,
    link_with: [
      plugin_libs,
      plugin_builtin_logitech_hidpp,
    ],
    install: true,
    install_rpath: libdir_pkg,
    install_tag: 'tests',
    install_dir: installed_test_bindir,
    c_args: [
      cargs,
      '-DSRCDIR="' + meson.current_source_dir() + '"',
    ],
  )
  test('logitech-hidpp-self-test', e, env: env)
endif
