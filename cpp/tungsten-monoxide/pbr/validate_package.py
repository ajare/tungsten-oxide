#!/usr/bin/env python3
"""Validate the exported TungstenMonoxide PipelineEditor package without MPP DLLs."""

from __future__ import annotations

import argparse
from pathlib import PurePosixPath
from zipfile import ZIP_STORED, ZipFile
import xml.etree.ElementTree as ET

REQUIRED_BINDINGS = {
    "Ship.Surface",
    "Track.Asphalt",
    "Track.Rail",
    "Track.Mesh",
    "Track.Shell",
    "Track.Zone",
    "Track.Trigger",
    "Track.Fallback",
}
EXPECTED_CHANNELS = [
    ("position3", "float32", False),
    ("normal3", "float32", False),
    ("texcoord2", "float32", False),
    ("colour4", "uint8", True),
    ("tangent4", "float32", False),
]


def text(node: ET.Element, path: str) -> str:
    child = node.find(path)
    assert child is not None and child.text and child.text.strip(), f"missing {path}"
    return child.text.strip()


def local_path(value: str) -> PurePosixPath:
    path = PurePosixPath(value.replace("\\", "/"))
    assert not path.is_absolute(), f"absolute package path: {value}"
    assert ":" not in path.parts[0], f"drive-qualified package path: {value}"
    assert ".." not in path.parts, f"escaping package path: {value}"
    return path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("package")
    args = parser.parse_args()

    with ZipFile(args.package) as archive:
        entries = {item.filename: item for item in archive.infolist()}
        assert entries, "empty package"
        assert len(entries) == len(set(entries)), "duplicate package entry"
        for name, item in entries.items():
            local_path(name)
            assert item.compress_type == ZIP_STORED, f"non-store ZIP entry: {name}"

        manifest = ET.fromstring(archive.read("manifest.xml"))
        assert manifest.tag == "MppPackage" and manifest.attrib.get("version") == "1"
        pipeline_name = str(local_path(text(manifest, "Pipeline")))
        scene_name = str(local_path(text(manifest, "Scene")))
        assert pipeline_name in entries and scene_name in entries

        pipeline = ET.fromstring(archive.read(pipeline_name))
        scene = ET.fromstring(archive.read(scene_name))
        assert text(pipeline, "name") == "TungstenMonoxide.Pbr"
        assert text(scene, "environmentBinding") == "TungstenMonoxide.Environment"

        graph_images = {text(image, "name"): image for image in pipeline.findall("./RenderGraph/Images/Image")}
        assert text(graph_images["SceneHdr"], "format") == "RGBA16F"
        assert text(graph_images["Presentation"], "format") == "RGBA8"
        assert text(graph_images["Presentation"], "import") == "screen"
        assert text(graph_images["Presentation"], "external") == "true"
        graph_passes = {text(render_pass, "name"): text(render_pass, "factory") for render_pass in pipeline.findall("./RenderGraph/Passes/Pass")}
        assert graph_passes == {
            "ShadowDepth": "MPP.ShadowDepth",
            "PbrScene": "MPP.PbrScene",
            "ToneMapPresentation": "MPP.ToneMapPresent",
        }
        output = pipeline.find("./Outputs/Output")
        assert output is not None and text(output, "image") == "Presentation"
        assert text(output, "AntiAliasing/taa") == "false", "TAA must remain disabled until camera cuts are integrated"
        shadow_lights = [
            light
            for light in scene.findall("./Lights/Light")
            if text(light, "castsShadows") == "true"
        ]
        assert len(shadow_lights) == 1, "the live scene requires exactly one package-authored shadow light"

        materials = {}
        for material in pipeline.findall("./LocalResources/PbrMaterial"):
            name = text(material, "name")
            assert name not in materials, f"duplicate PBR material: {name}"
            materials[name] = material
        assert set(materials) == REQUIRED_BINDINGS, "PBR material set differs from required bindings"

        bindings = {}
        for binding in pipeline.findall("./PreviewBindings/Material"):
            logical = text(binding, "binding")
            resource = text(binding, "resource")
            assert logical not in bindings, f"duplicate preview binding: {logical}"
            bindings[logical] = resource
        assert set(bindings) == REQUIRED_BINDINGS
        assert all(name == resource for name, resource in bindings.items())

        for name, material in materials.items():
            specification = material.find("MeshSpecification")
            assert specification is not None
            indexed = text(specification, "indexed") == "true"
            assert indexed == (name == "Ship.Surface"), f"unexpected indexed state for {name}"
            channels = []
            for channel in specification.findall("./Buffer/Channel"):
                channels.append((text(channel, "data"), text(channel, "type"), text(channel, "normalised") == "true" if channel.find("normalised") is not None else False))
            assert channels == EXPECTED_CHANNELS, f"unexpected 52-byte vertex contract for {name}: {channels}"

        scene_bindings = [text(model, "materialBinding") for model in scene.findall("./Models/Model")]
        assert set(scene_bindings) == REQUIRED_BINDINGS
        assert len(scene_bindings) == len(REQUIRED_BINDINGS), "each binding must have one representative model"

        referenced_files = []
        for document in (pipeline, scene):
            for element in document.iter():
                if element.tag in {"file", "filename"} and element.text and element.text.strip():
                    referenced_files.append(str(local_path(element.text.strip())))
        missing = sorted(path for path in referenced_files if path not in entries)
        assert not missing, f"package references missing files: {missing}"

        model_files = [text(model, "file") for model in scene.findall("./Models/Model")]
        indexed_models = [path for path in model_files if path.endswith("indexed-preview.mppmodel")]
        flat_models = [path for path in model_files if path.endswith("flat-preview.mppmodel")]
        assert len(indexed_models) == 1 and len(flat_models) == 7

    print(f"Validated {args.package}: 8 bindings, 52-byte indexed/non-indexed previews, self-contained paths")


if __name__ == "__main__":
    main()
