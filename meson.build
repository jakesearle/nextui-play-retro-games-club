project('prgc-tracker-pak', 'c',
  version: '1.0.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=true'
  ]
)

# strncasecmp and friends under -std=c11.
add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')

apostrophe_dep = dependency('apostrophe', required: true)
curl_dep = dependency('libcurl', required: true,
  default_options: ['tool=disabled', 'tests=disabled', 'unittests=disabled'])
# libm is part of libc on macOS but separate on Linux
m_dep = meson.get_compiler('c').find_library('m', required: false)

inc_dir = include_directories('src')

# Everything but the UI, shared with the scrape tool.
club_sources = files(
  'src/club.c',
  'src/html.c',
  'src/paths.c',
)

executable('prgc-tracker',
  files('src/main.c', 'src/views.c', 'src/subtitle_list.c') + club_sources,
  include_directories: inc_dir,
  dependencies: [apostrophe_dep, curl_dep, m_dep],
  install: true
)

# Runs the scraper without the UI. Native builds only: it is a development
# tool and never ships in the Pak.
if not meson.is_cross_build()
  executable('scrape',
    files('tools/scrape.c') + club_sources,
    include_directories: inc_dir,
    dependencies: [curl_dep],
  )
endif
