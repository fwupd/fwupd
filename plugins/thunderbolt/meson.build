if get_option('plugin_thunderbolt').require(gudev.found(),
    error_message: 'gudev is needed for plugin_thunderbolt').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginThunderbolt"']
install_data([
  'thunderbolt.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)
fu_plugin_thunderbolt = shared_module('fu_plugin_thunderbolt',
  fu_hash,
  sources : [
    'fu-plugin-thunderbolt.c',
    'fu-thunderbolt-common.c',
    'fu-thunderbolt-device.c',
    'fu-thunderbolt-retimer.c',
    'fu-thunderbolt-controller.c',
    'fu-thunderbolt-firmware.c',
    'fu-thunderbolt-firmware-update.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

install_data(['thunderbolt.conf'],
  install_dir:  join_paths(sysconfdir, 'fwupd')
)
# we use functions from 2.52 in the tests
if get_option('tests') and run_sanitize_unsafe_tests and umockdev.found() and gio.version().version_compare('>= 2.52')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  env.set('FWUPD_DATADIR_QUIRKS', meson.current_source_dir())
  e = executable(
    'thunderbolt-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-plugin-thunderbolt.c',
      'fu-thunderbolt-common.c',
      'fu-thunderbolt-device.c',
      'fu-thunderbolt-retimer.c',
      'fu-thunderbolt-controller.c',
      'fu-thunderbolt-firmware.c',
      'fu-thunderbolt-firmware-update.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      gudev,
      plugin_deps,
      umockdev,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : cargs
  )
  if get_option('b_sanitize') == 'address'
    env.prepend('LD_PRELOAD', 'libasan.so.5', 'libumockdev-preload.so.0', separator : ' ')
  else
    env.prepend('LD_PRELOAD', 'libumockdev-preload.so.0')
  endif
  test('thunderbolt-self-test', e, env: env, timeout : 120)
endif
endif
