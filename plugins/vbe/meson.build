if get_option('plugin_vbe').enabled() or \
   (get_option('plugin_vbe').auto() and libfdt.found() and libz.found())
cargs = ['-DG_LOG_DOMAIN="FuPluginVbe"']

shared_module('fu_plugin_vbe',
  fu_hash,
  sources : [
    'fit.c',
  ],
  include_directories : [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
    plugindfu_incdir,
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    dfu,
    fwupd,
    fwupdplugin,
  ],
  c_args : [
    cargs,
    '-DLOCALSTATEDIR="' + localstatedir + '"',
  ],
  dependencies : [
    plugin_deps,
    libfdt,
    libz,
  ],
)

glib2 = dependency('glib-2.0')

if get_option('tests') and run_sanitize_unsafe_tests and umockdev.found() and gio.version().version_compare('>= 2.52')
  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())
  e = executable(
    'fit_test',
    sources : [
      'fit_test.c',
      'fit.c',
    ],
    include_directories : [
      root_incdir,
    ],
    dependencies : [
      glib2,
      libfdt,
      libz,
      plugin_deps,
    ],
    link_with : [
    ],
  )
  test('fit_test.c', e, is_parallel:false, env:env)
endif

endif
