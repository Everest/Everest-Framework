// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2021 Pionix GmbH and Contributors to EVerest
#ifndef FRAMEWORK_EVEREST_HPP
#define FRAMEWORK_EVEREST_HPP

#include <chrono>
#include <map>
#include <set>
#include <thread>

#include <everest/exceptions.hpp>

#include <utils/config.hpp>
#include <utils/mqtt_abstraction.hpp>
#include <utils/types.hpp>

namespace Everest {
///
/// \brief A structure that contains a command definition for a cmd of a module
///
struct cmd {
    std::string impl_id;    ///< The implementation id of the command
    std::string cmd_name;   ///< The name of the command
    Command cmd;            ///< The callback function
    Arguments arg_types;    ///< The argument types
    ReturnType return_type; ///< The return type
};

///
/// \brief Contains the EVerest framework that provides convenience functionality for implementing EVerest modules
///
class Everest {

private:
    std::string module_id;
    Config config;
    std::map<std::string, std::set<std::string>> registered_cmds;
    std::map<std::string, Handler> registered_external_mqtt_handlers;
    std::vector<Token> registered_handlers;
    bool ready_received;
    std::chrono::seconds remote_cmd_ack_timeout;
    std::chrono::seconds remote_cmd_res_timeout;
    bool validate_data_with_schema;
    std::unique_ptr<std::function<void()>> on_ready;
    std::thread heartbeat_thread;
    std::string module_name;
    json module_manifest;
    json module_classes;

    Everest(std::string module_id, Config config, bool validate_data_with_schema,
            const std::string& mqtt_server_address, const std::string& mqtt_server_port);
    MQTTAbstraction& mqtt_abstraction;

    void internal_publish(const std::string& topic, const json& json);

    void handle_ready(json data);

    void heartbeat();

    static std::string check_args(const Arguments& func_args, json manifest_args);
    static bool check_arg(ArgumentType arg_types, json manifest_arg);

public:
    json get_cmd_definition(const std::string& module_id, const std::string& impl_id, const std::string& cmd_name,
                            bool is_call);
    json get_cmd_definition(const std::string& module_id, const std::string& impl_id, const std::string& cmd_name);

    ///
    /// \brief Allows a module to indicate that it provides the given command \p cmd
    ///
    void provide_cmd(const std::string impl_id, const std::string cmd_name, const JsonCommand handler);
    void provide_cmd(const cmd& cmd);

    ///
    /// \brief Provides functionality for calling commands of other modules. The module is identified by the given \p
    /// requirement_id, the command by the given command name \p cmd_name and the needed arguments by \p args
    ///
    json call_cmd(const std::string& requirement_id, const std::string& cmd_name, json json_args);
    json call_cmd(const std::string& requirement_id, uint64_t index, const std::string& cmd_name, json json_args);
    Result call_cmd(const std::string& requirement_id, const std::string& cmd_name, Parameters args);

    ///
    /// \brief Publishes a variable of the given \p impl_id, names \p var_name with the given \p value
    ///
    void publish_var(const std::string& impl_id, const std::string& var_name, json json_value);
    void publish_var(const std::string& impl_id, const std::string& var_name, Value value);

    ///
    /// \brief Subscribes to a variable of another module identified by the given \p requirement_id and variable name \p
    /// var_name. The given \p callback is called when a new value becomes available
    ///
    void subscribe_var(const std::string& requirement_id, const std::string& var_name, const ValueCallback& callback);
    void subscribe_var(const std::string& requirement_id, const std::string& var_name, const JsonCallback& callback);
    void subscribe_var(const std::string& requirement_id, uint64_t index, const std::string& var_name,
                       const JsonCallback& callback);

    ///
    /// \brief publishes the given \p data on the given \p topic
    ///
    void external_mqtt_publish(const std::string& topic, const std::string& data);

    ///
    /// \brief Allows a module to indicate that it provides a external mqtt \p handler at the given \p topic
    ///
    void provide_external_mqtt_handler(const std::string& topic, const StringHandler& handler);

    ///
    /// \brief Chccks if all commands of a module that are listed in its manifest are available
    ///
    void check_code();

    ///
    /// \brief Calls the connect method of the MQTTAbstraction to connect to the MQTT broker
    ///
    bool connect();

    ///
    /// \brief Calls the disconnect method of the MQTTAbstraction to disconnect from the MQTT broker
    ///
    void disconnect();

    ///
    /// \brief Calls the mainloop method of the MQTTAbstraction to start the MQTT mainloop
    ///
    void mainloop();

    ///
    /// \brief Ready Handler for local readyness (e.g. this module is now ready)
    ///
    void signal_ready();

    ///
    /// \brief registers a callback \p handler that is called when the global ready signal is received via mqtt
    ///
    void register_on_ready_handler(const std::function<void()>& handler);

    ///
    /// \returns the instance of the Everest singleton taking a \p module_id, the \p config, a \p mqtt_server_address
    /// and \p mqtt_server_port as parameters. If validation of data with the known json schemas is needed this can be
    /// activated by setting \p validate_data_with_schema to true
    static Everest& get_instance(std::string module_id, Config config, bool validate_data_with_schema,
                                 const std::string& mqtt_server_address, const std::string& mqtt_server_port) {
        static Everest instance(module_id, config, validate_data_with_schema, mqtt_server_address, mqtt_server_port);

        return instance;
    }

    ///
    /// \returns the instance of the Everest singleton taking a \p module_id and \p config, as parameters. If validation
    /// of data with the known json schemas is needed this can be activated by setting \p validate_data_with_schema to
    /// true. The mqtt_server_address is set to localhost and the mqtt_server_port to 1883
    static Everest& get_instance(std::string module_id, Config config, bool validate_data_with_schema) {
        return get_instance(module_id, config, validate_data_with_schema, "localhost", "1883");
    }

    ///
    /// \returns the instance of the Everest singleton taking a \p module_id and \p config, as parameters enabling
    /// validation of data with the known json schemas
    static Everest& get_instance(std::string module_id, Config config) {
        return get_instance(module_id, config, true);
    }

    Everest(Everest const&) = delete;
    void operator=(Everest const&) = delete;
};
} // namespace Everest

#endif // FRAMEWORK_EVEREST_HPP
