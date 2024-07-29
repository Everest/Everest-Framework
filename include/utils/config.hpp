// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef UTILS_CONFIG_HPP
#define UTILS_CONFIG_HPP

#include <filesystem>
#include <list>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>

#include <nlohmann/json-schema.hpp>

#include <utils/config_cache.hpp>
#include <utils/error.hpp>
#include <utils/error/error_type_map.hpp>
#include <utils/module_config.hpp>
#include <utils/types.hpp>

namespace Everest {
using json = nlohmann::json;
using json_uri = nlohmann::json_uri;
using json_validator = nlohmann::json_schema::json_validator;
namespace fs = std::filesystem;

struct ManagerSettings;
struct RuntimeSettings;

///
/// \brief A structure that contains all available schemas
///
struct schemas {
    json config;                 ///< The config schema
    json manifest;               ///< The manifest scheme
    json interface;              ///< The interface schema
    json type;                   ///< The type schema
    json error_declaration_list; ///< The error-declaration-list schema
};

///
/// \brief Allowed format of a type URI, which are of a format like this /type_file_name#/TypeName
///
const static std::regex type_uri_regex{R"(^((?:\/[a-zA-Z0-9\-\_]+)+#\/[a-zA-Z0-9\-\_]+)$)"};

class ConfigBase {
protected: // or protected?
    json main;
    json settings;

    json manifests;
    json interfaces;
    json interface_definitions;
    json types;
    schemas _schemas;

    std::unordered_map<std::string, ModuleTierMappings> tier_mappings;
    std::unordered_map<std::string, std::optional<TelemetryConfig>> telemetry_configs;
    // experimental caches
    std::unordered_map<std::string, std::string> module_names;
    std::unordered_map<std::string, ConfigCache> module_config_cache;

    error::ErrorTypeMap error_map;

    std::shared_ptr<MQTTSettings> mqtt_settings;

    ///
    /// \brief extracts information about the provided module given via \p module_id from the config and manifest
    ///
    /// \returns a json object containing module_id, module_name, impl_id and impl_intf
    json extract_implementation_info(const std::string& module_id) const;

    ///
    /// \brief extracts information about the provided implementation given via \p module_id and \p impl_id from the
    /// config and
    /// manifest
    ///
    /// \returns a json object containing module_id, module_name, impl_id and impl_intf
    json extract_implementation_info(const std::string& module_id, const std::string& impl_id) const;

    ///
    /// \brief Parses the 3 tier model mappings in the config
    /// You can set a EVSE id called "evse" and Connector id called "connector" for the whole module.
    /// Additionally a "mapping" can be specified in the following way:
    /// mapping:
    ///   implementation_id:
    ///     evse: 1
    ///     connector: 1
    /// If no mappings are found it will be assumed that the module is mapped to the charging station.
    /// If only a module mapping is defined alle implementations are mapped to this module mapping.
    /// Implementations can have overwritten mappings.
    void parse_3_tier_model_mapping();

public:
    ConfigBase(std::shared_ptr<MQTTSettings> mqtt_settings) : mqtt_settings(mqtt_settings) {}; // virtual?

    ///
    /// \brief turns then given \p module_id into a printable identifier
    ///
    /// \returns a string with the printable identifier
    std::string printable_identifier(const std::string& module_id) const;

    ///
    /// \brief turns then given \p module_id and \p impl_id into a printable identifier
    ///
    /// \returns a string with the printable identifier
    std::string printable_identifier(const std::string& module_id, const std::string& impl_id) const;

    std::string get_module_name(const std::string& module_id) const;

    ///
    /// \brief turns the given \p module_id and \p impl_id into a mqtt prefix
    ///
    std::string mqtt_prefix(const std::string& module_id, const std::string& impl_id);

    ///
    /// \brief turns the given \p module_id into a mqtt prefix
    ///
    std::string mqtt_module_prefix(const std::string& module_id);

    ///
    /// \returns a json object that contains the main config
    // FIXME (aw): this should be const and return the config by const ref!
    json get_main_config();

    ///
    /// \returns a json object that contains the manifests
    const json& get_manifests();

    ///
    /// \returns a json object that contains the interface definitions
    json get_interface_definitions();

    ///
    /// \returns a json object that contains the available interfaces
    json get_interfaces();

    ///
    /// \returns a json object that contains the settings
    json get_settings();

    ///
    /// \returns a json object that contains the schemas
    json get_schemas();

    ///
    /// \returns a json object that contains the schemas
    json get_error_types_map();

    ///
    /// \returns the module config cache
    std::unordered_map<std::string, ConfigCache> get_module_config_cache();

    ///
    /// \returns the 3 tier model mappings
    std::unordered_map<std::string, ModuleTierMappings> get_3_tier_model_mappings();

    //
    /// \returns the 3 tier model mappings for the given \p module_id
    std::optional<ModuleTierMappings> get_3_tier_model_mappings(const std::string& module_id);

    //
    /// \returns the 3 tier model mapping for the given \p module_id and \p impl_id
    std::optional<Mapping> get_3_tier_model_mapping(const std::string& module_id, const std::string& impl_id);
};

class ManagerConfig : public ConfigBase {
private:
    bool manager = false;
    std::shared_ptr<ManagerSettings> ms;

