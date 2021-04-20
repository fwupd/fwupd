cargs = ['-DG_LOG_DOMAIN="FuPluginCpu"']

install_data(['cpu.quirk'],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_cpu',
  fu_hash,
  sources : [
    'fu-plugin-cpu.c',
    'fu-cpu-device.c',
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

if cc.has_argument('-fcf-protection') and host_machine.system() != 'dragonfly'
  libfwupdcethelper = static_library('fwupdcethelper',
    sources : [
      'fu-cpu-helper-cet-common.c',
    ],
    include_directories : [
      root_incdir,
    ],
    c_args : ['-fcf-protection=none'],
    install : false,
  )

  executable(
    'fwupd-detect-cet',
    sources : [
      'fu-cpu-helper-cet.c',
    ],
    include_directories : [
      root_incdir,
    ],
    link_with : [
      libfwupdcethelper,
    ],
    c_args : ['-fcf-protection=full'],
    install : true,
    install_dir : join_paths(libexecdir, 'fwupd')
  )
endif
