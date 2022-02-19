tpm2tss_dell = dependency('tss2-esys', version : '>= 2.0', required: get_option('plugin_dell'))
libsmbios_c = dependency('libsmbios_c', version : '>= 2.4.0', required: get_option('plugin_dell'))

if tpm2tss_dell.found() and libsmbios_c.found() and \
   get_option('plugin_dell').require(get_option('plugin_uefi_capsule').allowed(),
    error_message: 'plugin_uefi_capsule is needed for plugin_dell').allowed()

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
    tpm2tss_dell,
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
      libsmbios_c,
      valgrind,
      tpm2tss_dell,
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
