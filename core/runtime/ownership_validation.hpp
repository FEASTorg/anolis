#pragma once

/**
 * @file ownership_validation.hpp
 * @brief Shared-bus ownership validation helpers for discovered device inventories.
 */

#include <string>
#include <vector>

#include "registry/device_registry.hpp"

namespace anolis {
namespace runtime {

/**
 * @brief Validate that no published devices claim the same I2C bus/address pair.
 *
 * Devices that expose canonical `hw.bus_path` and `hw.i2c_address` tags must
 * own a unique pair across the full runtime inventory.
 */
bool validate_i2c_ownership_claims(const std::vector<registry::RegisteredDevice> &devices, std::string &error);

/**
 * @brief Re-run ownership validation using a provider replacement candidate.
 *
 * This is used during provider restart to validate the replacement inventory
 * against the currently published devices from every other provider before the
 * swap is committed.
 */
bool validate_i2c_ownership_claims_after_provider_replacement(
    const std::vector<registry::RegisteredDevice> &current_devices, const std::string &provider_id,
    const std::vector<registry::RegisteredDevice> &replacement_devices, std::string &error);

}  // namespace runtime
}  // namespace anolis
