// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <framework/runtime.hpp>
#include <tests/helpers.hpp>
#include <utils/config.hpp>

namespace fs = std::filesystem;

SCENARIO("Check ManagerSettings Constructor", "[!throws]") {
    std::string bin_dir = Everest::tests::get_bin_dir().string() + "/";
    GIVEN("An invalid prefix, but a valid config file") {
        THEN("It should throw BootException") {
            CHECK_THROWS_AS(
                Everest::ManagerSettings(bin_dir + "non-valid-prefix/", bin_dir + "valid_config/config.yaml"),
                Everest::BootException);
        }
    }
    GIVEN("A valid prefix, but a non existing config file") {
        THEN("It should throw BootException") {
            CHECK_THROWS_AS(Everest::ManagerSettings(bin_dir + "valid_config/", bin_dir + "non-existing-config.yaml"),
                            Everest::BootException);
        }
    }
    GIVEN("A valid prefix and a valid config file") {
        THEN("It should not throw") {
            CHECK_NOTHROW(Everest::ManagerSettings(bin_dir + "valid_config/", bin_dir + "valid_config/config.yaml"));
        }
    }
    GIVEN("A broken yaml file") {
        THEN("It should throw") {
            CHECK_THROWS(Everest::ManagerSettings(bin_dir + "broken_yaml/", bin_dir + "broken_yaml/config.yaml"));
        }
    }
    GIVEN("A empty yaml file") {
        THEN("It shouldn't throw") {
            CHECK_NOTHROW(Everest::ManagerSettings(bin_dir + "empty_yaml/", bin_dir + "empty_yaml/config.yaml"));
        }
    }
    GIVEN("A empty yaml object file") {
        THEN("It shouldn't throw") {
            CHECK_NOTHROW(
                Everest::ManagerSettings(bin_dir + "empty_yaml_object/", bin_dir + "empty_yaml_object/config.yaml"));
        }
    }
    GIVEN("A null yaml file") {
        THEN("It shouldn't throw") {
            CHECK_NOTHROW(Everest::ManagerSettings(bin_dir + "null_yaml/", bin_dir + "null_yaml/config.yaml"));
        }
    }
    GIVEN("A string yaml file") {
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerSettings(bin_dir + "string_yaml/", bin_dir + "string_yaml/config.yaml"),
                            Everest::BootException);
        }
    }
}
SCENARIO("Check Config Constructor", "[!throws]") {
    std::string bin_dir = Everest::tests::get_bin_dir().string() + "/";
    GIVEN("A config without modules") {
        std::shared_ptr<Everest::ManagerSettings> rs = std::make_shared<Everest::ManagerSettings>(
            Everest::ManagerSettings(bin_dir + "empty_config/", bin_dir + "empty_config/config.yaml"));
        Everest::ManagerConfig config = Everest::ManagerConfig(rs);
        THEN("It should not contain the module some_module") {
            CHECK(!config.contains("some_module"));
        }
    }
    GIVEN("A config file referencing a non existent module") {
        std::shared_ptr<Everest::ManagerSettings> rs = std::make_shared<Everest::ManagerSettings>(
            Everest::ManagerSettings(bin_dir + "missing_module/", bin_dir + "missing_module/config.yaml"));
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(rs), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file using a module with broken manifest (missing meta data)") {
        std::shared_ptr<Everest::ManagerSettings> rs = std::make_shared<Everest::ManagerSettings>(
            Everest::ManagerSettings(bin_dir + "broken_manifest_1/", bin_dir + "broken_manifest_1/config.yaml"));
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(rs), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file using a module with broken manifest (empty file)") {
        std::shared_ptr<Everest::ManagerSettings> rs = std::make_shared<Everest::ManagerSettings>(
            Everest::ManagerSettings(bin_dir + "broken_manifest_2/", bin_dir + "broken_manifest_2/config.yaml"));
        THEN("It should throw Everest::EverestConfigError") {
            // FIXME: an empty manifest breaks the test?
            CHECK_THROWS_AS(Everest::ManagerConfig(rs), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file using a module with broken manifest (broken module config)") {
        std::shared_ptr<Everest::ManagerSettings> rs = std::make_shared<Everest::ManagerSettings>(
            Everest::ManagerSettings(bin_dir + "broken_manifest_3/", bin_dir + "broken_manifest_3/config.yaml"));
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(rs), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file using a module with broken manifest (broken implementation config)") {
        std::shared_ptr<Everest::ManagerSettings> rs = std::make_shared<Everest::ManagerSettings>(
            Everest::ManagerSettings(bin_dir + "broken_manifest_4/", bin_dir + "broken_manifest_4/config.yaml"));
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(rs), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file with an unknown implementation config") {
        std::shared_ptr<Everest::ManagerSettings> rs = std::make_shared<Everest::ManagerSettings>(
            Everest::ManagerSettings(bin_dir + "unknown_impls/", bin_dir + "unknown_impls/config.yaml"));
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(rs), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file with an missing config entry") {
        std::shared_ptr<Everest::ManagerSettings> rs = std::make_shared<Everest::ManagerSettings>(
            Everest::ManagerSettings(bin_dir + "missing_config_entry/", bin_dir + "missing_config_entry/config.yaml"));
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(rs), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file with an missing implementation config entry") {
        std::shared_ptr<Everest::ManagerSettings> rs =
            std::make_shared<Everest::ManagerSettings>(Everest::ManagerSettings(
                bin_dir + "missing_impl_config_entry/", bin_dir + "missing_impl_config_entry/config.yaml"));
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(rs), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file with an invalid type of an implementation config entry") {
        std::shared_ptr<Everest::ManagerSettings> rs =
            std::make_shared<Everest::ManagerSettings>(Everest::ManagerSettings(
                bin_dir + "invalid_config_entry_type/", bin_dir + "invalid_config_entry_type/config.yaml"));
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(rs), Everest::EverestConfigError);
        }
    }
    GIVEN("A config file using a module with an invalid interface (missing "
          "interface)") {
        std::shared_ptr<Everest::ManagerSettings> rs = std::make_shared<Everest::ManagerSettings>(
            Everest::ManagerSettings(bin_dir + "missing_interface/", bin_dir + "missing_interface/config.yaml"));
        THEN("It should throw Everest::EverestConfigError") {
            CHECK_THROWS_AS(Everest::ManagerConfig(rs), Everest::EverestConfigError);
        }
    }
    GIVEN("A valid config") {
        std::shared_ptr<Everest::ManagerSettings> rs = std::make_shared<Everest::ManagerSettings>(
            Everest::ManagerSettings(bin_dir + "valid_config/", bin_dir + "valid_config/config.yaml"));
        THEN("It should not throw at all") {
            CHECK_NOTHROW(Everest::ManagerConfig(rs));
        }
    }
    GIVEN("A valid config with a valid module") {
        std::shared_ptr<Everest::ManagerSettings> rs = std::make_shared<Everest::ManagerSettings>(
            Everest::ManagerSettings(bin_dir + "valid_module_config/", bin_dir + "valid_module_config/config.yaml"));
        THEN("It should not throw at all") {
            CHECK_NOTHROW(Everest::ManagerConfig(rs));
        }
    }
    GIVEN("A valid config with a valid module and a user-config applied") {
        std::shared_ptr<Everest::ManagerSettings> rs =
            std::make_shared<Everest::ManagerSettings>(Everest::ManagerSettings(
                bin_dir + "valid_module_config_userconfig/", bin_dir + "valid_module_config_userconfig/config.yaml"));
        THEN("It should not throw at all") {
            CHECK_NOTHROW([&]() {
                auto mc = Everest::ManagerConfig(rs);
                auto main = mc.get_main_config();
                CHECK(main.at("valid_module").at("config_module").at("valid_config_entry") == "hi");
            }());
        }
    }
    GIVEN("A valid config with a valid module and enabled schema validation") {
        std::shared_ptr<Everest::ManagerSettings> rs =
            std::make_shared<Everest::ManagerSettings>(Everest::ManagerSettings(
                bin_dir + "valid_module_config_validate/", bin_dir + "valid_module_config_validate/config.yaml"));
        THEN("It should not throw at all") {
            CHECK_NOTHROW([&]() {
                auto mc = Everest::ManagerConfig(rs);
                auto interfaces = mc.get_interfaces();
                CHECK(interfaces.size() == 1);
                CHECK(interfaces.contains("TESTValidManifestCmdVar"));
                CHECK(interfaces.at("TESTValidManifestCmdVar").at("main") == "test_interface_cmd_var");
                auto types = mc.get_types();
                CHECK(types.size() == 1);
                CHECK(types.contains("/test_type"));
            }());
        }
    }
    GIVEN("A valid config with a valid module serialized") {
        std::shared_ptr<Everest::ManagerSettings> rs = std::make_shared<Everest::ManagerSettings>(
            Everest::ManagerSettings(bin_dir + "valid_module_config/", bin_dir + "valid_module_config/config.yaml"));
        THEN("It should not throw at all") {
            CHECK_NOTHROW([&]() {
                auto mc = Everest::ManagerConfig(rs);
                auto serialized = mc.serialize();
                CHECK(serialized.at("module_names").size() == 1);
                CHECK(serialized.at("module_names").at("valid_module") == "TESTValidManifest");
            }());
        }
    }
    GIVEN("A valid config in legacy json format with a valid module") {
        std::shared_ptr<Everest::ManagerSettings> rs =
            std::make_shared<Everest::ManagerSettings>(Everest::ManagerSettings(
                bin_dir + "valid_module_config_json/", bin_dir + "valid_module_config_json/config.json"));
        THEN("It should not throw at all") {
            CHECK_NOTHROW(Everest::ManagerConfig(rs));
        }
    }
}
