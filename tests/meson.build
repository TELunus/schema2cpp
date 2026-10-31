schema = files('data/person.schema.json')

# Runtime path: generated classes parse JSON text and throw on bad input.
person_hpp = custom_target(
  'person_hpp',
  input: schema,
  output: 'person.hpp',
  command: [schema2cpp_exe, '@INPUT@', '-o', '@OUTPUT@', '--name', 'Person'],
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
