if bashcomp.found()
  tgt = bashcomp.get_pkgconfig_variable('completionsdir',
                                         define_variable: [ 'prefix', prefix ],
  )


if get_option('agent')
    install_data(['fwupdagent'],
      install_dir : tgt,
    )
endif # get_option('agent')

# replace @libexecdir@
fwupdtool_path = join_paths(libexecdir, 'fwupd')
con2 = configuration_data()
con2.set('libexecdir', fwupdtool_path)
configure_file(
  input : 'fwupdtool.in',
  output : 'fwupdtool',
  configuration : con2,
  install: true,
  install_dir: tgt)

if build_daemon
configure_file(
  input : 'fwupdmgr.in',
  output : 'fwupdmgr',
  configuration : con2,
  install: true,
  install_dir: tgt)
endif # build_daemon

endif # bashcomp.found()
