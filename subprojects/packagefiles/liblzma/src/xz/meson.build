sources = files(
  '../common/tuklib_exit.c',
  '../common/tuklib_mbstr_fw.c',
  '../common/tuklib_mbstr_width.c',
  '../common/tuklib_open_stdxxx.c',
  '../common/tuklib_progname.c',
  'args.c',
  'coder.c',
  'file_io.c',
  'hardware.c',
  'list.c',
  'main.c',
  'message.c',
  'mytime.c',
  'options.c',
  'signals.c',
  'suffix.c',
  'util.c',
)

xzinc = include_directories('../common', '../liblzma/api', '../../lib')

xz = executable(
  'xz',
  sources,
  c_args: ['-DHAVE_CONFIG_H'],
  include_directories: [confinc, xzinc],
  dependencies: lzma_dep,
)
