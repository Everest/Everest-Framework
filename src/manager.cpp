// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2023 Pionix GmbH and Contributors to EVerest

#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <thread>

#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/program_options.hpp>

#include <fmt/color.h>
#include <fmt/core.h>

#include <everest/logging.hpp>
#include <framework/everest.hpp>
#include <framework/runtime.hpp>
#include <utils/config.hpp>
#include <utils/mqtt_abstraction.hpp>
#include <utils/status_fifo.hpp>

#include "controller/ipc.hpp"
#include "system_unix.hpp"
#include <generated/version_information.hpp>

namespace po = boost::program_options;
namespace fs = std::filesystem;

using namespace Everest;

const auto PARENT_DIED_SIGNAL = SIGTERM;
const int CONTROLLER_IPC_READ_TIMEOUT_MS = 50;
auto complete_start_time = std::chrono::system_clock::now();

#ifdef ENABLE_ADMIN_PANEL
class ControllerHandle {
public:
    ControllerHandle(pid_t pid, int socket_fd) : pid(pid), socket_fd(socket_fd) {
        // we do "non-blocking" read
        controller_ipc::set_read_timeout(socket_fd, CONTROLLER_IPC_READ_TIMEOUT_MS);
    }

    void send_message(const nlohmann::json& msg) {
        controller_ipc::send_message(socket_fd, msg);
    }

    controller_ipc::Message receive_message() {
        return controller_ipc::receive_message(socket_fd);
    }

    void shutdown() {
        // FIXME (aw): tbd
    }

    const pid_t pid;

private:
    const int socket_fd;
};
#endif

// Helper struct keeping information on how to start module
struct ModuleStartInfo {
    enum class Language {
        cpp,
        javascript,
        python
    };
    ModuleStartInfo(const std::string& name_, const std::string& printable_name_, Language lang_, const fs::path& path_,
                    std::vector<std::string> capabilities_) :
        name(name_),
        printable_name(printable_name_),
        language(lang_),
        path(path_),
        capabilities(std::move(capabilities_)) {
    }
    std::string name;
    std::string printable_name;
    Language language;
    fs::path path;

    // required capabilities of this module
    std::vector<std::string> capabilities;
};

static std::vector<char*> arguments_to_exec_argv(std::vector<std::string>& arguments) {
    std::vector<char*> argv_list(arguments.size() + 1);
    std::transform(arguments.begin(), arguments.end(), argv_list.begin(),
                   [](std::string& value) { return value.data(); });

    // add NULL for exec
    argv_list.back() = nullptr;
    return argv_list;
}

static void exec_cpp_module(system::SubProcess& proc_handle, const ModuleStartInfo& module_info,
                            std::shared_ptr<RuntimeSettings> rs) {
    const auto exec_binary = module_info.path.c_str();
    std::vector<std::string> arguments = {module_info.printable_name, "--prefix", rs->prefix.string(), "--conf",
                                          rs->config_file.string(),   "--module", module_info.name};

    auto argv_list = arguments_to_exec_argv(arguments);
    execv(exec_binary, argv_list.data());

    // exec failed
    proc_handle.send_error_and_exit(fmt::format("Syscall to execv() with \"{} {}\" failed ({})", exec_binary,
                                                fmt::join(arguments.begin() + 1, arguments.end(), " "),
                                                strerror(errno)));
}

static void exec_javascript_module(system::SubProcess& proc_handle, const ModuleStartInfo& module_info,
                                   std::shared_ptr<RuntimeSettings> rs) {
    // instead of using setenv, using execvpe might be a better way for a controlled environment!

    // FIXME (aw): everest directory layout
    const auto node_modules_path = rs->prefix / defaults::LIB_DIR / defaults::NAMESPACE / "node_modules";
    setenv("NODE_PATH", node_modules_path.c_str(), 0);

    setenv("EV_MODULE", module_info.name.c_str(), 1);
    setenv("EV_PREFIX", rs->prefix.c_str(), 0);
    setenv("EV_CONF_FILE", rs->config_file.c_str(), 0);

    if (rs->validate_schema) {
        setenv("EV_VALIDATE_SCHEMA", "1", 1);
    }

    if (!rs->validate_schema) {
        setenv("EV_DONT_VALIDATE_SCHEMA", "", 0);
    }

    const auto node_binary = "node";

    std::vector<std::string> arguments = {
        "node",
        "--unhandled-rejections=strict",
        module_info.path.string(),
    };

    auto argv_list = arguments_to_exec_argv(arguments);
    execvp(node_binary, argv_list.data());

    // exec failed
    proc_handle.send_error_and_exit(fmt::format("Syscall to execv() with \"{} {}\" failed ({})", node_binary,
                                                fmt::join(arguments.begin() + 1, arguments.end(), " "),
                                                strerror(errno)));
}

