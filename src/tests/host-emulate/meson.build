if build_standalone
  gzip = find_program('gzip')
  foreach input_file : [
    'thinkpad-p1-iommu.json',
  ]
    custom_target(input_file,
      input: input_file,
      output: '@0@.gz'.format(input_file),
      capture: true,
      command: [gzip, '--keep', '--stdout', '@INPUT@'],
      install: true,
      install_dir: join_paths(datadir, 'fwupd', 'host-emulate.d'),
    )
  endforeach
endif
