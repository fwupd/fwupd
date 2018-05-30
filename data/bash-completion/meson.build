if bashcomp.found()
  tgt = bashcomp.get_pkgconfig_variable('completionsdir',
                                         define_variable: [ 'prefix', prefix ],
  )


if get_option('daemon')
    install_data(['fwupdmgr'],
      install_dir : tgt,
    )
endif

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

endif
