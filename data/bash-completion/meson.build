if bashcomp.found()
  tgt = bashcomp.get_pkgconfig_variable('completionsdir',
                                         define_variable: [ 'prefix', prefix ],
  )

  install_data(['fwupdmgr'],
    install_dir : tgt,
  )
endif
