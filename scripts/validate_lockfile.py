#!/usr/bin/env python3
"""
Validate requirements-lock.txt encoding and integrity.

This script ensures the lock file is:
1. Valid UTF-8 (not UTF-16 from Windows PowerShell redirection)
2. Contains no NUL bytes (binary corruption check)

Exit Codes:
    0: Valid
    1: Invalid encoding or contains NUL bytes
"""
import sys
from pathlib import Path


def validate_lockfile(path: str) -> bool:
    """Validate lock file encoding and integrity."""
    try:
        content = Path(path).read_bytes()
        
        # Check UTF-8 encoding
        try:
            content.decode('utf-8')
        except UnicodeDecodeError as e:
            print(f"ERROR: {path} is not valid UTF-8: {e}", file=sys.stderr)
            return False
        
        # Check for NUL bytes (binary corruption)
        if b'\x00' in content:
            print(f"ERROR: {path} contains NUL bytes (binary corruption)", file=sys.stderr)
            return False
        
        print(f"OK: {path} is valid UTF-8 and contains no NUL bytes")
        return True
        
    except FileNotFoundError:
        print(f"ERROR: {path} not found", file=sys.stderr)
        return False
    except Exception as e:
        print(f"ERROR: Failed to validate {path}: {e}", file=sys.stderr)
        return False


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "requirements-lock.txt"
    sys.exit(0 if validate_lockfile(path) else 1)
