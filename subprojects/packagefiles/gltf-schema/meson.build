project('gltf-schema', version: '2.0')

schema_dir = meson.current_source_dir() / 'specification/2.0/schema'
root_schema = files('specification/2.0/schema/glTF.schema.json')

gltf_schema_dep = declare_dependency(
    sources: root_schema,
    variables: {
        'schema_dir': schema_dir,
    },
)
