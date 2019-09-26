cargs = ['-DG_LOG_DOMAIN="FuPluginThunderbolt"']

fu_plugin_thunderbolt = shared_module('fu_plugin_thunderbolt',
  fu_hash,
  sources : [
    'fu-plugin-thunderbolt.c',
    'fu-thunderbolt-image.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  link_with : [
    libfwupdprivate,
  ],
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)

testdatadir_src = join_paths(meson.source_root(), 'data', 'tests')
testdatadir_dst = join_paths(meson.build_root(), 'data', 'tests')
cargs += '-DTESTDATADIR="' + testdatadir_src + ':' + testdatadir_dst + '"'
executable('tbtfwucli',
  fu_hash,
  sources : [
    'fu-thunderbolt-tool.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  c_args : cargs,
  link_with : [
    fu_plugin_thunderbolt,
    libfwupdprivate,
  ],
  dependencies : [
    plugin_deps,
  ],
)

install_data(['thunderbolt.conf'],
  install_dir:  join_paths(sysconfdir, 'fwupd')
)
# we use functions from 2.52 in the tests
if get_option('tests') and umockdev.found() and gio.version().version_compare('>= 2.52')
  cargs += '-DFU_OFFLINE_DESTDIR="/tmp/fwupd-self-test"'
  cargs += '-DPLUGINBUILDDIR="' + meson.current_build_dir() + '"'
  e = executable(
    'thunderbolt-self-test',
    fu_hash,
    sources : [
      'fu-self-test.c',
      'fu-plugin-thunderbolt.c',
      'fu-thunderbolt-image.c',
    ],
    include_directories : [
      include_directories('../..'),
      include_directories('../../src'),
      include_directories('../../libfwupd'),
    ],
    dependencies : [
      plugin_deps,
      umockdev,
    ],
    link_with : [
      libfwupdprivate,
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
