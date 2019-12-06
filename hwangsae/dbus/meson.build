hwangsae_dbus_install_header_subdir = join_paths(hwangsae_install_header_subdir, 'dbus')

sources = [
]

dbus_ifaces = [
  [ 'Manager', 'manager-generated' ],
  [ 'EdgeInterface', 'edge-interface-generated' ],
  [ 'RecorderInterface', 'recorder-interface-generated' ],
]

dbus_configs = [
  'org.hwangsaeul.Hwangsae1.RelayAgent.conf',
  'org.hwangsaeul.Hwangsae1.RecorderAgent.conf',
]

foreach iface: dbus_ifaces
  prefix = 'org.hwangsaeul.Hwangsae1.'
  name = prefix + iface[0] 

  sources += gnome.gdbus_codegen(
    iface[1],
    sources: name + '.xml',
    interface_prefix: prefix,
    namespace: 'Hwangsae1DBus',
  )
endforeach

libhwangsae_dbus = shared_library(
  'hwangsae-dbus-@0@'.format(apiversion),
  sources,
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

foreach config: dbus_configs
  install_data(
    config,
    install_dir : join_paths(get_option('datadir'), 'dbus-1', 'system.d')
  )
endforeach