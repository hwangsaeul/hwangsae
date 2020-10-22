pkg = import('pkgconfig')

hwangsae_install_header_subdir = join_paths(hwangsae_api_name, 'hwangsae')

configure_file(output: 'config.h', configuration: cdata)

source_h = [
  'hwangsae.h',
  'recorder.h',
  'relay.h',
  'transmuxer.h',
  'types.h',
]

source_c = [
  'recorder.c',
  'relay.c',
  'transmuxer.c',
  'types.c',
  'common.c'
]

gsettings_schemas = [
  'org.hwangsaeul.hwangsae.gschema.xml',
]

install_headers(source_h, subdir: hwangsae_install_header_subdir)

hwangsae_c_args = [
  '-DG_LOG_DOMAIN="HWANGSAE"',
  '-DHWANGSAE_COMPILATION',
]

hwangsae_enums = gnome.mkenums_simple(
  'enumtypes',
  header_prefix: '#include <hwangsae/types.h>',
  sources: source_h,
  decorator: 'HWANGSAE_API_EXPORT',
  install_dir: join_paths(get_option('includedir'), hwangsae_install_header_subdir),
  install_header: true,
)

hwangsae_enums_h = hwangsae_enums[1]
hwangsae_enums_c = hwangsae_enums[0]

subdir('dbus')

libhwangsae = library(
  'hwangsae-@0@'.format(apiversion),
  hwangsae_enums, source_c,
  version: libversion,
  soversion: soversion,
  include_directories: hwangsae_incs,
  dependencies: [ gstreamer_dep, gio_dep, libsrt_dep,libhwangsae_dbus_dep ],
  c_args: hwangsae_c_args,
  link_args: common_ldflags,
  install: true
)

pkg.generate(libhwangsae,
  libraries : [ gaeguli_dep, libsrt_dep ],
  name : meson.project_name(),
  description : 'A SRT Relay and Recording Library',
  filebase : hwangsae_api_name,
  subdirs: hwangsae_api_name,
  variables: 'exec_prefix=${prefix}'
)

libhwangsae_dep = declare_dependency(link_with: libhwangsae,
  include_directories: [ hwangsae_incs ],
  dependencies: [ gobject_dep ],
  sources: [ hwangsae_enums_h ],
)

install_data(gsettings_schemas,
  install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas'))

# Used only in the test suite
gnome = import('gnome')
hwangsae_schemas = gnome.compile_schemas(depend_files: gsettings_schemas)
hwangsae_schemas_dir = meson.current_build_dir()


