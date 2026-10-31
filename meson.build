project(
  'schema2cpp', 'cpp',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>=0.59',
  default_options: ['cpp_std=c++17', 'warning_level=2'],
)

src_inc   = include_directories('src')

# wuffs meson subproject
wuffs_proj = subproject('wuffs')
wuffs_inc = wuffs_proj.get_variable('wuffs_inc')

# Runtime: JSON parsing (Wuffs) + validation helpers used by generated classes.
# Wuffs is a big C file, so silence warnings for this one target.
s2c_runtime_lib = static_library(
  's2c_runtime', 'src/s2c_runtime.cpp',
  include_directories: [src_inc, wuffs_inc],
  override_options: ['warning_level=0'],
)

# Link this (and its include dir) to *parse* JSON text at run time.
s2c_runtime_dep = declare_dependency(
  link_with: s2c_runtime_lib,
  include_directories: src_inc,
)

# Include dir only: enough for programs that just use embedded data (--data),
# which need no parser at run time.
s2c_headers_dep = declare_dependency(include_directories: src_inc)

# The generator runs on the build machine when cross-compiling.
if meson.is_cross_build()
  s2c_runtime_native = static_library(
    's2c_runtime_native', 'src/s2c_runtime.cpp',
    include_directories: [src_inc, wuffs_inc],
    override_options: ['warning_level=0'],
    native: true,
  )
else
  s2c_runtime_native = s2c_runtime_lib
endif

schema2cpp_exe = executable(
  'schema2cpp', 'src/schema2cpp.cpp',
  include_directories: src_inc,
  link_with: s2c_runtime_native,
  native: true,
  install: true,
)

# Make everything available to projects that use us as a subproject / wrap.
meson.override_find_program('schema2cpp', schema2cpp_exe)
meson.override_dependency('s2c_runtime', s2c_runtime_dep)
meson.override_dependency('s2c_headers', s2c_headers_dep)

if not meson.is_subproject()
  subdir('tests')
endif
