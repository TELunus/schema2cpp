project('gltf-generated-assets', version: '0.6.1')

assets_dir = meson.current_source_dir()

gltf_generated_assets_dep = declare_dependency(
    variables: {
        'assets_dir': assets_dir,
        'positive_dir': assets_dir / 'Positive',
        'negative_dir': assets_dir / 'Negative',
        'manifest': assets_dir / 'Manifest.json',
    },
)