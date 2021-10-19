install_data([
    'bcm57xx.builder.xml',
  ],
  install_dir: join_paths(installed_test_datadir, 'fuzzing'),
)

# fuzzing targets are shipped in tree
run_target('fuzz-targets-bcm57xx',
  command: [
     fwupdtool,
     'firmware-build',
     join_paths(meson.current_source_dir(), 'bcm57xx.builder.xml'),
     join_paths(meson.current_source_dir(), 'bcm57xx.bin'),
  ],
)
