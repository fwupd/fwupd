_vars = {
  'CURL_NETWORK_AND_TIME_LIBS': ['deps', sys_deps],
}

foreach _spec : makefile_extractions['server']
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
      _srcs += _part
    endif
  endforeach
  server_targets += executable(
    _name,
    _srcs,
    c_args: ['-DCURL_STATICLIB', _args],
    dependencies: [_deps, curl_iflags_dep],
    include_directories: [include_directories('../../src'), _incs],
  )
endforeach
