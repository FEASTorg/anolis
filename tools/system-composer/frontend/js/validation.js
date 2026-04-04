// validation.js — field-level and system-level validation

/**
 * Validate a single input against rules. Adds/removes inline error message.
 * @param {HTMLInputElement} input
 * @param {{ required?: boolean, min?: number, max?: number,
 *           pattern?: RegExp, patternMsg?: string }} rules
 * @returns {boolean} true if valid
 */
export function validateField(input, rules) {
  const value = input.value.trim();
  let error = null;

  if (rules.required && !value) {
    error = 'Required';
  } else if (rules.min !== undefined && rules.max !== undefined && value !== '') {
    const n = Number(value);
    if (isNaN(n) || n < rules.min || n > rules.max) {
      error = `Must be between ${rules.min} and ${rules.max}`;
    }
  } else if (rules.pattern && value !== '' && !rules.pattern.test(value)) {
    error = rules.patternMsg || 'Invalid format';
  }

  _applyError(input, error);
  return error === null;
}

export function clearFieldError(input) {
  _applyError(input, null);
}

function _applyError(input, message) {
  const parent = input.closest('.form-group') || input.parentElement;
  parent.querySelector('.field-error')?.remove();
  if (message) {
    input.classList.add('input-error');
    const el = document.createElement('span');
    el.className = 'field-error';
    el.textContent = message;
    input.insertAdjacentElement('afterend', el);
  } else {
    input.classList.remove('input-error');
  }
}

/**
 * System-level pre-save validation.
 * @param {object} system
 * @returns {string[]} error messages (empty = valid)
 */
export function validateSystem(system) {
  const errors = [];
  const paths = system.paths ?? {};
  const runtime = system.topology?.runtime ?? {};
  const topologyProviders = system.topology?.providers ?? {};
  const providerPaths = system.paths?.providers ?? {};
  const providers = system.topology?.runtime?.providers ?? [];

  // 1. Runtime executable path
  if (!paths.runtime_executable?.trim()) {
    errors.push('Runtime executable path is missing.');
  }

  // 2. Duplicate provider IDs
  const seen = new Set();
  const dupes = new Set();
  for (const p of providers) {
    if (seen.has(p.id)) dupes.add(p.id);
    seen.add(p.id);
  }
  if (dupes.size > 0) {
    errors.push(`Duplicate provider IDs: ${[...dupes].join(', ')}`);
  }

  // 3. Port 3002 collision (reserved by System Composer)
  if (system.topology?.runtime?.http_port === 3002) {
    errors.push('Port 3002 is reserved for the System Composer itself.');
  }

  // 4. Duplicate (bus_path, address) across bread/ezo providers
  const busOwnership = {};
  for (const p of providers) {
    if (p.kind !== 'bread' && p.kind !== 'ezo') continue;
    const busPath = providerPaths?.[p.id]?.bus_path ?? '';
    const devices = topologyProviders?.[p.id]?.devices ?? [];
    for (const d of devices) {
      const key = `${busPath}::${d.address}`;
      if (busOwnership[key]) {
        errors.push(
          `Address ${d.address} on bus "${busPath}" is claimed by both "${busOwnership[key]}" and "${p.id}".`
        );
      } else {
        busOwnership[key] = p.id;
      }
    }
  }

  // 5. Runtime/topology membership mismatch
  for (const p of providers) {
    if (!topologyProviders?.[p.id]) {
      errors.push(`Provider "${p.id}" has no config in topology.providers.`);
    }
  }
  const runtimeIds = new Set(providers.map(p => p.id));
  for (const pid of Object.keys(topologyProviders)) {
    if (!runtimeIds.has(pid)) {
      errors.push(`Provider "${pid}" has a config entry but is not in the runtime list.`);
    }
  }

  // 6. Duplicate device IDs within a provider
  for (const [pid, cfg] of Object.entries(topologyProviders)) {
    const deviceSeen = new Set();
    const deviceDupes = new Set();
    for (const dev of cfg.devices ?? []) {
      if (!dev.id) continue;
      if (deviceSeen.has(dev.id)) deviceDupes.add(dev.id);
      deviceSeen.add(dev.id);
    }
    if (deviceDupes.size > 0) {
      errors.push(`Provider "${pid}" has duplicate device IDs: ${[...deviceDupes].join(', ')}`);
    }
  }

  // 7. Required provider path entries
  for (const p of providers) {
    const pathEntry = providerPaths?.[p.id];
    if (!pathEntry) {
      errors.push(`Provider "${p.id}" has no paths.providers entry.`);
      continue;
    }
    if (!pathEntry.executable?.trim()) {
      errors.push(`Provider "${p.id}" is missing an executable path.`);
    }
    if ((p.kind === 'bread' || p.kind === 'ezo') && !pathEntry.bus_path?.trim()) {
      errors.push(`Provider "${p.id}" requires a bus path.`);
    }
  }

  // 8. Restart policy validation
  for (const p of providers) {
    const rp = p.restart_policy;
    if (!rp?.enabled) continue;

    if (!Number.isInteger(rp.max_attempts) || rp.max_attempts < 1) {
      errors.push(`Provider "${p.id}" restart policy max_attempts must be >= 1.`);
    }
    if (!Array.isArray(rp.backoff_ms) || rp.backoff_ms.length === 0) {
      errors.push(`Provider "${p.id}" restart policy backoff_ms must be a non-empty list.`);
    } else {
      if (Number.isInteger(rp.max_attempts) && rp.backoff_ms.length !== rp.max_attempts) {
        errors.push(`Provider "${p.id}" restart policy backoff_ms length must match max_attempts.`);
      }
      if (rp.backoff_ms.some(v => !Number.isInteger(v) || v < 0)) {
        errors.push(`Provider "${p.id}" restart policy backoff_ms values must be integers >= 0.`);
      }
    }
    if (!Number.isInteger(rp.timeout_ms) || rp.timeout_ms < 1000) {
      errors.push(`Provider "${p.id}" restart policy timeout_ms must be >= 1000.`);
    }
    if (rp.success_reset_ms !== undefined && (!Number.isInteger(rp.success_reset_ms) || rp.success_reset_ms < 0)) {
      errors.push(`Provider "${p.id}" restart policy success_reset_ms must be >= 0.`);
    }
  }

  // 9. Automation consistency
  if (runtime.automation_enabled && !(runtime.behavior_tree_path ?? runtime.behavior_tree)) {
    errors.push('Automation is enabled but no behavior tree path is set.');
  }

  return errors;
}
