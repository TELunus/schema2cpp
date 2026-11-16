# glTF schema: generate a class from the Khronos glTF 2.0 JSON Schema and use
# the Khronos-generated reference assets as test cases.
gltf_schema_proj = subproject('gltf-schema')
gltf_schema_dep = gltf_schema_proj.get_variable('gltf_schema_dep')
gltf_schema_dir = gltf_schema_dep.get_variable('schema_dir')
gltf_root_schema = gltf_schema_proj.get_variable('root_schema')

gltf_assets_proj = subproject('gltf-generated-assets')
gltf_assets_dep = gltf_assets_proj.get_variable('gltf_generated_assets_dep')
gltf_positive_dir = gltf_assets_dep.get_variable('positive_dir')
gltf_negative_dir = gltf_assets_dep.get_variable('negative_dir')

gltf_hpp = custom_target(
  'gltf_hpp',
  input: gltf_root_schema,
  output: 'gltf.hpp',
  command: [schema2cpp_exe, '@INPUT@', '-o', '@OUTPUT@', '--name', 'GlTF',
            '--schema-dir', gltf_schema_dir],
)

test('glTF generated assets round-trip and invalid documents are rejected',
  executable('test_gltf', 'test_gltf.cpp', gltf_hpp, dependencies: s2c_runtime_dep),
  args: [gltf_positive_dir, gltf_negative_dir])

schema = files('data/person.schema.json')

# Runtime path: generated classes parse JSON text and throw on bad input.
person_hpp = custom_target(
  'person_hpp',
  input: schema,
  output: 'person.hpp',
  command: [schema2cpp_exe, '@INPUT@', '-o', '@OUTPUT@', '--name', 'Person'],
)

settings_hpp = custom_target(
  'settings_hpp',
  input: files('data/settings.schema.json'),
  output: 'settings.hpp',
  command: [schema2cpp_exe, '@INPUT@', '-o', '@OUTPUT@', '--name', 'Settings', '--namespace', 'settings'],
)

# Embedded path: data is checked at generation time and compiled in.
embedded_hpp = custom_target(
  'person_embedded_hpp',
  input: [schema, files('data/person.json')],
  output: 'person_embedded.hpp',
  command: [schema2cpp_exe, '@INPUT0@', '--data', '@INPUT1@', '-o', '@OUTPUT@',
            '--name', 'Person', '--namespace', 'embedded', '--data-name', 'sample'],
)

test('generated class validates JSON',
  executable('test_runtime', 'test_runtime.cpp', person_hpp, dependencies: s2c_runtime_dep))

test('update() merges into existing JSON',
  executable('test_update', 'test_update.cpp', person_hpp, settings_hpp, dependencies: s2c_runtime_dep))

test('parser edge cases',
  executable('test_parser', 'test_parser.cpp', dependencies: s2c_runtime_dep))

# Uses only the headers: no parser is linked, proving embedded data needs none.
test('embedded data',
  executable('test_embedded', 'test_embedded.cpp', embedded_hpp, dependencies: s2c_headers_dep))

test('generator rejects data that does not match the schema',
  schema2cpp_exe,
  args: [schema, '--data', files('data/bad_person.json'),
         '-o', meson.current_build_dir() / 'unused.hpp'],
  should_fail: true)
