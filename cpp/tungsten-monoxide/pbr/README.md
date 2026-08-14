# TungstenMonoxide PBR workspace

This is the editable PipelineEditor source for TungstenMonoxide's PBR package. `TungstenMonoxide.pipeline.yaml` starts from MassivePolyPusher's Shadows template and retains its directional shadow pass, RGBA16F scene target, and ACES tone-map presentation pass. `TungstenMonoxidePreview.scene.yaml` provides one representative model for every stable game binding.

The runtime artifact is `../resources/TungstenMonoxide.mpppackage`. Do not edit its ZIP contents by hand.

## Material and geometry contract

The workspace exports these bindings:

- `Ship.Surface`
- `Track.Asphalt`
- `Track.Rail`
- `Track.Mesh`
- `Track.Shell`
- `Track.Zone`
- `Track.Trigger`
- `Track.Fallback`

Every material expects position3/float32, normal3/float32, UV0/float32, normalized colour4/uint8, and tangent4/float32 in a single 52-byte buffer. `Ship.Surface` is indexed. Track materials are non-indexed. The preview scene deliberately uses `models/indexed-preview.mppmodel` for the ship and `models/flat-preview.mppmodel` for all track bindings; DemoSuite's package smoke test therefore exercises both forms rather than substituting PipelineEditor's float-colour built-in primitives.

The current package uses factor/colour-map materials with metallic 0–0.65 and roughness 0.42–0.8. Zone and trigger diagnostics have emissive factors. Maps use the required colour spaces: base-colour textures are sRGB, while the package currently has no linear normal, metallic-roughness, or occlusion maps. Texture wrapping is `REPEAT` to preserve tiled track UVs.

The pipeline declares `TungstenMonoxide.Environment` but intentionally omits production IBL maps. MPP consequently supplies its documented neutral environment fallback. This is the accepted first functional package; replacing it with authored HDR IBL is a later visual-quality task.

## Authoring and export

Build the matching MassivePolyPusher tools first:

```bat
cmake --build ext\massive-poly-pusher\build\cmake --config Release --target PipelineEditor DemoSuite ModelConvert --parallel
```

Open the committed workspace:

```bat
ext\massive-poly-pusher\build\cmake\bin\Release\PipelineEditor.exe cpp\tungsten-monoxide\pbr\TungstenMonoxide.pipeline.yaml
```

Use **File > Export Package...** and select:

```text
cpp/tungsten-monoxide/resources/TungstenMonoxide.mpppackage
```

The equivalent PipelineEditor automation command uses exactly the same exporter and is suitable for reproducibility checks:

```bat
ext\massive-poly-pusher\build\cmake\bin\Release\PipelineEditor.exe --export-package cpp\tungsten-monoxide\pbr\TungstenMonoxide.pipeline.yaml cpp\tungsten-monoxide\resources\TungstenMonoxide.mpppackage
```

## Validation

From the repository root:

```bat
ext\massive-poly-pusher\build\cmake\bin\Release\PipelineEditor.exe --validate cpp\tungsten-monoxide\pbr\TungstenMonoxide.pipeline.yaml
python cpp\tungsten-monoxide\pbr\validate_package.py cpp\tungsten-monoxide\resources\TungstenMonoxide.mpppackage
ext\massive-poly-pusher\build\cmake\bin\Release\DemoSuite.exe --package cpp\tungsten-monoxide\resources\TungstenMonoxide.mpppackage --package-smoke-test
```

`validate_package.py` is intentionally independent of MPP DLLs. It checks the standard store-only ZIP, version-1 manifest, package-local references, complete binding set, representative scene coverage, and exact indexed/non-indexed 52-byte material contracts. DemoSuite performs the authoritative parser, GPU resource, material compatibility, shadow, HDR, and presentation validation.

The `.obj`, `.mtl`, and `.modelspec.yaml` files under `models/` document the preview-model sources. The checked-in `.mppmodel` files are the source workspace's actual preview dependencies and are included by PipelineEditor's normal transitive package export.
