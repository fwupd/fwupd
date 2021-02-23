if get_option('plugin_thunderbolt')
if not get_option('gudev')
  error('gudev is required for plugin_thunderbolt')
endif
cargs = ['-DG_LOG_DOMAIN="FuPluginThunderbolt"']
cargs += '-DTESTDATADIR="' + join_paths(meson.source_root(), 'data', 'tests') + '"'
install_data([
  'thunderbolt.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)
fu_plugin_thunderbolt = shared_module('fu_plugin_thunderbolt',
  fu_hash,
  sources : [
    'fu-plugin-thunderbolt.c',
    'fu-thunderbolt-device.c',
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
if get_option('tests') and umockdev.found() and gio.version().version_compare('>= 2.52')
  cargs += '-DPLUGINBUILDDIR="' + meson.current_build_dir() + '"'
  e = executable(
    'thunderbolt-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-plugin-thunderbolt.c',
      'fu-thunderbolt-device.c',
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
  test_env = environment()
  if get_option('b_sanitize') == 'address'
    test_env.prepend('LD_PRELOAD', 'libasan.so.5', 'libumockdev-preload.so.0', separator : ' ')
  else
    test_env.prepend('LD_PRELOAD', 'libumockdev-preload.so.0')
  endif
  test('thunderbolt-self-test', e, env: test_env, timeout : 120)
endif
endif
