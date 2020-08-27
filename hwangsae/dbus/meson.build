hwangsae_dbus_install_header_subdir = join_paths(hwangsae_install_header_subdir, 'dbus')

sources = [
]

dbus_ifaces = [
  [ 'Manager', 'manager-generated' ],
  [ 'EdgeInterface', 'edge-interface-generated' ],
  [ 'RecorderInterface', 'recorder-interface-generated' ],
]

dbus_configs = [
  'org.hwangsaeul.Hwangsae1.RecorderAgent.conf',
]

foreach iface: dbus_ifaces
  prefix = 'org.hwangsaeul.Hwangsae1.'
  name = prefix + iface[0] 

  if meson.version().version_compare('>= 0.46.0')

    sources += gnome.gdbus_codegen(
      iface[1],
      sources: name + '.xml',
      interface_prefix: prefix,
      namespace: 'Hwangsae1DBus',
    )

  else
    sources += custom_target(
      iface[1],
      input: name + '.xml',
      output: [iface[1] + '.h', iface[1] + '.c'],
      command: [
        find_program('codegen.py'), iface[0], iface[1],
        meson.current_build_dir(), '@INPUT@'
      ]
    )
  endif

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
