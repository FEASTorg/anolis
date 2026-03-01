#pragma once

#include <limits>
#include <optional>
#include <string>

#include "events/event_types.hpp"
#include "parameter_types.hpp"

namespace anolis {
namespace automation {

/**
 * @brief Convert control-plane ParameterValue into events::TypedValue.
 *
 * Ownership boundary:
 * - ParameterValue is the canonical control-plane domain (runtime parameters).
 * - events::TypedValue is the canonical signal/event domain.
 * - Cross-domain conversion is explicit and centralized in this header.
 */
inline events::TypedValue parameter_value_to_typed_value(const ParameterValue &value) {
    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value);
    }
    if (std::holds_alternative<int64_t>(value)) {
        return std::get<int64_t>(value);
    }
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value);
    }
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }
    return std::string{};
}

/**
 * @brief Convert signal/event TypedValue into control-plane ParameterValue.
 *
 * Conversion policy:
 * - `double`, `int64`, `bool`, `string` map directly.
 * - `uint64` is accepted only when it fits in signed int64 range.
 * - Unsupported values return nullopt and optionally populate `error`.
 */
inline std::optional<ParameterValue> parameter_value_from_typed_value(const events::TypedValue &value,
                                                                      std::string *error = nullptr) {
    if (std::holds_alternative<double>(value)) {
        return ParameterValue{std::get<double>(value)};
    }
    if (std::holds_alternative<int64_t>(value)) {
        return ParameterValue{std::get<int64_t>(value)};
    }
    if (std::holds_alternative<bool>(value)) {
        return ParameterValue{std::get<bool>(value)};
    }
    if (std::holds_alternative<std::string>(value)) {
        return ParameterValue{std::get<std::string>(value)};
    }
    if (std::holds_alternative<uint64_t>(value)) {
        const uint64_t raw = std::get<uint64_t>(value);
        if (raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            if (error != nullptr) {
                *error = "uint64 value exceeds int64 range for ParameterValue";
            }
            return std::nullopt;
        }
        return ParameterValue{static_cast<int64_t>(raw)};
    }

    if (error != nullptr) {
        *error = "Unsupported TypedValue alternative for ParameterValue conversion";
    }
    return std::nullopt;
}

}  // namespace automation
}  // namespace anolis
