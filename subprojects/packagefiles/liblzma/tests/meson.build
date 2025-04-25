liblzma_tests = [
  # 'test_bcj_exact_size',
  'test_block_header',
  'test_check',
  'test_filter_flags',
  'test_index',
  'test_stream_flags',
]

tinc = [include_directories('../src/common'), confinc]

foreach t : liblzma_tests
  test(
    t,
    executable(
      t,
      '@0@.c'.format(t),
      c_args: '-DHAVE_CONFIG_H',
      include_directories: tinc,
      dependencies: lzma_dep,
    ),
  )
endforeach
