#include "json.hpp"

#include <iomanip>
#include <sstream>

namespace anolis {
namespace http {

// Base64 encoding for bytes
static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

static std::string base64_encode(const std::string &bytes) {
    std::string encoded;
    int val = 0;
    int bits = -6;

    for (unsigned char c : bytes) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            encoded.push_back(base64_chars[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) {
        encoded.push_back(base64_chars[((val << 8) >> (bits + 8)) & 0x3F]);
    }
    while ((encoded.size() % 4) != 0) {
        encoded.push_back('=');
    }
    return encoded;
}

static std::string base64_decode(const std::string &encoded) {
    std::string decoded;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) {
        T[base64_chars[i]] = i;
    }

    int val = 0;
    int bits = -8;
    for (unsigned char c : encoded) {
        if (T[c] == -1) {
            break;
        }
        val = (val << 6) + T[c];
        bits += 6;
        if (bits >= 0) {
            decoded.push_back(char((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return decoded;
}

std::string quality_to_string(anolis::deviceprovider::v0::SignalValue_Quality quality) {
    using Q = anolis::deviceprovider::v0::SignalValue_Quality;
    switch (quality) {
        case Q::SignalValue_Quality_QUALITY_OK:
            return "OK";
        case Q::SignalValue_Quality_QUALITY_STALE:
            return "STALE";
        case Q::SignalValue_Quality_QUALITY_UNKNOWN:
            return "UNAVAILABLE";
        case Q::SignalValue_Quality_QUALITY_FAULT:
            return "FAULT";
        default:
            return "UNAVAILABLE";
    }
}

std::string value_type_to_string(anolis::deviceprovider::v0::ValueType type) {
    using VT = anolis::deviceprovider::v0::ValueType;
    switch (type) {
        case VT::VALUE_TYPE_DOUBLE:
            return "double";
        case VT::VALUE_TYPE_INT64:
            return "int64";
        case VT::VALUE_TYPE_UINT64:
            return "uint64";
        case VT::VALUE_TYPE_BOOL:
            return "bool";
        case VT::VALUE_TYPE_STRING:
            return "string";
        case VT::VALUE_TYPE_BYTES:
            return "bytes";
        default:
            return "unknown";
    }
}

nlohmann::json encode_value(const anolis::deviceprovider::v0::Value &value) {
    nlohmann::json result;

    using VT = anolis::deviceprovider::v0::ValueType;
    switch (value.type()) {
        case VT::VALUE_TYPE_DOUBLE:
            result["type"] = "double";
            result["double"] = value.double_value();
            break;
        case VT::VALUE_TYPE_INT64:
            result["type"] = "int64";
            result["int64"] = value.int64_value();
            break;
        case VT::VALUE_TYPE_UINT64:
            result["type"] = "uint64";
            result["uint64"] = value.uint64_value();
            break;
        case VT::VALUE_TYPE_BOOL:
            result["type"] = "bool";
            result["bool"] = value.bool_value();
            break;
        case VT::VALUE_TYPE_STRING:
            result["type"] = "string";
            result["string"] = value.string_value();
            break;
        case VT::VALUE_TYPE_BYTES:
            result["type"] = "bytes";
            result["base64"] = base64_encode(value.bytes_value());
            break;
        default:
            result["type"] = "unknown";
            break;
    }
    return result;
}

nlohmann::json encode_signal_value(const state::CachedSignalValue &cached, const std::string &signal_id) {
    auto now = std::chrono::system_clock::now();
    auto timestamp_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(cached.timestamp.time_since_epoch()).count();
    auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - cached.timestamp).count();

    return {{"signal_id", signal_id},
            {"value", encode_value(cached.value)},
            {"timestamp_epoch_ms", timestamp_ms},
            {"quality", quality_to_string(cached.quality)},
            {"age_ms", age_ms}};
}

nlohmann::json encode_device_state(const state::DeviceState &state, const std::string &provider_id,
                                   const std::string &device_id) {
    nlohmann::json values = nlohmann::json::array();

    // Determine worst-case quality for device-level quality
    auto worst_quality = anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_OK;

    for (const auto &[signal_id, cached] : state.signals) {
        values.push_back(encode_signal_value(cached, signal_id));
        // Track worst quality
        if (cached.quality > worst_quality) {
            worst_quality = cached.quality;
        }
    }

    // If provider is unavailable, device quality is UNAVAILABLE
    std::string device_quality = state.provider_available ? quality_to_string(worst_quality) : "UNAVAILABLE";

    return {{"provider_id", provider_id}, {"device_id", device_id}, {"quality", device_quality}, {"values", values}};
}

nlohmann::json encode_device_info(const registry::RegisteredDevice &device) {
    // Extract display_name and type from proto if available
    std::string display_name = device.capabilities.proto.label();
    if (display_name.empty()) {
        display_name = device.device_id;
    }

    std::string type = device.capabilities.proto.type_id();
    if (type.empty()) {
        type = "unknown";
    }

    return {{"provider_id", device.provider_id},
            {"device_id", device.device_id},
            {"display_name", display_name},
            {"type", type}};
}

nlohmann::json encode_signal_spec(const registry::SignalSpec &spec) {
    // Note: unit is not currently in SignalSpec, would need to extend if needed
    return {{"signal_id", spec.signal_id}, {"value_type", value_type_to_string(spec.type)}, {"label", spec.label}};
}

nlohmann::json encode_function_spec(const registry::FunctionSpec &spec) {
    // Build args structure
    nlohmann::json args = nlohmann::json::object();
    for (const auto &param_id : spec.param_ids) {
        // Note: param types not currently tracked in FunctionSpec
        // Would need to extend registry to include param type info
        args[param_id] = nlohmann::json::object();
    }

    return {{"function_id", spec.function_id}, {"name", spec.function_name}, {"label", spec.label}, {"args", args}};
}

nlohmann::json encode_capabilities(const registry::DeviceCapabilitySet &caps) {
    nlohmann::json signals = nlohmann::json::array();
    for (const auto &[id, spec] : caps.signals_by_id) {
        signals.push_back(encode_signal_spec(spec));
    }

    nlohmann::json functions = nlohmann::json::array();
    for (const auto &[id, spec] : caps.functions_by_id) {
        functions.push_back(encode_function_spec(spec));
    }

    return {{"signals", signals}, {"functions", functions}};
}

bool decode_value(const nlohmann::json &json, anolis::deviceprovider::v0::Value &value, std::string &error) {
    try {
        if (!json.contains("type")) {
            error = "Value missing 'type' field";
            return false;
        }

        std::string type = json.at("type").get<std::string>();

        using VT = anolis::deviceprovider::v0::ValueType;

        if (type == "double") {
            if (!json.contains("double")) {
                error = "Value type 'double' missing 'double' field";
                return false;
            }
            value.set_type(VT::VALUE_TYPE_DOUBLE);
            value.set_double_value(json.at("double").get<double>());
        } else if (type == "int64") {
            if (!json.contains("int64")) {
                error = "Value type 'int64' missing 'int64' field";
                return false;
            }
            value.set_type(VT::VALUE_TYPE_INT64);
            value.set_int64_value(json.at("int64").get<int64_t>());
        } else if (type == "uint64") {
            if (!json.contains("uint64")) {
                error = "Value type 'uint64' missing 'uint64' field";
                return false;
            }
            value.set_type(VT::VALUE_TYPE_UINT64);
            value.set_uint64_value(json.at("uint64").get<uint64_t>());
        } else if (type == "bool") {
            if (!json.contains("bool")) {
                error = "Value type 'bool' missing 'bool' field";
                return false;
            }
            value.set_type(VT::VALUE_TYPE_BOOL);
            value.set_bool_value(json.at("bool").get<bool>());
        } else if (type == "string") {
            if (!json.contains("string")) {
                error = "Value type 'string' missing 'string' field";
                return false;
            }
            value.set_type(VT::VALUE_TYPE_STRING);
            value.set_string_value(json.at("string").get<std::string>());
        } else if (type == "bytes") {
            if (!json.contains("base64")) {
                error = "Value type 'bytes' missing 'base64' field";
                return false;
            }
            value.set_type(VT::VALUE_TYPE_BYTES);
            value.set_bytes_value(base64_decode(json.at("base64").get<std::string>()));
        } else {
            error = "Unknown value type: " + type;
            return false;
        }

        return true;
    } catch (const std::exception &e) {
        error = std::string("JSON parse error: ") + e.what();
        return false;
    }
}

bool decode_call_request(const nlohmann::json &json, std::string &provider_id, std::string &device_id,
                         uint32_t &function_id, std::map<std::string, anolis::deviceprovider::v0::Value> &args,
                         std::string &error) {
    try {
        if (!json.contains("provider_id")) {
            error = "Missing 'provider_id'";
            return false;
        }
        if (!json.contains("device_id")) {
            error = "Missing 'device_id'";
            return false;
        }
        if (!json.contains("function_id")) {
            error = "Missing 'function_id'";
            return false;
        }

        provider_id = json.at("provider_id").get<std::string>();
        device_id = json.at("device_id").get<std::string>();
        function_id = json.at("function_id").get<uint32_t>();

        // Args is optional
        if (json.contains("args")) {
            const auto &args_json = json.at("args");
            if (!args_json.is_object()) {
                error = "'args' must be an object";
                return false;
            }

            for (const auto &[key, val] : args_json.items()) {
                anolis::deviceprovider::v0::Value value;
                if (!decode_value(val, value, error)) {
                    error = "Invalid value for arg '" + key + "': " + error;
                    return false;
                }
                args[key] = value;
            }
        }

        return true;
    } catch (const std::exception &e) {
        error = std::string("JSON parse error: ") + e.what();
        return false;
    }
}

}  // namespace http
}  // namespace anolis
