#!/usr/bin/env python3
"""
Telemetry timeseries contract validator.

Validates fixture payloads against the telemetry timeseries JSON schema.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit("ERROR: missing dependency 'pyyaml' (pip install pyyaml)") from exc

try:
    import jsonschema
except ImportError as exc:  # pragma: no cover
    raise SystemExit("ERROR: missing dependency 'jsonschema' (pip install jsonschema)") from exc


@dataclass
class Failure:
    fixture: Path
    message: str


def _repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[3]


def _load_yaml(path: Path) -> dict[str, Any]:
    try:
        raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise SystemExit(f"ERROR: failed to parse YAML '{path}': {exc}") from exc

    if not isinstance(raw, dict):
        raise SystemExit(f"ERROR: manifest '{path}' must be a mapping")
    return raw


def _load_json(path: Path) -> dict[str, Any]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise RuntimeError(f"json parse failed: {exc}") from exc

    if not isinstance(raw, dict):
        raise RuntimeError("fixture root must be a JSON object")
    return raw


def _build_validator(schema_path: Path) -> jsonschema.Validator:
    try:
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise SystemExit(f"ERROR: failed to parse schema '{schema_path}': {exc}") from exc

    schema_draft = schema.get("$schema")
    expected_draft = "http://json-schema.org/draft-07/schema#"
    if schema_draft != expected_draft:
        raise SystemExit(
            f"ERROR: telemetry schema draft mismatch. Expected '{expected_draft}', found '{schema_draft}'."
        )

    validator_cls = jsonschema.validators.validator_for(schema)
    try:
        validator_cls.check_schema(schema)
    except jsonschema.SchemaError as exc:
        raise SystemExit(f"ERROR: schema meta-validation failed: {exc}") from exc

    if validator_cls is not jsonschema.Draft7Validator:
        raise SystemExit("ERROR: telemetry schema must validate with Draft7Validator in this wave.")

    return validator_cls(schema)


def _example_errors(validator: jsonschema.Validator, payload: dict[str, Any]) -> list[str]:
    errors = sorted(validator.iter_errors(payload), key=lambda e: list(e.path))
    rendered: list[str] = []
    for err in errors:
        path = "$" if not err.path else "$." + ".".join(str(part) for part in err.path)
        rendered.append(f"{path}: {err.message}")
    return rendered


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate telemetry timeseries contract fixtures.")
    parser.add_argument(
        "--repo-root",
        default=str(_repo_root_from_script()),
        help="Path to anolis repository root (default: auto-detected).",
    )
    parser.add_argument(
        "--manifest",
        default="tests/contracts/telemetry-timeseries/examples/manifest.yaml",
        help="Manifest path relative to repo root.",
    )
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    manifest_path = (repo_root / args.manifest).resolve()
    if not manifest_path.is_file():
        print(f"ERROR: manifest file not found: {manifest_path}", file=sys.stderr)
        return 1

    manifest = _load_yaml(manifest_path)
    schema_rel = manifest.get("schema")
    if not isinstance(schema_rel, str) or not schema_rel.strip():
        print("ERROR: manifest must define non-empty string field 'schema'", file=sys.stderr)
        return 1

    schema_path = (repo_root / schema_rel).resolve()
    if not schema_path.is_file():
        print(f"ERROR: schema file not found: {schema_path}", file=sys.stderr)
        return 1

    examples = manifest.get("examples")
    if not isinstance(examples, list) or not examples:
        print("ERROR: manifest field 'examples' must be a non-empty array", file=sys.stderr)
        return 1

    validator = _build_validator(schema_path)
    failures: list[Failure] = []
    seen: set[Path] = set()

    for idx, example in enumerate(examples, start=1):
        if not isinstance(example, dict):
            failures.append(Failure(manifest_path, f"examples[{idx}] must be a mapping"))
            continue

        file_rel = example.get("file")
        expect = example.get("expect")
        if not isinstance(file_rel, str) or not file_rel.strip():
            failures.append(Failure(manifest_path, f"examples[{idx}] missing non-empty 'file'"))
            continue
        if expect not in {"valid", "invalid"}:
            failures.append(Failure(manifest_path, f"examples[{idx}] has invalid expect='{expect}'"))
            continue

        fixture_path = (repo_root / file_rel).resolve()
        if fixture_path in seen:
            failures.append(Failure(fixture_path, "fixture listed more than once in manifest"))
            continue
        seen.add(fixture_path)

        if not fixture_path.is_file():
            failures.append(Failure(fixture_path, "fixture file not found"))
            continue

        try:
            payload = _load_json(fixture_path)
        except RuntimeError as exc:
            failures.append(Failure(fixture_path, str(exc)))
            continue

        errors = _example_errors(validator, payload)
        if expect == "valid" and errors:
            failures.append(Failure(fixture_path, "expected valid but failed: " + " | ".join(errors)))
        if expect == "invalid" and not errors:
            failures.append(Failure(fixture_path, "expected invalid but passed schema validation"))

    print("telemetry-timeseries contract validation summary")
    print(f"  manifest: {manifest_path}")
    print(f"  schema:   {schema_path}")
    print(f"  examples: {len(examples)}")

    if failures:
        print("\nFAILURES:")
        for failure in failures:
            print(f"  - {failure.fixture}: {failure.message}")
        return 1

    print("\nAll telemetry-timeseries fixtures validated successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
