if get_option('plugin_cpu').disable_auto_if(host_machine.system() != 'linux').require(get_option('hsi'),
    error_message: 'plugin_cpu needs hsi to be set').allowed()
cargs = ['-DG_LOG_DOMAIN="FuPluginCpu"']

plugin_quirks += join_paths(meson.current_source_dir(), 'cpu.quirk')

shared_module('fu_plugin_cpu',
  fu_hash,
  sources: [
    'fu-plugin-cpu.c',
    'fu-cpu-device.c',
  ],
  include_directories: [
    root_incdir,
    fwupd_incdir,
    fwupdplugin_incdir,
  ],
  install: true,
  install_dir: plugin_dir,
  link_with: [
    fwupd,
    fwupdplugin,
  ],
  c_args: cargs,
  dependencies: [
    plugin_deps,
  ],
)

code = '''
#if !__has_attribute (__noclone__)
#error symver attribute not supported
#endif
static void __attribute__((noinline,noclone)) f(void) {}
'''

# verify the compiler knows what to do
if cc.has_argument('-fcf-protection')
  build_fwupdcethelper = cc.compiles(code,
    name: '__attribute__((noinline,noclone))',
  )
else
  build_fwupdcethelper = false
endif

if build_fwupdcethelper
  libfwupdcethelper = static_library('fwupdcethelper',
    sources: [
      'fu-cpu-helper-cet-common.c',
    ],
    include_directories: [
      root_incdir,
    ],
    c_args: ['-fcf-protection=none'],
    install: false,
  )

  executable(
    'fwupd-detect-cet',
    sources: [
      'fu-cpu-helper-cet.c',
    ],
    include_directories: [
      root_incdir,
    ],
    link_with: [
      libfwupdcethelper,
    ],
    c_args: ['-fcf-protection=full'],
    install: true,
    install_dir: join_paths(libexecdir, 'fwupd')
  )
endif
endif
