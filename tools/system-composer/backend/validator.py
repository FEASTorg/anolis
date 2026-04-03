"""Cross-provider system-level validation for the Anolis System Composer."""


def validate_system(system: dict) -> list[str]:
    """
    Returns a list of error strings. Empty list means the system is valid.
    """
    errors: list[str] = []

    topology = system.get("topology", {})
    providers = topology.get("providers", {})
    runtime = topology.get("runtime", {})
    paths = system.get("paths", {})
    provider_paths = paths.get("providers", {})
    runtime_providers = runtime.get("providers", [])

    # 1. Duplicate provider IDs
    pids = [p["id"] for p in runtime_providers]
    if len(pids) != len(set(pids)):
        errors.append("Duplicate provider IDs in runtime config.")

    # 2. Tool-port vs runtime-port collision
    if runtime.get("http_port") == 3002:
        errors.append(
            "Runtime HTTP port 3002 conflicts with the composer's own port."
        )

    # 3. Duplicate (bus_path, address) ownership across bread + ezo providers
    owned: dict = {}  # (bus_path, addr_int) -> provider_id
    for pid, pcfg in providers.items():
        if pcfg.get("kind") not in ("bread", "ezo"):
            continue
        bus_path = provider_paths.get(pid, {}).get("bus_path", "")
        for dev in pcfg.get("devices", []):
            addr_str = dev.get("address", "0x00")
            try:
                addr = int(addr_str, 16)
            except (ValueError, TypeError):
                continue
            key = (bus_path, addr)
            if key in owned:
                errors.append(
                    f"I2C address {addr_str} on bus '{bus_path}' is claimed by "
                    f"both '{owned[key]}' and '{pid}'."
                )
            else:
                owned[key] = pid

    # 4. Provider in runtime list with no matching topology entry
    for p in runtime_providers:
        if p["id"] not in providers:
            errors.append(
                f"Provider '{p['id']}' is in the runtime list but has no config entry."
            )

    # 5. Provider in topology with no matching runtime entry
    runtime_ids = {p["id"] for p in runtime_providers}
    for pid in providers:
        if pid not in runtime_ids:
            errors.append(
                f"Provider '{pid}' has a config entry but is not in the runtime list."
            )

    return errors
