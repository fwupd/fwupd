subdir('.libs')

_generated_srcs = {
  'lib1521.c': custom_target(
    'lib1521.c',
    capture: true,
    command: [
      bash_exe,
      '-c',
      'exec perl "$@"',
      '--',
      meson.current_source_dir() / 'mk-lib1521.pl',
    ],
    depend_files: 'mk-lib1521.pl',
    feed: true,
    input: meson.project_source_root() / 'include/curl/curl.h',
    output: 'lib1521.c',
  ),
}

_vars = {
  'CURL_NETWORK_LIBS': ['deps', []],
  'TESTUTIL_LIBS': ['deps', []],
}

foreach _spec : makefile_extractions['libtest']
  _name = ''
  _args = []
  _deps = []
  _incs = []
  _srcs = []
  foreach _part : _spec.strip().split()
    if _name == ''
      _name = _part
      continue
    endif
    if _part.startswith('@') and _part.endswith('@')
      _value = _vars.get(_part.substring(1, -1))
      if _value[0] == 'deps'
        _deps += _value[1]
      endif
    elif _part.startswith('-D')
      _args += _part
    elif _part.startswith('-I')
      _incs += include_directories(_part.substring(2))
    elif _part.endswith('.c')
      _srcs += (_part in _generated_srcs ? _generated_srcs.get(_part) : files(
        _part,
      )
)
    endif
  endforeach
  libtest_targets += executable(
    _name,
    c_args: _args,
    dependencies: [_deps, curl_ilib_dep, threads_dep],
    include_directories: _incs,
    sources: _srcs,
  )
endforeach
