"""Unit tests for deterministic handoff package export core."""

from __future__ import annotations

import io
import json
import pathlib
import sys
import zipfile

import yaml

_WB_DIR = pathlib.Path(__file__).resolve().parents[2]
_REPO_ROOT = _WB_DIR.parent.parent
_EXPORTER_DIR = _WB_DIR / "backend"
if str(_EXPORTER_DIR) not in sys.path:
    sys.path.insert(0, str(_EXPORTER_DIR))

import exporter  # noqa: E402


def test_build_package_is_deterministic_and_rewrites_runtime_paths(tmp_path: pathlib.Path) -> None:
    project_dir = _make_project(tmp_path, name="export-deterministic")
    out_a = tmp_path / "a.anpkg"
    out_b = tmp_path / "b.anpkg"

    exporter.build_package(project_dir=project_dir, out_path=out_a)
    exporter.build_package(project_dir=project_dir, out_path=out_b)

    data_a = out_a.read_bytes()
    data_b = out_b.read_bytes()
    assert data_a == data_b

    with zipfile.ZipFile(io.BytesIO(data_a), mode="r") as archive:
        members = sorted(archive.namelist())
        assert members == sorted(
            [
                "machine-profile.yaml",
                "meta/checksums.sha256",
                "meta/provenance.json",
                "providers/sim0.yaml",
                "runtime/anolis-runtime.yaml",
                "runtime/behaviors/local.xml",
            ]
        )

        runtime_payload = yaml.safe_load(archive.read("runtime/anolis-runtime.yaml"))
        assert runtime_payload["providers"][0]["args"] == ["--config", "providers/sim0.yaml"]
        assert runtime_payload["automation"]["behavior_tree"] == "runtime/behaviors/local.xml"
        assert "token" not in runtime_payload.get("telemetry", {}).get("influxdb", {})
        assert "influx_token" not in runtime_payload.get("telemetry", {})

        machine_profile = yaml.safe_load(archive.read("machine-profile.yaml"))
        assert machine_profile["runtime_profiles"]["manual"] == "runtime/anolis-runtime.yaml"
        assert machine_profile["providers"]["sim0"]["config"] == "providers/sim0.yaml"
        assert machine_profile["behaviors"] == ["runtime/behaviors/local.xml"]

        provenance = json.loads(archive.read("meta/provenance.json").decode("utf-8"))
        assert provenance["exported_at"] == "2026-04-16T19:01:02Z"
        assert provenance["package_format_version"] == 1
        assert provenance["source_project"] == "export-deterministic"


def _make_project(tmp_path: pathlib.Path, *, name: str) -> pathlib.Path:
    template_path = _REPO_ROOT / "tools" / "system-composer" / "templates" / "sim-quickstart" / "system.json"
    system = json.loads(template_path.read_text(encoding="utf-8"))

    project_dir = tmp_path / name
    project_dir.mkdir(parents=True, exist_ok=True)

    system["meta"]["name"] = name
    system["meta"]["created"] = "2026-04-16T19:01:02.999999+00:00"
    system["topology"]["runtime"]["telemetry"] = {
        "enabled": True,
        "influxdb": {
            "url": "http://localhost:8086",
            "org": "anolis",
            "bucket": "anolis",
            "token": "super-secret",
        },
    }
    system["topology"]["runtime"]["automation_enabled"] = True
    system["topology"]["runtime"]["behavior_tree_path"] = "behaviors/local.xml"

    behavior_dir = project_dir / "behaviors"
    behavior_dir.mkdir(parents=True, exist_ok=True)
    (behavior_dir / "local.xml").write_text("<root />\n", encoding="utf-8")

    (project_dir / "system.json").write_text(json.dumps(system, indent=2), encoding="utf-8")
    return project_dir
