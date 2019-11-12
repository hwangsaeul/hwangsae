source_h = [
  'agent.h',
]

source_c = [
  'agent.c',
]

hwangsae_agent_c_args = [
  '-DG_LOG_DOMAIN="HWANGSAE-AGENT"',
  '-DHWANGSAE_COMPILATION',
]


hwangsae_agent = executable (
  'hwangsae-agent', 
  source_c,
  include_directories: hwangsae_incs,
  c_args: hwangsae_agent_c_args,
  dependencies: [ gobject_dep, gio_dep, libhwangsae_dbus_dep, libhwangsae_dep, chamge_dep],
  install: true
)
