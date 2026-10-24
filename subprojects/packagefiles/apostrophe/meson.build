project('apostrophe', 'c')
sdl2       = dependency('sdl2')
sdl2_ttf   = dependency('SDL2_ttf')
sdl2_image = dependency('SDL2_image')
threads    = dependency('threads')
apostrophe_dep = declare_dependency(
  include_directories: include_directories('include'),
  dependencies: [sdl2, sdl2_ttf, sdl2_image, threads],
)
