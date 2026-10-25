# Only the C library: the repo also holds the same generator in other
# languages, demos and tests.
project('qrcodegen', 'c', default_options: ['c_std=c99'])

qrcodegen_lib = static_library('qrcodegen', files('c/qrcodegen.c'),
  include_directories: include_directories('c'),
  c_args: ['-w'],
)

qrcodegen_dep = declare_dependency(
  include_directories: include_directories('c'),
  link_with: qrcodegen_lib,
)
