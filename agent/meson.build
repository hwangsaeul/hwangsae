hwangsae_relay_agent_src = ['relay-agent.c']

hwangsae_recorder_agent_src = ['recorder-agent.c', 'http-server.c']

hwangsae_agent_c_args = [
  '-DG_LOG_DOMAIN="HWANGSAE-AGENT"',
  '-DHWANGSAE_COMPILATION',
]

executable('hwangsae-relay-agent',
  hwangsae_relay_agent_src,
  install: true,
  include_directories: hwangsae_incs,
  c_args: [ hwangsae_agent_c_args, ],
  dependencies: [ libhwangsae_dbus_dep, libhwangsae_dep, chamge_dep,
                 gstreamer_dep],)

executable('hwangsae-recorder-agent',
  hwangsae_recorder_agent_src,
  install: true,
  include_directories: hwangsae_incs,
  c_args: [ hwangsae_agent_c_args, ],
  dependencies: [ libhwangsae_dbus_dep, libhwangsae_dep, chamge_dep,
                 gstreamer_dep, soup_dep, json_glib_dep],)