static void exec_python_module(system::SubProcess& proc_handle, const ModuleStartInfo& module_info,
                               std::shared_ptr<RuntimeSettings> rs) {
    // instead of using setenv, using execvpe might be a better way for a controlled environment!

    const auto pythonpath = rs->prefix / defaults::LIB_DIR / defaults::NAMESPACE / "everestpy";

    setenv("EV_MODULE", module_info.name.c_str(), 1);
    setenv("EV_PREFIX", rs->prefix.c_str(), 0);
    setenv("EV_CONF_FILE", rs->config_file.c_str(), 0);
    setenv("PYTHONPATH", pythonpath.c_str(), 0);

    if (rs->validate_schema) {
        setenv("EV_VALIDATE_SCHEMA", "1", 1);
    }

    if (!rs->validate_schema) {
        setenv("EV_DONT_VALIDATE_SCHEMA", "", 0);
    }

    const auto python_binary = "python3";

    std::vector<std::string> arguments = {python_binary, module_info.path.c_str()};

    auto argv_list = arguments_to_exec_argv(arguments);
    execvp(python_binary, argv_list.data());

    // exec failed
    proc_handle.send_error_and_exit(fmt::format("Syscall to execv() with \"{} {}\" failed ({})", python_binary,
                                                fmt::join(arguments.begin() + 1, arguments.end(), " "),
                                                strerror(errno)));
}

static void exec_module(std::shared_ptr<RuntimeSettings> rs, const ModuleStartInfo& module,
                        system::SubProcess& proc_handle) {
    switch (module.language) {
    case ModuleStartInfo::Language::cpp:
        exec_cpp_module(proc_handle, module, rs);
        break;
    case ModuleStartInfo::Language::javascript:
        exec_javascript_module(proc_handle, module, rs);
        break;
    case ModuleStartInfo::Language::python:
        exec_python_module(proc_handle, module, rs);
        break;
    default:
        throw std::logic_error("Module language not in enum");
        break;
    }
}

static std::map<pid_t, std::string> spawn_modules(const std::vector<ModuleStartInfo>& modules,
                                                  std::shared_ptr<RuntimeSettings> rs) {
    std::map<pid_t, std::string> started_modules;

    for (const auto& module : modules) {

        auto proc_handle = system::SubProcess::create(rs->run_as_user, module.capabilities);

        if (proc_handle.is_child()) {
            // first, check if we need any capabilities

            try {
                exec_module(rs, module, proc_handle);
            } catch (const std::exception& err) {
                proc_handle.send_error_and_exit(err.what());
            }
        }

        // we can only come here, if we're the parent!
        auto child_pid = proc_handle.check_child_executed();

        EVLOG_debug << fmt::format("Forked module {} with pid: {}", module.name, child_pid);
        started_modules[child_pid] = module.name;
    }

    return started_modules;
}

struct ModuleReadyInfo {
    bool ready;
    std::shared_ptr<TypedHandler> token;
};

// FIXME (aw): these are globals here, because they are used in the ready callback handlers
std::map<std::string, ModuleReadyInfo> modules_ready;
std::mutex modules_ready_mutex;

