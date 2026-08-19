#!/usr/bin/env python3
"""Validate the exported TungstenMonoxide PipelineEditor package without MPP DLLs."""

from __future__ import annotations

import argparse
import json
from pathlib import PurePosixPath
from zipfile import ZIP_STORED, ZipFile
import xml.etree.ElementTree as ET

REQUIRED_BINDINGS = {"Ship.Surface", "Track.Asphalt", "Track.Rail", "Track.Mesh", "Track.Shell", "Track.Zone", "Track.Trigger", "Track.Fallback"}
EXPECTED_CHANNELS = [("position3", "float32", False), ("normal3", "float32", False), ("texcoord2", "float32", False),
                     ("colour4", "uint8", True), ("tangent4", "float32", False)]


def parse_scalar(value: str) -> str:
    value = value.strip()
    if value.startswith(('"', "'")):
        return str(json.loads(value)) if value.startswith('"') else value[1:-1]
    return value


def parse_yaml(data: bytes) -> dict:
    """Parse the mapping/sequence/scalar subset emitted by MPP's YamlWriter."""
    lines = []
    for raw in data.decode("utf-8").splitlines():
        text = raw.rstrip()
        if not text.strip() or text.lstrip().startswith("#"):
            continue
        lines.append((len(text) - len(text.lstrip(" ")), text.lstrip(" ")))

    def block(index: int, indent: int):
        is_list = lines[index][1].startswith("-")
        result = [] if is_list else {}
        while index < len(lines) and lines[index][0] == indent and lines[index][1].startswith("-") == is_list:
            text = lines[index][1]
            if is_list:
                item = text[1:].strip()
                if not item:
                    value, index = block(index + 1, lines[index + 1][0])
                    result.append(value)
                    continue
                key, separator, scalar = item.partition(":")
                if separator:
                    node = {}
                    if scalar.strip():
                        node[key] = parse_scalar(scalar)
                        index += 1
                    else:
                        value, index = block(index + 1, lines[index + 1][0])
                        node[key] = value
                    if index < len(lines) and lines[index][0] > indent:
                        extra, index = block(index, lines[index][0])
                        node.update(extra)
                    result.append(node)
                else:
                    result.append(parse_scalar(item))
                    index += 1
            else:
                key, separator, scalar = text.partition(":")
                assert separator, f"invalid YAML line: {text}"
                if scalar.strip():
                    result[key] = parse_scalar(scalar)
                    index += 1
                else:
                    assert index + 1 < len(lines) and lines[index + 1][0] > indent, f"empty YAML mapping: {key}"
                    result[key], index = block(index + 1, lines[index + 1][0])
        return result, index

    document, end = block(0, lines[0][0])
    assert end == len(lines)
    return document


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


def items(value):
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("package")
    args = parser.parse_args()

    with ZipFile(args.package) as archive:
        entries = {item.filename: item for item in archive.infolist()}
        assert entries and len(entries) == len(set(entries)), "empty or duplicate package entries"
        for name, item in entries.items():
            local_path(name)
            assert item.compress_type == ZIP_STORED, f"non-store ZIP entry: {name}"

        manifest = ET.fromstring(archive.read("manifest.xml"))
        assert manifest.tag == "MppPackage" and manifest.attrib.get("version") == "1"
        pipeline_name = str(local_path(text(manifest, "Pipeline")))
        scene_name = str(local_path(text(manifest, "Scene")))
        assert pipeline_name in entries and scene_name in entries
        assert pipeline_name.endswith(".yaml") and scene_name.endswith(".yaml")

        pipeline = parse_yaml(archive.read(pipeline_name))["PbrPipeline"]
        scene = parse_yaml(archive.read(scene_name))["Scene"]
        assert pipeline["name"] == "TungstenMonoxide.Pbr"
        assert scene["environmentBinding"] == "TungstenMonoxide.Environment"

        graph_images = {image["name"]: image for image in pipeline["RenderGraph"]["Images"]}
        assert graph_images["SceneHdr"]["format"] == "RGBA16F"
        assert graph_images["Presentation"]["format"] == "RGBA8"
        assert graph_images["Presentation"]["import"] == "screen" and graph_images["Presentation"]["external"] == "true"
        graph_passes = {render_pass["name"]: render_pass["factory"] for render_pass in pipeline["RenderGraph"]["Passes"]}
        assert graph_passes == {"ShadowDepth": "MPP.ShadowDepth", "PbrScene": "MPP.PbrScene", "ToneMapPresentation": "MPP.FullscreenEffect"}
        tone_map = next(render_pass for render_pass in pipeline["RenderGraph"]["Passes"] if render_pass["name"] == "ToneMapPresentation")
        assert tone_map["program"] == "PostEffect.ToneMap"
        post_effect = pipeline["LocalResources"]["PostEffectMaterial"]
        assert post_effect["name"] == "PostEffect.ToneMap" and post_effect["Program"]["Ref"] == "__mpp_p2d_tonemap__"
        output = items(pipeline["Outputs"])[0]
        assert output["image"] == "Presentation" and output["AntiAliasing"]["taa"] == "false"
        assert sum(light["castsShadows"] == "true" for light in scene["Lights"]) == 1

        materials = {material["name"]: material for material in items(pipeline["LocalResources"]["PbrMaterial"])}
        assert set(materials) == REQUIRED_BINDINGS
        bindings = {binding["binding"]: binding["resource"] for binding in pipeline["PreviewBindings"]}
        assert set(bindings) == REQUIRED_BINDINGS and all(name == resource for name, resource in bindings.items())

        for name, material in materials.items():
            specification = material["MeshSpecification"]
            assert (specification["indexed"] == "true") == (name == "Ship.Surface")
            channels = [(channel["data"], channel["type"], channel.get("normalised") == "true") for channel in specification["Buffer"]]
            assert channels == EXPECTED_CHANNELS, f"unexpected 52-byte vertex contract for {name}: {channels}"

        scene_bindings = [model["materialBinding"] for model in scene["Models"]]
        assert set(scene_bindings) == REQUIRED_BINDINGS and len(scene_bindings) == len(REQUIRED_BINDINGS)

        referenced_files = []
        def references(value):
            if isinstance(value, dict):
                for key, child in value.items():
                    if key in {"file", "filename"} and isinstance(child, str):
                        referenced_files.append(str(local_path(child)))
                    else:
                        references(child)
            elif isinstance(value, list):
                for child in value:
                    references(child)
        references(pipeline)
        references(scene)
        missing = sorted(path for path in referenced_files if path not in entries)
        assert not missing, f"package references missing files: {missing}"

        model_files = [model["file"] for model in scene["Models"]]
        assert sum(path.endswith("indexed-preview.mppmodel") for path in model_files) == 1
        assert sum(path.endswith("flat-preview.mppmodel") for path in model_files) == 7

    print(f"Validated {args.package}: YAML pipeline/scene, 8 bindings, self-contained paths")


if __name__ == "__main__":
    main()
