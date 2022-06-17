project('qt5-thread-test', 'cpp',
  license: 'LGPL-2.1+',
)
add_project_arguments('-fPIC', language: 'cpp')
qt5core = dependency('Qt5Core')
qt5concurrent = dependency('Qt5Concurrent')
glib2 = dependency('glib-2.0')
gio2 = dependency('gio-2.0')
fwupd = dependency('fwupd')
env = environment()
env.set('G_DEBUG', 'fatal-criticals')
e = executable(
  'qt-thread-test',
  sources: [
    'qt-thread-test.cpp'
  ],
  dependencies: [
    qt5core,
    qt5concurrent,
    glib2,
    gio2,
    fwupd,
  ],
)
test('qt-thread-test', e, timeout: 60, env: env)
