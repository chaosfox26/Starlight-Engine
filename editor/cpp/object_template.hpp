#pragma once
#include "platform.hpp"
namespace starlight::editor {
std::string encode_object_template(const Character&);
Character decode_object_template(std::string_view);
void save_object_template(const Character&, const fs::path&);
Character load_object_template(const fs::path&);
void place_object_template(Character&, double x, double z);
std::string encode_object_group(const std::vector<Character>&);
std::vector<Character> decode_object_group(std::string_view);
void save_object_group(const std::vector<Character>&, const fs::path&);
std::vector<Character> load_object_group(const fs::path&);
void renew_object_ids(std::vector<Character>&);
void place_object_group(std::vector<Character>&, SceneData&, double x, double z,
                        bool grid = false, bool objects = false, bool ground = false,double gridStep=.5);
}
