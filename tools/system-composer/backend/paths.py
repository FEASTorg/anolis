"""Path helpers for System Composer.

This module keeps repo-relative paths centralized so backend modules do not
duplicate hardcoded string literals or depend on the caller's CWD.
"""

from __future__ import annotations

import pathlib

BACKEND_DIR = pathlib.Path(__file__).resolve().parent
COMPOSER_DIR = BACKEND_DIR.parent
TOOLS_DIR = COMPOSER_DIR.parent
REPO_ROOT = TOOLS_DIR.parent

SYSTEMS_ROOT = REPO_ROOT / "systems"
TEMPLATES_ROOT = COMPOSER_DIR / "templates"
FRONTEND_DIR = COMPOSER_DIR / "frontend"
CATALOG_PATH = COMPOSER_DIR / "catalog" / "providers.json"
SYSTEM_SCHEMA_PATH = COMPOSER_DIR / "schema" / "system.schema.json"


def resolve_repo_path(path_value: str) -> pathlib.Path:
    """Resolve absolute/relative config paths against repo root."""
    path = pathlib.Path(path_value).expanduser()
    if path.is_absolute():
        return path
    return (REPO_ROOT / path).resolve()
