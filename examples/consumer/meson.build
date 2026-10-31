project('consumer', 'cpp', default_options: ['cpp_std=c++17'])

schema2cpp = find_program('schema2cpp')   # built from the wrap if not installed
schema = files('schema/person.schema.json')

# Mode 1: schema known at build time, JSON file loaded at run time.
person_hpp = custom_target('person_hpp',
  input: schema, output: 'person.hpp',
  command: [schema2cpp, '@INPUT@', '-o', '@OUTPUT@', '--name', 'Person'])

executable('load_at_runtime', 'src/load.cpp', person_hpp,
  dependencies: dependency('s2c_runtime'))

# Mode 2: JSON checked at build time and compiled into the program.
config_hpp = custom_target('config_hpp',
  input: [schema, files('data/person.json')], output: 'config.hpp',
  command: [schema2cpp, '@INPUT0@', '--data', '@INPUT1@', '-o', '@OUTPUT@',
            '--name', 'Person', '--data-name', 'config'])

executable('embedded', 'src/embedded.cpp', config_hpp,
  dependencies: dependency('s2c_headers'))
