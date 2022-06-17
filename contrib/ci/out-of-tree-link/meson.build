  project('out-of-tree-link', 'c',
  license: 'LGPL-2.1+',
)
fwupd = dependency('fwupd')
fwupdplugin = dependency('fwupdplugin')
env = environment()
env.set('G_DEBUG', 'fatal-criticals')
e = executable(
  'fwupd',
  sources: [
    'fwupd.c'
  ],
  dependencies: [
    fwupd
  ],
  )
test('fwupd', e, timeout: 60, env: env)
e = executable(
  'fwupdplugin',
  sources: [
    'fwupdplugin.c'
  ],
  dependencies: [
    fwupdplugin
  ],
  )
test('fwupdplugin', e, timeout: 60, env: env)