    void load_and_validate_manifest(const std::string& module_id, const json& module_config);

    ///
    /// \brief loads and validates the given file \p file_path with the schema \p schema
    ///
    /// \returns the loaded json and how long the validation took in ms
    std::tuple<json, int> load_and_validate_with_schema(const fs::path& file_path, const json& schema);

    ///
    /// \brief resolves inheritance tree of json interface \p intf_name, throws an exception if variables or commands
    /// would be overwritten
    ///
    /// \returns the resulting interface definiion
    json resolve_interface(const std::string& intf_name);

    ///
    /// \brief loads the contents of the interface file referenced by the give \p intf_name from disk and validates its
    /// contents
    ///
    /// \returns a json object containing the interface definition
    json load_interface_file(const std::string& intf_name);

    ///
    /// \brief loads the contents of an error or an error list referenced by the given \p reference.
    ///
    /// \returns a list of json objects containing the error definitions
    std::list<json> resolve_error_ref(const std::string& reference);

    ///
    /// \brief replaces all error references in the given \p interface_json with the actual error definitions
    ///
    /// \returns the interface_json with replaced error references
    json replace_error_refs(json& interface_json);

    void resolve_all_requirements();

    void parse(json config);

public:
    ManagerConfig(std::shared_ptr<ManagerSettings> ms);

    json serialize();
};

// TODO: split config into managerconfig and config!

///
/// \brief Contains config and manifest parsing
///
class Config : public ConfigBase {
private:
    std::shared_ptr<RuntimeSettings> rs;
    bool manager;

    std::unordered_map<std::string, std::optional<TelemetryConfig>> telemetry_configs;

public:
    error::ErrorTypeMap get_error_map() const;
    bool module_provides(const std::string& module_name, const std::string& impl_id);
    json get_module_cmds(const std::string& module_name, const std::string& impl_id);
    ///
    /// \brief creates a new Config object
    explicit Config(std::shared_ptr<MQTTSettings> mqtt_settings, json config);

    ///
    /// \brief checks if the given \p module_id provides the requirement given in \p requirement_id
    ///
    /// \returns a json object that contains the requirement
    json resolve_requirement(const std::string& module_id, const std::string& requirement_id) const;

    ///
    /// \returns a list of Requirements for \p module_id
    ///
    std::list<Requirement> get_requirements(const std::string& module_id) const;

    ///
    /// \brief checks if the config contains the given \p module_id
    ///
    bool contains(const std::string& module_id) const;

    ///
    /// \returns a map of module config options
    ModuleConfigs get_module_configs(const std::string& module_id) const;

    ///
    /// \returns a json object that contains the module config options
    json get_module_json_config(const std::string& module_id);

    ///
    /// \brief assemble basic information about the module (id, name,
    /// authors, license)
    ///
    /// \returns a ModuleInfo object
    ModuleInfo get_module_info(const std::string& module_id);

    ///
    /// \returns a TelemetryConfig if this has been configured
    std::optional<TelemetryConfig> get_telemetry_config(const std::string& module_id);

    ///
    /// \returns a json object that contains the interface definition
    json get_interface_definition(const std::string& interface_name);

    ///
    /// \brief A json schema loader that can handle type refs and otherwise uses the builtin draft7 schema of
    /// the json schema validator when it encounters it. Throws an exception
    /// otherwise
    ///
    void ref_loader(const json_uri& uri, json& schema);

    ///
    /// \brief loads the config.json and manifest.json in the schemes subfolder of
    /// the provided \p schemas_dir
    ///
    /// \returns the config and manifest schemas
    static schemas load_schemas(const fs::path& schemas_dir);

    ///
    /// \brief loads and validates a json schema at the provided \p path
    ///
    /// \returns the loaded json schema as a json object
    static json load_schema(const fs::path& path);

    ///
    /// \brief loads all module manifests relative to the \p main_dir
    ///
    /// \returns all module manifests as a json object
    static json load_all_manifests(const std::string& modules_dir, const std::string& schemas_dir);

    ///
    /// \brief Extracts the keys of the provided json \p object
    ///
    /// \returns a set of object keys
    static std::set<std::string> keys(const json& object);

    ///
    /// \brief A simple json schema loader that uses the builtin draft7 schema of
    /// the json schema validator when it encounters it, throws an exception
    /// otherwise
    ///
    static void loader(const json_uri& uri, json& schema);

    ///
    /// \brief An extension to the default format checker of the json schema
    /// validator supporting uris
    ///
    static void format_checker(const std::string& format, const std::string& value);
};
} // namespace Everest

NLOHMANN_JSON_NAMESPACE_BEGIN
template <> struct adl_serializer<Everest::schemas> {
    static void to_json(nlohmann::json& j, const Everest::schemas& s) {
        j = {{"config", s.config},
             {"manifest", s.manifest},
             {"interface", s.interface},
             {"type", s.type},
             {"error_declaration_list", s.error_declaration_list}};
    }

    static void from_json(const nlohmann::json& j, Everest::schemas& s) {
        s.config = j.at("config");
        s.manifest = j.at("manifest");
        s.interface = j.at("interface");
        s.type = j.at("type");
        s.error_declaration_list = j.at("error_declaration_list");
    }
};
NLOHMANN_JSON_NAMESPACE_END

#endif // UTILS_CONFIG_HPP
