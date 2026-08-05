buildinfo_cfg = {
  'build_backend'   : meson.backend(),
  'build_cpu'       : build_machine.cpu(),
  'build_system'    : build_machine.system(),
  'compiler_id'     : cc.get_id(),
  'compiler_version': cc.version(),
  'host_cpu'        : host_machine.cpu(),
  'host_system'     : host_machine.system(),
  'meson_version'   : meson.version(),
}
if meson.version().version_compare('>=1.1.0')
  buildinfo_cfg += {
    'build_options': meson.build_options(),
  }
endif
configure_file(
  configuration: buildinfo_cfg,
  input: '../meson/buildinfo.txt.meson',
  output: 'buildinfo.txt',
)

libtest_targets = []
server_targets = []
unit_targets = []

subdir('log')

# Extract test targets info.
makefile_extractions = ({
  'libtest': '',
  'server': '',
} + (build_unittests.found() ? {
  'unit': '',
} : {}
)
)
foreach _subdir : makefile_extractions.keys()
  makefile_extractions += {
    _subdir: run_command(
      [
        make_exe,
        '--silent',
        '-C',
        meson.project_source_root(),
        '-f',
        files('../meson/extract.mk'),
        'tests' / _subdir,
      ],
      check: true,
      env: 'MAKEFLAGS=',
    ).stdout().strip().split('\n'),
  }
  subdir(_subdir)
endforeach

_builddir = meson.current_build_dir()
_srcdir = meson.current_source_dir()

_env = environment()
_env.set('srcdir', fs.as_posix(_srcdir))
if host_machine.system() == 'windows'
  foreach _component : ['TOOL', 'SRV']
    _env.set('CURL_TEST_EXE_EXT_' + _component, '.exe')
  endforeach
  _libcurl_dir = meson.project_build_root() / 'lib'
  _env.prepend('PATH', _libcurl_dir.replace('/', '\\'))
endif

_disabled_tests = []
# Do not run flaky tests.
_disabled_tests += '!flaky'
# We don't build the doc.
_disabled_tests += '!1177'
# We don't build man pages either.
_disabled_tests += ['!1139', '!1140', '!1173']
if not fs.exists('errorcodes.pl')
  # Missing in git checkout.
  _disabled_tests += '!1477'
endif
if host_machine.system() == 'darwin'
  # Some preprocessor issue prevent those 2
  # source analysis tests from succeeding.
  _disabled_tests += ['!1119', '!1167']
endif
if not get_option('curldebug')
  _disabled_tests += [
    '!356',
    '!358',
    '!359',
    '!412',
    '!413',
    '!437',
    '!446',
    '!823',
    '!869',
    '!907',
    '!1908',
    '!1972',
    '!2100',
    '!2300',
    '!2301',
    '!2302',
    '!2303',
    '!2304',
    '!2307',
  ]
endif
if ipv6_opt.disabled()
  _disabled_tests += '!1614'
endif

_kwargs = {
  'args': [
    '-c',
    'exec perl "$@"',
    '--',
    '-I' + fs.as_posix(_srcdir),
    fs.as_posix(_srcdir / 'runtests.pl'),
    # Continue even if a test fails.
    '-a',
    # Use this curl executable.
    '-c',
    fs.as_posix(curl_exe.full_path()),
    # No valgrind.
    '-n',
    # Print log file contents on failure.
    '-p',
    # Verbose.
    # '-v',
    _disabled_tests,
  ],
  'depends': [curl_exe, libtest_targets, server_targets, unit_targets],
  'env': _env,
  'is_parallel': false,
  'timeout': 0,
  'workdir': _builddir,
}
if meson.version().version_compare('>=0.62.0')
  test(
    'runtests',
    bash_exe,
    kwargs: _kwargs + {
      'verbose': true,
    },
  )
else
  test(
    'runtests',
    bash_exe,
    kwargs: _kwargs,
  )
endif