static std::map<pid_t, std::string> start_modules(Config& config, MQTTAbstraction& mqtt_abstraction,
                                                  const std::vector<std::string>& ignored_modules,
                                                  const std::vector<std::string>& standalone_modules,
                                                  std::shared_ptr<RuntimeSettings> rs, StatusFifo& status_fifo) {
    BOOST_LOG_FUNCTION();

    std::vector<ModuleStartInfo> modules_to_spawn;

    auto main_config = config.get_main_config();
    modules_to_spawn.reserve(main_config.size());

    for (const auto& module : main_config.items()) {
        std::string module_name = module.key();
        if (std::any_of(ignored_modules.begin(), ignored_modules.end(),
                        [module_name](const auto& element) { return element == module_name; })) {
            EVLOG_info << fmt::format("Ignoring module: {}", module_name);
            continue;
        }

        // FIXME (aw): shall create a ref to main_confit.at(module_name)!
        std::string module_type = main_config[module_name]["module"];
        // FIXME (aw): implicitely adding ModuleReadyInfo and setting its ready member
        auto module_it = modules_ready.emplace(module_name, ModuleReadyInfo{false, nullptr}).first;

        const auto capabilities = [&module_config = main_config.at(module_name)]() {
            const auto cap_it = module_config.find("capabilities");
            if (cap_it == module_config.end()) {
                return std::vector<std::string>();
            }

            return std::vector<std::string>(cap_it->begin(), cap_it->end());
        }();

        if (not capabilities.empty()) {
            EVLOG_info << fmt::format("Module {} wants to aquire the following capabilities: {}", module_name,
                                      fmt::join(capabilities.begin(), capabilities.end(), " "));
        }

        Handler module_ready_handler = [module_name, &mqtt_abstraction, standalone_modules,
                                        mqtt_everest_prefix = rs->mqtt_everest_prefix,
                                        &status_fifo](const std::string&, nlohmann::json json) {
            EVLOG_debug << fmt::format("received module ready signal for module: {}({})", module_name, json.dump());
            std::unique_lock<std::mutex> lock(modules_ready_mutex);
            // FIXME (aw): here are race conditions, if the ready handler gets called while modules are shut down!
            modules_ready.at(module_name).ready = json.get<bool>();
            size_t modules_spawned = 0;
            for (const auto& mod : modules_ready) {
                std::string text_ready =
                    fmt::format((mod.second.ready) ? TERMINAL_STYLE_OK : TERMINAL_STYLE_ERROR, "ready");
                EVLOG_debug << fmt::format("  {}: {}", mod.first, text_ready);
                if (mod.second.ready) {
                    modules_spawned += 1;
                }
            }
            if (!standalone_modules.empty() && std::find(standalone_modules.begin(), standalone_modules.end(),
                                                         module_name) != standalone_modules.end()) {
                EVLOG_info << fmt::format("Standalone module {} initialized.", module_name);
            }
            if (std::all_of(modules_ready.begin(), modules_ready.end(),
                            [](const auto& element) { return element.second.ready; })) {
                auto complete_end_time = std::chrono::system_clock::now();
                status_fifo.update(StatusFifo::ALL_MODULES_STARTED);
                EVLOG_info << fmt::format(
                    TERMINAL_STYLE_OK, "🚙🚙🚙 All modules are initialized. EVerest up and running [{}ms] 🚙🚙🚙",
                    std::chrono::duration_cast<std::chrono::milliseconds>(complete_end_time - complete_start_time)
                        .count());
                mqtt_abstraction.publish(fmt::format("{}ready", mqtt_everest_prefix), nlohmann::json(true));
            } else if (!standalone_modules.empty()) {
                if (modules_spawned == modules_ready.size() - standalone_modules.size()) {
                    EVLOG_info << fmt::format(fg(fmt::terminal_color::green),
                                              "Modules started by manager are ready, waiting for standalone modules.");
                    status_fifo.update(StatusFifo::WAITING_FOR_STANDALONE_MODULES);
                }
            }
        };

        std::string topic = fmt::format("{}/ready", config.mqtt_module_prefix(module_name));

        module_it->second.token =
            std::make_shared<TypedHandler>(HandlerType::ExternalMQTT, std::make_shared<Handler>(module_ready_handler));

        mqtt_abstraction.register_handler(topic, module_it->second.token, QOS::QOS2);

        if (std::any_of(standalone_modules.begin(), standalone_modules.end(),
                        [module_name](const auto& element) { return element == module_name; })) {
            EVLOG_info << "Not starting standalone module: " << fmt::format(TERMINAL_STYLE_BLUE, "{}", module_name);
            continue;
        }

        std::string binary_filename = fmt::format("{}", module_type);
        std::string javascript_library_filename = "index.js";
        std::string python_filename = "module.py";
        auto module_path = rs->modules_dir / module_type;
        const auto printable_module_name = config.printable_identifier(module_name);
        auto binary_path = module_path / binary_filename;
        auto javascript_library_path = module_path / javascript_library_filename;
        auto python_module_path = module_path / python_filename;

        if (fs::exists(binary_path)) {
            EVLOG_debug << fmt::format("module: {} ({}) provided as binary", module_name, module_type);
            modules_to_spawn.emplace_back(module_name, printable_module_name, ModuleStartInfo::Language::cpp,
                                          binary_path, capabilities);
        } else if (fs::exists(javascript_library_path)) {
            EVLOG_debug << fmt::format("module: {} ({}) provided as javascript library", module_name, module_type);
            modules_to_spawn.emplace_back(module_name, printable_module_name, ModuleStartInfo::Language::javascript,
                                          fs::canonical(javascript_library_path), capabilities);
        } else if (fs::exists(python_module_path)) {
            EVLOG_verbose << fmt::format("module: {} ({}) provided as python module", module_name, module_type);
            modules_to_spawn.emplace_back(module_name, printable_module_name, ModuleStartInfo::Language::python,
                                          fs::canonical(python_module_path), capabilities);
        } else {
            throw std::runtime_error(
                fmt::format("module: {} ({}) cannot be loaded because no Binary, JavaScript or Python "
                            "module has been found\n"
                            "  checked paths:\n"
                            "    binary: {}\n"
                            "    js:  {}\n",
                            "    py:  {}\n", module_name, module_type, binary_path.string(),
                            javascript_library_path.string(), python_module_path.string()));
        }
    }

    return spawn_modules(modules_to_spawn, rs);
}

