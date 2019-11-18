agents = [
  'relay-agent',
  'recorder-agent',
]

hwangsae_agent_c_args = [
  '-DG_LOG_DOMAIN="HWANGSAE-AGENT"',
  '-DHWANGSAE_COMPILATION',
]

libcurl_req_version = '>= 7.0.0'

libcurl_dep = dependency('libcurl', version: libcurl_req_version)

foreach agent: agents
  exe_name = 'hwangsae-@0@'.format(agent)
  src_file = '@0@.c'.format(agent)

  executable(exe_name,
    src_file,
    install: true,
    include_directories: hwangsae_incs,
    c_args: [ hwangsae_agent_c_args, ],
    dependencies: [ libhwangsae_dbus_dep, libhwangsae_dep, chamge_dep,
                   gstreamer_dep, libcurl_dep],
  )

endforeach
