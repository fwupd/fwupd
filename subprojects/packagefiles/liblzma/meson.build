# liblzma is what upstream uses for their pkg-config name
project(
  'liblzma',
  'c',
  version: '5.2.12',
  license: 'pd/lgpl2/gpl2/gpl3',
  meson_version: '>=0.49.0',
)

add_project_arguments(
  '-D_GNU_SOURCE',
  language: 'c',
)

cc = meson.get_compiler('c')
cdata = configuration_data()

check_headers = [
  'byteswap.h',
  'CommonCrypto/CommonDigest.h',
  'dlfcn.h',
  'fcntl.h',
  'getopt.h',
  'inttypes.h',
  'limits.h',
  'memory.h',
  'minix/config.h',
  'minix/sha2.h',
  'sha256.h',
  'sha2.h',
  'stdbool.h',
  'stdint.h',
  'stdio.h',
  'stdlib.h',
  'string.h',
  'strings.h',
  'sys/byteorder.h',
  'sys/endian.h',
  'sys/param.h',
  'sys/stat.h',
  'sys/time.h',
  'sys/types.h',
  'unistd.h',
  'wchar.h',
]

functions = [
  '__builtin_assume_aligned',
  '_futime',
  'futimens',
  'futimes',
  'futimesat',
  'getopt_long',
  'mbrtowc',
  'posix_fadvise',
  'utime',
  'wcwidth',
]

macros = {
  'bswap_16': 'byteswap.h',
  'bswap_32': 'byteswap.h',
  'bswap_64': 'byteswap.h',
}

decoders = [
  'decoder_arm',
  'decoder_arm64',
  'decoder_armthumb',
  'decoder_delta',
  'decoder_ia64',
  'decoder_lzma1',
  'decoder_lzma2',
  'decoder_powerpc',
  'decoder_sparc',
  'decoder_x86',
]

encoders = [
  'encoder_arm',
  'encoder_arm64',
  'encoder_armthumb',
  'encoder_delta',
  'encoder_ia64',
  'encoder_lzma1',
  'encoder_lzma2',
  'encoder_powerpc',
  'encoder_sparc',
  'encoder_x86',
]

foreach d : decoders
  if get_option(d)
    cdata.set('HAVE_DECODERS', 1)
    break
  endif
endforeach

foreach e : encoders
  if get_option(e)
    cdata.set('HAVE_ENCODERS', 1)
    break
  endif
endforeach

foreach h : check_headers
  if cc.has_header(h)
    cdata.set('HAVE_@0@'.format(h.underscorify().to_upper()), 1)
  endif
endforeach

foreach m, h : macros
  if cc.has_header_symbol(h, m)
    cdata.set('HAVE_@0@'.format(m.underscorify().to_upper()), 1)
  endif
endforeach

if cc.check_header('immintrin.h')
  cdata.set('HAVE_IMMINTRIN_H', 1)
endif

if cdata.has('HAVE_IMMINTRIN_H') and cc.has_header_symbol(
  'immintrin.h',
  '_mm_movemask_epi8',
)
  cdata.set('HAVE__MM_MOVEMASK_EPI8', 1)
endif

foreach f : functions
  if cc.has_function(f)
    cdata.set('HAVE_@0@'.format(f.underscorify().to_upper()), 1)
  endif
endforeach

if cc.has_function('__builtin_bswap16') and cc.has_function('__builtin_bswap32') and cc.has_function(
  '__builtin_bswap64',
)
  cdata.set('HAVE___BUILTIN_BSWAPXX', 1)
endif

if cc.has_function('sched_getaffinity')
  cdata.set('TUKLIB_CPUCORES_SCHED_GETAFFINITY', 1)
endif

if cc.has_header_symbol('time.h', 'CLOCK_MONOTONIC') and host_machine.system() != 'windows'
  cdata.set('HAVE_DECL_CLOCK_MONOTONIC', 1)
endif

if false
  #if cc.has_header_symbol('getopt.h', 'optreset', args: '-D_BSD_SOURCE')
  cdata.set('HAVE_OPTRESET', 1)
endif

if cc.has_header_symbol(
  'errno.h',
  'program_invocation_name',
  args: '-D_GNU_SOURCE',
)
  cdata.set('HAVE_DECL_PROGRAM_INVOCATION_NAME', 1)
endif

if cc.has_header_symbol('unistd.h', '_SC_PAGESIZE') and cc.has_header_symbol(
  'unistd.h',
  '_SC_PHYS_PAGES',
)
  cdata.set('TUKLIB_PHYSMEM_SYSCONF', 1)
endif

thread_dep = dependency(
  'threads',
  required: get_option('threads'),
)
if cc.has_function(
  'pthread_condattr_setclock',
  dependencies: thread_dep,
)
  cdata.set('HAVE_PTHREAD_CONDATTR_SETCLOCK', 1)
endif

#if cc.has_function('clock_gettime', dependencies: thread_dep)
if cc.has_function('clock_gettime')
  cdata.set('HAVE_CLOCK_GETTIME', 1)
endif

if cc.compiles(
  '#include <pthread.h>\nint main() { int i = PTHREAD_PRIO_INHERIT; }',
  dependencies: thread_dep,
)
  cdata.set('HAVE_PTHREAD_PRIO_INHERIT', 1)
