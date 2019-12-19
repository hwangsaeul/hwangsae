test_c_args = '-DG_LOG_DOMAIN="hwangsae-tests"'

subdir('common')

tests = [
  'test-recorder',
  'test-relay',
  'test-transmuxer',
]

env = environment()
env.set('G_TEST_SRCDIR', meson.current_source_dir())
env.set('G_TEST_BUILDDIR', meson.current_build_dir())

# Use uninstalled schema and dummy GSettings backend in tests
env.set('GSETTINGS_SCHEMA_DIR', hwangsae_schemas_dir)
env.set('GSETTINGS_BACKEND', 'memory')

foreach t: tests
  installed_test = '@0@.test'.format(t)

  exe = executable(
    t, ['@0@.c'.format(t), hwangsae_schemas],
    c_args: test_c_args,
    include_directories: hwangsae_incs,
    link_with: libhwangsae_test_common,
    dependencies: [ libhwangsae_dep, gaeguli_dep, gstreamer_pbutils_dep ],
    install: false,
  )

  test(
    t, exe,
    env: env,
    timeout: 120,
    is_parallel: false
  )
endforeach

debugenv = environment()
debugenv.set('GST_DEBUG', '3')
add_test_setup('debug', env: debugenv)
