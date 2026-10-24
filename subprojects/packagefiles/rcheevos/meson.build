# Only rhash, rcheevos' ROM hashing, which is all PRGC Tracker needs: no
# runtime, no rc_client, no network layer.
project('rcheevos', 'c', default_options: ['c_std=gnu99'])

rcheevos_hash_lib = static_library('rcheevos_hash',
  files(
    'src/rc_compat.c',
    'src/rc_util.c',
    'src/rhash/aes.c',
    'src/rhash/cdreader.c',
    'src/rhash/hash.c',
    'src/rhash/hash_disc.c',
    'src/rhash/hash_encrypted.c',
    'src/rhash/hash_rom.c',
    'src/rhash/hash_zip.c',
    'src/rhash/md5.c',
  ),
  include_directories: include_directories('include', 'src'),
  c_args: ['-DRC_DISABLE_LUA', '-w'],
)

rcheevos_hash_dep = declare_dependency(
  include_directories: include_directories('include'),
  link_with: rcheevos_hash_lib,
)
