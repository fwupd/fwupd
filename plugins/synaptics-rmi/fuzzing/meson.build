synaptics_example0x = custom_target('example0x.img',
  output: 'example0x.img',
  command: [synaptics_rmi_dump, 'gen0x', '@OUTPUT@'],
)
synaptics_example10 = custom_target('example10.img',
  output: 'example10.img',
  command: [synaptics_rmi_dump, 'gen10', '@OUTPUT@'],
)
run_target('fuzz-synaptics-rmi',
  command: [
    join_paths(meson.source_root(), 'contrib/afl-fuzz.py'),
    '-i', meson.current_build_dir(),
    '-o', join_paths(meson.current_build_dir(), '..', 'findings'),
    synaptics_rmi_dump,
  ],
  depends: [
    synaptics_example0x,
    synaptics_example10,
  ],
)
