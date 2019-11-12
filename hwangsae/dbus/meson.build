hwangsae_dbus_install_header_subdir = join_paths(hwangsae_install_header_subdir, 'dbus')

sources = [
]

dbus_ifaces = [
  [ 'Manager', 'manager-generated' ],
  [ 'EdgeInterface', 'edge-interface-generated' ],
]

dbus_headers = []

foreach iface: dbus_ifaces
  prefix = 'org.hwangsaeul.Hwangsae1.'
  name = prefix + iface[0] 
  dbus_sources = gnome.gdbus_codegen(
    iface[1],
    sources: name + '.xml',
    interface_prefix: prefix,
    namespace: 'HwangsaeDBus',
  )
  dbus_headers = dbus_sources[0]
  sources += dbus_sources[1]

endforeach

libhwangsae_dbus = shared_library(
  'hwangsae-dbus-@0@'.format(apiversion),
  sources, dbus_headers,
  version: libversion,
  soversion: soversion,
  include_directories: hwangsae_incs,
  dependencies: [ gio_dep ],
  c_args: hwangsae_c_args,
  link_args: common_ldflags,
  install: true
)

libhwangsae_dbus_dep = declare_dependency(link_with: libhwangsae_dbus,
  include_directories: [ hwangsae_incs ],
  dependencies: [ gio_dep ]
)
