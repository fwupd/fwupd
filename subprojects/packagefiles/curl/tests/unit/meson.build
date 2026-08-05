_deps = {
  'unit1394': [curl_ilib_dep, curltool_dep],
  'unit1621': [curl_ilib_dep, curltool_dep],
}

foreach _spec : makefile_extractions['unit']
  _spec = _spec.split()
  _name = _spec[0]
  unit_targets += executable(
    _spec,
    c_args: curlu_flags,
    dependencies: [curl_iflags_dep, _deps.get(_name, [curltool_dep, curlu_dep])],
    include_directories: include_directories('../../src', '../libtest'),
  )
endforeach
