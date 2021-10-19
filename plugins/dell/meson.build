if get_option('plugin_dell')
cargs = ['-DG_LOG_DOMAIN="FuPluginDell"']

install_data(['dell.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_dell',
  fu_hash,
  sources : [
    'fu-plugin-dell.c',
    'fu-dell-smi.c',
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
  c_args : [
    cargs,
  ],
  dependencies : [
    plugin_deps,
    libsmbios_c,
    tpm2tss,
  ],
)

if get_option('tests')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  env.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  e = executable(
    'dell-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-dell-smi.c',
      'fu-plugin-dell.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
      sqlite,
      libsmbios_c,
      valgrind,
      tpm2tss,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : [
      cargs,
    ],
  )
  test('dell-self-test', e, env : env)
endif
endif
