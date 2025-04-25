lzma_sources = files(
  '../common/tuklib_physmem.c',
  'common/block_util.c',
  'common/common.c',
  'common/easy_preset.c',
  'common/filter_common.c',
  'common/hardware_physmem.c',
  'common/index.c',
  'common/stream_flags_common.c',
  'common/vli_size.c',
)

main_enc_sources = files(
  'common/alone_encoder.c',
  'common/block_buffer_encoder.c',
  'common/block_encoder.c',
  'common/block_header_encoder.c',
  'common/easy_buffer_encoder.c',
  'common/easy_encoder.c',
  'common/easy_encoder_memusage.c',
  'common/filter_buffer_encoder.c',
  'common/filter_encoder.c',
  'common/filter_flags_encoder.c',
  'common/index_encoder.c',
  'common/stream_buffer_encoder.c',
  'common/stream_encoder.c',
  'common/stream_flags_encoder.c',
  'common/vli_encoder.c',
)

thread_sources = files(
  '../common/tuklib_cpucores.c',
  'common/hardware_cputhreads.c',
  'common/outqueue.c',
  'common/stream_encoder_mt.c',
)

main_dec_sources = files(
  'common/alone_decoder.c',
  'common/auto_decoder.c',
  'common/block_buffer_decoder.c',
  'common/block_decoder.c',
  'common/block_header_decoder.c',
  'common/easy_decoder_memusage.c',
  'common/filter_buffer_decoder.c',
  'common/filter_decoder.c',
  'common/filter_flags_decoder.c',
  'common/index_decoder.c',
  'common/index_hash.c',
  'common/stream_buffer_decoder.c',
  'common/stream_decoder.c',
  'common/stream_flags_decoder.c',
  'common/vli_decoder.c',
)

check_sources = files(
  'check/check.c',
  'check/crc32_fast.c',
  'check/crc32_table.c',
  'check/crc64_fast.c',
  'check/crc64_table.c',
  'check/sha256.c',
)

simplefilter_sources = []
if get_option('filter_simple')
  simplefilter_sources += files(
    'simple/simple_coder.c',
    'simple/simple_decoder.c',
    'simple/simple_encoder.c',
    'simple/x86.c',
  )  # FIXME add other platforms.
endif

lzma1_sources = []
if get_option('filter_lzma1')
  lzma1_sources += files('rangecoder/price_table.c')
  if get_option('encoder_lzma1')
    lzma1_sources += files(
      'lzma/fastpos_table.c',
      'lzma/lzma_encoder.c',
      'lzma/lzma_encoder_optimum_fast.c',
      'lzma/lzma_encoder_optimum_normal.c',
      'lzma/lzma_encoder_presets.c',
    )
  endif
  if get_option('encoder_lzma1')
    lzma1_sources += files('lzma/lzma_decoder.c')
  endif
  if get_option('encoder_lzma2')
    lzma1_sources += files('lzma/lzma2_encoder.c')
  endif
  if get_option('decoder_lzma2')
    lzma1_sources += files('lzma/lzma2_decoder.c')
  endif
endif

lz_sources = []
if get_option('filter_lz')
  if get_option('encoder_lz')
    lz_sources += files('lz/lz_encoder.c', 'lz/lz_encoder_mf.c')
  endif
  if get_option('decoder_lz')
    lz_sources += files('lz/lz_decoder.c')
  endif
endif

delta_sources = []
if get_option('filter_delta')
  delta_sources += files('delta/delta_common.c')
  if get_option('encoder_delta')
    delta_sources += files('delta/delta_encoder.c')
  endif
  if get_option('decoder_delta')
    delta_sources += files('delta/delta_decoder.c')
  endif
endif

api = ['-DHAVE_CONFIG_H', '-DTUKLIB_SYMBOL_PREFIX=lzma_']
if host_machine.system() == 'windows' and get_option('default_library') != 'static'
  api += '-DDLL_EXPORT'
endif

lzmainc = include_directories(
  'api',
  'common',
  'check',
  'lz',
  'rangecoder',
  'lzma',
  'delta',
  'simple',
  '../common',
)

liblzma = library(
  'lzma',
  lzma_sources,
  main_dec_sources,
  main_enc_sources,
  check_sources,
  simplefilter_sources,
  lzma1_sources,
  lz_sources,
  delta_sources,
  thread_sources,
  gnu_symbol_visibility: 'hidden',
  include_directories: [confinc, lzmainc],
  dependencies: [nls_dep, thread_dep],
  c_args: api,
  link_args: largs,
  install: true,
)

api = []
if host_machine.system() == 'windows' and get_option('default_library') == 'static'
  api += '-DLZMA_API_STATIC'
endif

lzma_dep = declare_dependency(
  compile_args: api,
  link_with: liblzma,
  include_directories: include_directories('api'),
  dependencies: [nls_dep, thread_dep],
)
