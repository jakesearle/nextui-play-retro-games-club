project('prgc-tracker-pak', 'c',
  version: '1.0.0',
  default_options: [
    'warning_level=2',
    'werror=true'
  ]
)

# C11 for our own code only: as a project default it would also reach the
# subprojects, and OpenSSL does not build under strict C11. _DEFAULT_SOURCE
# brings back strncasecmp and friends.
add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')
c11 = ['c_std=c11']

apostrophe_dep = dependency('apostrophe', required: true)
curl_dep = dependency('libcurl', required: true,
  default_options: ['tool=disabled', 'tests=disabled', 'unittests=disabled'])
# libm is part of libc on macOS but separate on Linux
m_dep = meson.get_compiler('c').find_library('m', required: false)
# Unzipping zipped ROMs to hash them. The device already loads libz for curl.
zlib_dep = dependency('zlib', required: true)
rcheevos_dep = dependency('rcheevos_hash', required: true)

inc_dir = include_directories('src')

# Everything but the UI, shared with the tools.
club_sources = files(
  'src/club.c',
  'src/html.c',
  'src/io.c',
  'src/paths.c',
  'src/romzip.c',
  'src/roms.c',
)

executable('prgc-tracker',
  files('src/main.c', 'src/views.c', 'src/subtitle_list.c', 'src/launch.c') + club_sources,
  include_directories: inc_dir,
  dependencies: [apostrophe_dep, curl_dep, rcheevos_dep, zlib_dep, m_dep],
  override_options: c11,
  install: true
)

# Run the scraper and the ROM finder without the UI. Native builds only:
# they are development tools and never ship in the Pak.
if not meson.is_cross_build()
  foreach tool : ['scrape', 'findrom']
    executable(tool,
      files('tools/' + tool + '.c') + club_sources,
      include_directories: inc_dir,
      dependencies: [curl_dep, rcheevos_dep, zlib_dep],
      override_options: c11,
    )
  endforeach
endif
