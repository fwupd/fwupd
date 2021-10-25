if get_option('plugin_uefi_capsule')

cargs = ['-DG_LOG_DOMAIN="FuPluginLenovoThinkLmi"']

shared_module('fu_plugin_lenovo_thinklmi',
  fu_hash,
  sources : [
    'fu-plugin-lenovo-thinklmi.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : [
    cargs,
  ],
  link_with : [
    fwupd,
    fwupdplugin,
  ],
  dependencies : [
    plugin_deps,
  ],
)

if get_option('tests')
  testdatadir = join_paths(meson.current_source_dir(), 'tests')
  testdatadirs = environment()
  testdatadirs.set('G_TEST_SRCDIR', meson.current_source_dir())
  testdatadirs.set('G_TEST_BUILDDIR', meson.current_build_dir())
  testdatadirs.set('FWUPD_LOCALSTATEDIR', '/tmp/fwupd-self-test/var')
  cargs += '-DTESTDATADIR="' + testdatadir + '"'
  cargs += '-DPLUGINBUILDDIR="' + meson.current_build_dir() + '"'
  e = executable(
    'lenovo-thinklmi-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
    ],
    include_directories : [
      root_incdir,
      fwupd_incdir,
      fwupdplugin_incdir,
    ],
    dependencies : [
      plugin_deps,
    ],
    link_with : [
      fwupd,
      fwupdplugin,
    ],
    c_args : [
      cargs,
    ],
  )
  test('lenovo-thinklmi-self-test', e)
endif
endif
