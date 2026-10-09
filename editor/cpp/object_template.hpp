#pragma once
#include "platform.hpp"
#include <unordered_set>
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
// Clears a joint or lightning link whose target is not in `present` (a pasted or exported object whose target is not coming along would otherwise stop Play: EDIT-5).
void drop_unresolved_links(std::vector<Character>&, const std::unordered_set<std::string>& present);
// Names of the gameplay rules whose subject or open-door target is one of these object ids: deleting the objects leaves those rules pointing at nothing.
std::vector<std::string> rules_naming_objects(const SceneData&, const std::unordered_set<std::string>& ids);
void place_object_group(std::vector<Character>&, SceneData&, double x, double z,
                        bool grid = false, bool objects = false, bool ground = false,double gridStep=.5);
}