endif

if host_machine.system() != 'windows' and thread_dep.found()
  cdata.set('MYTHREAD_POSIX', 1)
endif

if host_machine.system() == 'windows' and thread_dep.found()
  cdata.set('MYTHREAD_VISTA', 1)
endif

if cc.has_type(
  'uintptr_t',
  prefix: '#include <stdint.h>',
)
  cdata.set('HAVE_UINTPTR_T', 1)
endif

if cc.has_member(
  'struct stat',
  'st_atim.tv_nsec',
  prefix: '#include <sys/stat.h>',
)
  cdata.set('HAVE_STRUCT_STAT_ST_ATIM_TV_NSEC', 1)
endif

if meson.version().version_compare('>= 0.59')
  nls_dep = dependency(
    'intl',
    required: get_option('nls'),
  )
else
  nls_dep = dependency(
    '',
    required: false,
  )
endif
if nls_dep.found()
  cdata.set10('ENABLE_NLS', true)
  has_dcgettext = nls_dep.type_name() == 'internal' or cc.has_function(
    'dcgettext',
    dependencies: nls_dep,
  )
  cdata.set10(
    'HAVE_DCGETTEXT',
    has_dcgettext,
  )
  cdata.set10(
    'HAVE_GETTEXT',
    has_dcgettext,
  )
  add_project_arguments(
    '-DLOCALEDIR="@0@"'.format(get_option('prefix') / get_option('localedir')),
    language: 'c',
  )
endif

foreach i : decoders + encoders
  if get_option(i)
    cdata.set('HAVE_@0@'.format(i.underscorify().to_upper()), 1)
  endif
endforeach

largs = []
if host_machine.system() == 'linux'
  largs += cc.get_supported_link_arguments(
    '-Wl,--version-script,@0@/src/liblzma/liblzma_linux.map'.format(
      meson.current_source_dir(),
    ),
  )
elif host_machine.system() == 'freebsd'
  largs += cc.get_supported_link_arguments(
    '-Wl,--version-script,@0@/src/liblzma/liblzma_generic.map'.format(
      meson.current_source_dir(),
    ),
  )
endif

if get_option('b_ndebug') == 'true'
  cdata.set('NDEBUG', 1)
endif

cdata.set10('HAVE__BOOL', cc.has_type('_Bool'))
cdata.set10('HAVE_VISIBILITY', cc.has_function_attribute('visibility'))

cdata.set_quoted('PACKAGE', 'xz')
cdata.set_quoted('PACKAGE_TARNAME', 'xz')
cdata.set_quoted('VERSION', meson.project_version())
cdata.set_quoted('PACKAGE_VERSION', meson.project_version())
cdata.set_quoted('PACKAGE_NAME', 'XZ Utils')
cdata.set_quoted(
  'PACKAGE_STRING',
  'XZ Utils @0@'.format(meson.project_version()),
)
cdata.set_quoted('PACKAGE_URL', 'https://tukaani.org/xz/')
cdata.set_quoted('PACKAGE_BUGREPORT', 'lasse.collin@tukaani.org')

cdata.set('ASSUME_RAM', 128)
cdata.set('HAVE_CHECK_CRC32', 1)
cdata.set('HAVE_CHECK_CRC64', 1)
cdata.set('HAVE_CHECK_SHA256', 1)
cdata.set('TUKLIB_FAST_UNALIGNED_ACCESS', 1)
cdata.set('HAVE_MF_BT2', 1)
cdata.set('HAVE_MF_BT3', 1)
cdata.set('HAVE_MF_BT4', 1)
cdata.set('HAVE_MF_HC3', 1)
cdata.set('HAVE_MF_HC4', 1)

cdata.set('STDC_HEADERS', 1)
cdata.set('_ALL_SOURCE', 1)
cdata.set('_DARWIN_C_SOURCE', 1)
cdata.set('__EXTENSIONS__', 1)
cdata.set('_GNU_SOURCE', 1)
cdata.set('_HPUX_ALT_XOPEN_SOCKET_API', 1)
cdata.set('_NETBSD_SOURCE', 1)
cdata.set('_OPENBSD_SOURCE', 1)
cdata.set('_POSIX_PTHREAD_SEMANTICS', 1)
cdata.set('__STDC_WANT_IEC_60559_ATTRIBS_EXT__ ', 1)
cdata.set('__STDC_WANT_IEC_60559_BFP_EXT__', 1)
cdata.set('__STDC_WANT_IEC_60559_DFP_EXT__', 1)
cdata.set('__STDC_WANT_IEC_60559_FUNCS_EXT__ ', 1)
cdata.set('__STDC_WANT_IEC_60559_TYPES_EXT__', 1)
cdata.set('__STDC_WANT_LIB_EXT2__', 1)
cdata.set('__STDC_WANT_MATH_SPEC_FUNCS__', 1)
cdata.set('_TANDEM_SOURCE', 1)

configure_file(
  input: 'config.h.meson',
  output: 'config.h',
  configuration: cdata,
)

confinc = include_directories('.')
subdir('src')
subdir('tests')