static void shutdown_modules(const std::map<pid_t, std::string>& modules, Config& config,
                             MQTTAbstraction& mqtt_abstraction) {

    {
        const std::lock_guard<std::mutex> lck(modules_ready_mutex);

        for (const auto& module : modules_ready) {
            const auto& ready_info = module.second;
            const auto& module_name = module.first;
            const std::string topic = fmt::format("{}/ready", config.mqtt_module_prefix(module_name));
            mqtt_abstraction.unregister_handler(topic, ready_info.token);
        }

        modules_ready.clear();
    }

    for (const auto& child : modules) {
        auto retval = kill(child.first, SIGTERM);
        // FIXME (aw): supply errno strings
        if (retval != 0) {
            EVLOG_critical << fmt::format("SIGTERM of child: {} (pid: {}) {}: {}. Escalating to SIGKILL", child.second,
                                          child.first, fmt::format(TERMINAL_STYLE_ERROR, "failed"), retval);
            retval = kill(child.first, SIGKILL);
            if (retval != 0) {
                EVLOG_critical << fmt::format("SIGKILL of child: {} (pid: {}) {}: {}.", child.second, child.first,
                                              fmt::format(TERMINAL_STYLE_ERROR, "failed"), retval);
            } else {
                EVLOG_info << fmt::format("SIGKILL of child: {} (pid: {}) {}.", child.second, child.first,
                                          fmt::format(TERMINAL_STYLE_OK, "succeeded"));
            }
        } else {
            EVLOG_info << fmt::format("SIGTERM of child: {} (pid: {}) {}.", child.second, child.first,
                                      fmt::format(TERMINAL_STYLE_OK, "succeeded"));
        }
    }
}

