#include "test_framework.hpp"
#include "config.hpp"
#include <fstream>
#include <cstdio>

TEST_CASE(Config_BasicKeyValueParsing) {
    isp::Config cfg;
    std::string data = "key1=value1\nkey2 = value2\nkey3= value3 \n";
    ASSERT_TRUE(cfg.load_from_string(data));
    ASSERT_EQ(cfg.get_string("key1"), "value1");
    ASSERT_EQ(cfg.get_string("key2"), "value2");
    ASSERT_EQ(cfg.get_string("key3"), "value3");
}

TEST_CASE(Config_CommentsAndEmptyLines) {
    isp::Config cfg;
    std::string data = 
        "# This is a comment\n"
        "\n"
        "   \n"
        "; Another comment style\n"
        "video_device = /dev/video10 # inline comment\n"
        "poll_interval_ms = 500 ; semicolon comment\n";
    ASSERT_TRUE(cfg.load_from_string(data));
    ASSERT_EQ(cfg.get_string("video_device"), "/dev/video10");
    ASSERT_EQ(cfg.get_int("poll_interval_ms"), 500);
}

TEST_CASE(Config_TypeConversions) {
    isp::Config cfg;
    std::string data = 
        "cpu_weight = 800\n"
        "ratio = 0.75\n"
        "enabled = true\n"
        "disabled = false\n"
        "flag_yes = yes\n"
        "flag_no = no\n"
        "flag_on = on\n"
        "flag_off = off\n"
        "flag_1 = 1\n"
        "flag_0 = 0\n";
    ASSERT_TRUE(cfg.load_from_string(data));
    ASSERT_EQ(cfg.get_int("cpu_weight"), 800);
    ASSERT_EQ(cfg.get_double("ratio"), 0.75);
    ASSERT_TRUE(cfg.get_bool("enabled"));
    ASSERT_FALSE(cfg.get_bool("disabled"));
    ASSERT_TRUE(cfg.get_bool("flag_yes"));
    ASSERT_FALSE(cfg.get_bool("flag_no"));
    ASSERT_TRUE(cfg.get_bool("flag_on"));
    ASSERT_FALSE(cfg.get_bool("flag_off"));
    ASSERT_TRUE(cfg.get_bool("flag_1"));
    ASSERT_FALSE(cfg.get_bool("flag_0"));
}

TEST_CASE(Config_DefaultFallbacks) {
    isp::Config cfg;
    std::string data = "invalid_int = not_a_number\ninvalid_double = abc\n";
    ASSERT_TRUE(cfg.load_from_string(data));
    ASSERT_EQ(cfg.get_string("non_existent", "default_val"), "default_val");
    ASSERT_EQ(cfg.get_int("non_existent", 42), 42);
    ASSERT_EQ(cfg.get_int("invalid_int", 99), 99);
    ASSERT_EQ(cfg.get_double("invalid_double", 3.14), 3.14);
    ASSERT_FALSE(cfg.get_bool("non_existent", false));
    ASSERT_TRUE(cfg.get_bool("non_existent", true));
}

TEST_CASE(Config_SetAndHas) {
    isp::Config cfg;
    ASSERT_FALSE(cfg.has("test_key"));
    cfg.set("test_key", "test_value");
    ASSERT_TRUE(cfg.has("test_key"));
    ASSERT_EQ(cfg.get_string("test_key"), "test_value");
}

TEST_CASE(Config_ToResourcePolicy) {
    isp::Config cfg;
    std::string data = 
        "protected_cpu_weight = 900\n"
        "background_cpu_weight = 30\n"
        "starvation_floor_cpu_weight = 10\n"
        "aging_increment = 15\n"
        "video_device = /dev/video5\n"
        "dry_run = true\n";
    ASSERT_TRUE(cfg.load_from_string(data));
    isp::ResourcePolicy pol = cfg.to_resource_policy();
    ASSERT_EQ(pol.protected_cpu_weight, 900u);
    ASSERT_EQ(pol.background_cpu_weight, 30u);
    ASSERT_EQ(pol.starvation_floor_cpu_weight, 10u);
    ASSERT_EQ(pol.aging_increment, 15u);
    ASSERT_EQ(pol.video_device, "/dev/video5");
    ASSERT_TRUE(pol.dry_run);
    // Verify untouched defaults remain standard
    ASSERT_EQ(pol.protected_io_weight, 800u);
    ASSERT_EQ(pol.hysteresis_delay_sec, 8u);
}

TEST_CASE(Config_LoadFromFile) {
    std::string tmp_filename = "temp_test_cfg.conf";
    {
        std::ofstream out(tmp_filename);
        out << "temp_key = temp_value\n";
        out << "temp_int = 12345\n";
    }

    isp::Config cfg;
    ASSERT_TRUE(cfg.load_from_file(tmp_filename));
    ASSERT_EQ(cfg.get_string("temp_key"), "temp_value");
    ASSERT_EQ(cfg.get_int("temp_int"), 12345);

    std::remove(tmp_filename.c_str());
    ASSERT_FALSE(cfg.load_from_file("non_existent_file_xyz.conf"));
}

TEST_CASE(Config_WorkloadThrottlingOptions) {
    isp::Config cfg;
    std::string data =
        "throttle_games_and_engines = true\n"
        "throttle_heavy_background = false\n"
        "background_cpu_max_percent = 5\n"
        "custom_game_patterns = my_custom_game, super_engine\n"
        "custom_background_patterns = heavy_batch_runner\n";

    ASSERT_TRUE(cfg.load_from_string(data));
    isp::ResourcePolicy pol = cfg.to_resource_policy();

    ASSERT_TRUE(pol.throttle_games_and_engines);
    ASSERT_FALSE(pol.throttle_heavy_background);
    ASSERT_EQ(pol.background_cpu_max_percent, 5u);
    ASSERT_EQ(pol.custom_game_patterns, "my_custom_game, super_engine");
    ASSERT_EQ(pol.custom_background_patterns, "heavy_batch_runner");
}
