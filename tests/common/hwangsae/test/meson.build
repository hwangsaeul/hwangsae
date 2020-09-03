sources = [
  'test.c',
  'test-streamer.c',
]

headers = [
  'test-streamer.h',
]

libhwangsae_test_common = library(
  'hwangsae-test-common-@0@'.format(apiversion),
  sources,
  version: libversion,
  soversion: soversion,
  dependencies: [ glib_dep, gaeguli_dep, gstreamer_pbutils_dep ],
  c_args: test_c_args,
  install: true
)

install_headers(headers, subdir: join_paths(hwangsae_install_header_subdir, 'test'))

pkg.generate(libhwangsae_test_common,
  description : 'A SRT Relay and Recording Library (test ancillaries)',
)

libhwangsae_test_common_dep = declare_dependency(
  link_with: libhwangsae_test_common,
  include_directories: [ include_directories('../..') ],
)