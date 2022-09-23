if gudev.found() and (host_cpu == 'x86' or host_cpu == 'x86_64')
cargs = ['-DG_LOG_DOMAIN="FuPluginThunderbolt"']

plugin_quirks += files('thunderbolt.quirk')

plugin_builtin_thunderbolt = static_library('fu_plugin_thunderbolt',
  sources: [
    'fu-thunderbolt-plugin.c',
    'fu-thunderbolt-common.c',
    'fu-thunderbolt-device.c',
    'fu-thunderbolt-retimer.c',
    'fu-thunderbolt-controller.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: plugin_deps,
)
plugin_libs += plugin_builtin_thunderbolt

install_data(['thunderbolt.conf'],
  install_dir: join_paths(sysconfdir, 'fwupd')
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
    sources: [
      'fu-self-test.c',
    ],
    include_directories: plugin_incdirs,
    dependencies: [
      gudev,
      plugin_deps,
      umockdev,
    ],
    link_with: [
      fwupd,
      fwupdplugin,
      plugin_builtin_thunderbolt,
    ],
    c_args: cargs
  )
  if get_option('b_sanitize') == 'address'
    env.prepend('LD_PRELOAD', 'libasan.so.5', 'libumockdev-preload.so.0', separator: ' ')
  else
    env.prepend('LD_PRELOAD', 'libumockdev-preload.so.0')
  endif
  test('thunderbolt-self-test', e, env: env, timeout: 120)
endif
endif