#ifdef ENABLE_ADMIN_PANEL
static ControllerHandle start_controller(std::shared_ptr<RuntimeSettings> rs) {
    int socket_pair[2];

    // FIXME (aw): destroy this socketpair somewhere
    auto retval = socketpair(AF_UNIX, SOCK_DGRAM, 0, socket_pair);
    const int manager_socket = socket_pair[0];
    const int controller_socket = socket_pair[1];

    auto proc_handle = system::SubProcess::create(rs->run_as_user);

    if (proc_handle.is_child()) {
        // FIXME (aw): hack to get the correct directory of the controller
        const auto bin_dir = fs::canonical("/proc/self/exe").parent_path();

        auto controller_binary = bin_dir / "controller";

        close(manager_socket);
        dup2(controller_socket, STDIN_FILENO);
        close(controller_socket);

        execl(controller_binary.c_str(), MAGIC_CONTROLLER_ARG0, NULL);

        // exec failed
        proc_handle.send_error_and_exit(
            fmt::format("Syscall to execl() with \"{} {}\" failed ({})", controller_binary.string(), strerror(errno)));
    }

    close(controller_socket);

    // send initial config to controller
    controller_ipc::send_message(manager_socket, {
                                                     {"method", "boot"},
                                                     {"params",
                                                      {
                                                          {"module_dir", rs->modules_dir.string()},
                                                          {"interface_dir", rs->interfaces_dir.string()},
                                                          {"www_dir", rs->www_dir.string()},
                                                          {"configs_dir", rs->configs_dir.string()},
                                                          {"logging_config_file", rs->logging_config_file.string()},
                                                          {"controller_port", rs->controller_port},
                                                          {"controller_rpc_timeout_ms", rs->controller_rpc_timeout_ms},
                                                      }},
                                                 });

    return {proc_handle.check_child_executed(), manager_socket};
}
#endif

int boot(const po::variables_map& vm) {
    bool check = (vm.count("check") != 0);

    const auto prefix_opt = parse_string_option(vm, "prefix");
    const auto config_opt = parse_string_option(vm, "config");

    std::shared_ptr<RuntimeSettings> rs = std::make_shared<RuntimeSettings>(prefix_opt, config_opt);

    Logging::init(rs->logging_config_file.string());

    EVLOG_info << "  \033[0;1;35;95m_\033[0;1;31;91m__\033[0;1;33;93m__\033[0;1;32;92m__\033[0;1;36;96m_\033[0m      "
                  "\033[0;1;31;91m_\033[0;1;33;93m_\033[0m                \033[0;1;36;96m_\033[0m   ";
    EVLOG_info << " \033[0;1;31;91m|\033[0m  \033[0;1;33;93m_\033[0;1;32;92m__\033[0;1;36;96m_\\\033[0m "
                  "\033[0;1;34;94m\\\033[0m    \033[0;1;33;93m/\033[0m \033[0;1;32;92m/\033[0m               "
                  "\033[0;1;34;94m|\033[0m \033[0;1;35;95m|\033[0m";
    EVLOG_info
        << " \033[0;1;33;93m|\033[0m \033[0;1;32;92m|_\033[0;1;36;96m_\033[0m   \033[0;1;35;95m\\\033[0m "
           "\033[0;1;31;91m\\\033[0m  \033[0;1;33;93m/\033[0m \033[0;1;32;92m/\033[0;1;36;96m__\033[0m "
           "\033[0;1;34;94m_\033[0m \033[0;1;35;95m_\033[0;1;31;91m_\033[0m \033[0;1;33;93m__\033[0;1;32;92m_\033[0m  "
           "\033[0;1;36;96m_\033[0;1;34;94m__\033[0;1;35;95m|\033[0m \033[0;1;31;91m|_\033[0m";
    EVLOG_info << " \033[0;1;32;92m|\033[0m  \033[0;1;36;96m_\033[0;1;34;94m_|\033[0m   \033[0;1;31;91m\\\033[0m "
                  "\033[0;1;33;93m\\\033[0;1;32;92m/\033[0m \033[0;1;36;96m/\033[0m \033[0;1;34;94m_\033[0m "
                  "\033[0;1;35;95m\\\033[0m \033[0;1;31;91m'_\033[0;1;33;93m_/\033[0m \033[0;1;32;92m_\033[0m "
                  "\033[0;1;36;96m\\\033[0;1;34;94m/\033[0m \033[0;1;35;95m__\033[0;1;31;91m|\033[0m "
                  "\033[0;1;33;93m__\033[0;1;32;92m|\033[0m";
    EVLOG_info << " \033[0;1;36;96m|\033[0m \033[0;1;34;94m|_\033[0;1;35;95m__\033[0;1;31;91m_\033[0m   "
                  "\033[0;1;32;92m\\\033[0m  \033[0;1;36;96m/\033[0m  \033[0;1;35;95m__\033[0;1;31;91m/\033[0m "
                  "\033[0;1;33;93m|\033[0m \033[0;1;32;92m|\033[0m  "
                  "\033[0;1;36;96m_\033[0;1;34;94m_/\033[0;1;35;95m\\_\033[0;1;31;91m_\033[0m \033[0;1;33;93m\\\033[0m "
                  "\033[0;1;32;92m|_\033[0m";
    EVLOG_info << " \033[0;1;34;94m|_\033[0;1;35;95m__\033[0;1;31;91m__\033[0;1;33;93m_|\033[0m   "
                  "\033[0;1;36;96m\\\033[0;1;34;94m/\033[0m "
                  "\033[0;1;35;95m\\_\033[0;1;31;91m__\033[0;1;33;93m|_\033[0;1;32;92m|\033[0m  "
                  "\033[0;1;36;96m\\\033[0;1;34;94m__\033[0;1;35;95m_|\033[0;1;31;91m|_\033[0;1;33;93m__\033[0;1;32;"
                  "92m/\\\033[0;1;36;96m__\033[0;1;34;94m|\033[0m";
    EVLOG_info << "";
    EVLOG_info << PROJECT_NAME << " " << PROJECT_VERSION << " " << GIT_VERSION;
    EVLOG_info << rs->version_information;
    EVLOG_info << "";

    if (rs->mqtt_broker_socket_path.empty()) {
        EVLOG_info << "Using MQTT broker " << rs->mqtt_broker_host << ":" << rs->mqtt_broker_port;
    } else {
        EVLOG_info << "Using MQTT broker unix domain sockets:" << rs->mqtt_broker_socket_path;
    }
    if (rs->telemetry_enabled) {
        EVLOG_info << "Telemetry enabled";
    }
    if (not rs->run_as_user.empty()) {
        EVLOG_info << "EVerest will run as system user: " << rs->run_as_user;
    }

#ifdef ENABLE_ADMIN_PANEL
    auto controller_handle = start_controller(rs);
#endif

    EVLOG_verbose << fmt::format("EVerest prefix was set to {}", rs->prefix.string());

    // dump all manifests if requested and terminate afterwards
    if (vm.count("dumpmanifests")) {
        auto dumpmanifests_path = fs::path(vm["dumpmanifests"].as<std::string>());
        EVLOG_debug << fmt::format("Dumping all known validated manifests into '{}'", dumpmanifests_path.string());

        auto manifests = Config::load_all_manifests(rs->modules_dir.string(), rs->schemas_dir.string());

        for (const auto& module : manifests.items()) {
            std::string filename = module.key() + ".yaml";
            auto module_output_path = dumpmanifests_path / filename;
            // FIXME (aw): should we check if the directory exists?
            std::ofstream output_stream(module_output_path);

            // FIXME (aw): this should be either YAML prettyfied, or better, directly copied
            output_stream << module.value().dump(DUMP_INDENT);
        }

        return EXIT_SUCCESS;
    }

    auto start_time = std::chrono::system_clock::now();
    std::unique_ptr<Config> config;
    try {
        // FIXME (aw): we should also use std::filesystem::path here as argument types
        config = std::make_unique<Config>(rs, true);
    } catch (EverestInternalError& e) {
        EVLOG_error << fmt::format("Failed to load and validate config!\n{}", boost::diagnostic_information(e, true));
        return EXIT_FAILURE;
    } catch (boost::exception& e) {
        EVLOG_error << "Failed to load and validate config!";
        EVLOG_critical << fmt::format("Caught top level boost::exception:\n{}", boost::diagnostic_information(e, true));
        return EXIT_FAILURE;
    } catch (std::exception& e) {
        EVLOG_error << "Failed to load and validate config!";
        EVLOG_critical << fmt::format("Caught top level std::exception:\n{}", boost::diagnostic_information(e, true));
        return EXIT_FAILURE;
    }
    auto end_time = std::chrono::system_clock::now();
    EVLOG_info << "Config loading completed in "
               << std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() << "ms";

    // dump config if requested
    if (vm.count("dump")) {
        auto dump_path = fs::path(vm["dump"].as<std::string>());
        EVLOG_debug << fmt::format("Dumping validated config and manifests into '{}'", dump_path.string());

        auto config_dump_path = dump_path / "config.json";

        std::ofstream output_config_stream(config_dump_path);

        output_config_stream << config->get_main_config().dump(DUMP_INDENT);

        auto manifests = config->get_manifests();

        for (const auto& module : manifests.items()) {
            std::string filename = module.key() + ".json";
            auto module_output_path = dump_path / filename;
            std::ofstream output_stream(module_output_path);

            output_stream << module.value().dump(DUMP_INDENT);
        }
    }

    // only config check (and or config dumping) was requested, log check result and exit
    if (check) {
        EVLOG_debug << "Config is valid, terminating as requested";
        return EXIT_SUCCESS;
    }

    std::vector<std::string> standalone_modules;
    if (vm.count("standalone")) {
        standalone_modules = vm["standalone"].as<std::vector<std::string>>();
    }

    const auto& main_config = config->get_main_config();
    for (const auto& module : main_config.items()) {
        std::string module_id = module.key();
        // check if standalone parameter is set
        const auto& module_config = main_config.at(module_id);
        if (module_config.value("standalone", false)) {
            if (std::find(standalone_modules.begin(), standalone_modules.end(), module_id) ==
                standalone_modules.end()) {
                EVLOG_info << "Module " << fmt::format(TERMINAL_STYLE_BLUE, "{}", module_id)
                           << " marked as standalone in config";

                standalone_modules.push_back(module_id);
            }
        }
    }

    std::vector<std::string> ignored_modules;
    if (vm.count("ignore")) {
        ignored_modules = vm["ignore"].as<std::vector<std::string>>();
    }

    // create StatusFifo object
    auto status_fifo = StatusFifo::create_from_path(vm["status-fifo"].as<std::string>());

    auto mqtt_abstraction =
        MQTTAbstraction(rs->mqtt_broker_socket_path, rs->mqtt_broker_host, std::to_string(rs->mqtt_broker_port),
                        rs->mqtt_everest_prefix, rs->mqtt_external_prefix);

    if (!mqtt_abstraction.connect()) {
        if (rs->mqtt_broker_socket_path.empty()) {
            EVLOG_error << fmt::format("Cannot connect to MQTT broker at {}:{}", rs->mqtt_broker_host,
                                       rs->mqtt_broker_port);
        } else {
            EVLOG_error << fmt::format("Cannot connect to MQTT broker socket at {}", rs->mqtt_broker_socket_path);
        }
        return EXIT_FAILURE;
    }

    mqtt_abstraction.spawn_main_loop_thread();

    auto module_handles =
        start_modules(*config, mqtt_abstraction, ignored_modules, standalone_modules, rs, status_fifo);
    bool modules_started = true;
    bool restart_modules = false;

    int wstatus;

#ifndef ENABLE_ADMIN_PANEL
    // switch to low privilege user if configured
    if (not rs->run_as_user.empty()) {
        auto err_set_user = system::set_real_user(rs->run_as_user);
        if (not err_set_user.empty()) {
            EVLOG_error << "Error switching manager to user " << rs->run_as_user << ": " << err_set_user;
            return EXIT_FAILURE;
        }
    }
#endif

    while (true) {
// check if anyone died
#ifdef ENABLE_ADMIN_PANEL
        // non-blocking if admin panel is enabled, as this main loop also processes controller RPC
        auto pid = waitpid(-1, &wstatus, WNOHANG);
#else
        // block if admin panel is disabled, no controller RPC is handled by main loop
        auto pid = waitpid(-1, &wstatus, 0);
#endif

        if (pid == 0) {
            // nothing new from our child process
        } else if (pid == -1) {
            throw std::runtime_error(fmt::format("Syscall to waitpid() failed ({})", strerror(errno)));
        } else {

#ifdef ENABLE_ADMIN_PANEL
            // one of our children exited (first check controller, then modules)
            if (pid == controller_handle.pid) {
                // FIXME (aw): what to do, if the controller exited? Restart it?
                throw std::runtime_error("The controller process exited.");
            }
#endif

            auto module_iter = module_handles.find(pid);
            if (module_iter == module_handles.end()) {
                throw std::runtime_error(fmt::format("Unkown child width pid ({}) died.", pid));
            }

            const auto module_name = module_iter->second;
            module_handles.erase(module_iter);
            // one of our modules died -> kill 'em all
            if (modules_started) {
                EVLOG_critical << fmt::format("Module {} (pid: {}) exited with status: {}. Terminating all modules.",
                                              module_name, pid, wstatus);
                shutdown_modules(module_handles, *config, mqtt_abstraction);
                modules_started = false;

                // Exit if a module died, this gives systemd a change to restart manager
                EVLOG_critical << "Exiting manager.";
                return EXIT_FAILURE;
            } else {
                EVLOG_info << fmt::format("Module {} (pid: {}) exited with status: {}.", module_name, pid, wstatus);
            }
        }

#ifdef ENABLE_ADMIN_PANEL
        if (module_handles.size() == 0 && restart_modules) {
            module_handles =
                start_modules(*config, mqtt_abstraction, ignored_modules, standalone_modules, rs, status_fifo);
            restart_modules = false;
            modules_started = true;
        }

        // check for news from the controller
        auto msg = controller_handle.receive_message();
        if (msg.status == controller_ipc::MESSAGE_RETURN_STATUS::OK) {
            // FIXME (aw): implement all possible messages here, for now just log them
            const auto& payload = msg.json;
            if (payload.at("method") == "restart_modules") {
                shutdown_modules(module_handles, *config, mqtt_abstraction);
                config = std::make_unique<Config>(rs, true);
                modules_started = false;
                restart_modules = true;
            } else if (payload.at("method") == "check_config") {
                const std::string check_config_file_path = payload.at("params");

                try {
                    // check the config
                    auto cfg = Config(rs, true);
                    controller_handle.send_message({{"id", payload.at("id")}});
                } catch (const std::exception& e) {
                    controller_handle.send_message({{"result", e.what()}, {"id", payload.at("id")}});
                }
            } else {
                // unknown payload
                EVLOG_error << fmt::format("Received unkown command via controller ipc:\n{}\n... ignoring",
                                           payload.dump(DUMP_INDENT));
            }
        } else if (msg.status == controller_ipc::MESSAGE_RETURN_STATUS::ERROR) {
            fmt::print("Error in IPC communication with controller: {}\nExiting\n", msg.json.at("error").dump(2));
            return EXIT_FAILURE;
        } else {
            // TIMEOUT fall-through
        }
#endif
    }

    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    po::options_description desc("EVerest manager");
    desc.add_options()("version", "Print version and exit");
    desc.add_options()("help,h", "produce help message");
    desc.add_options()("check", "Check and validate all config files and exit (0=success)");
    desc.add_options()("dump", po::value<std::string>(),
                       "Dump validated and augmented main config and all used module manifests into dir");
    desc.add_options()("dumpmanifests", po::value<std::string>(),
                       "Dump manifests of all modules into dir (even modules not used in config) and exit");
    desc.add_options()("prefix", po::value<std::string>(), "Prefix path of everest installation");
    desc.add_options()("standalone,s", po::value<std::vector<std::string>>()->multitoken(),
                       "Module ID(s) to not automatically start child processes for (those must be started manually to "
                       "make the framework start!).");
    desc.add_options()("ignore", po::value<std::vector<std::string>>()->multitoken(),
                       "Module ID(s) to ignore: Do not automatically start child processes and do not require that "
                       "they are started.");
    desc.add_options()("dontvalidateschema", "Don't validate json schema on every message");
    desc.add_options()("config", po::value<std::string>(),
                       "Full path to a config file.  If the file does not exist and has no extension, it will be "
                       "looked up in the default config directory");
    desc.add_options()("status-fifo", po::value<std::string>()->default_value(""),
                       "Path to a named pipe, that shall be used for status updates from the manager");

    po::variables_map vm;

    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help") != 0) {
            desc.print(std::cout);
            return EXIT_SUCCESS;
        }

        if (vm.count("version") != 0) {
            std::cout << argv[0] << " (" << PROJECT_NAME << " " << PROJECT_VERSION << " " << GIT_VERSION << ") "
                      << std::endl;
            return EXIT_SUCCESS;
        }

        return boot(vm);

    } catch (const BootException& e) {
        EVLOG_error << "Failed to start up everest:\n" << e.what();
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        EVLOG_error << "Main manager process exits because of caught exception:\n" << e.what();
        return EXIT_FAILURE;
    }
}
