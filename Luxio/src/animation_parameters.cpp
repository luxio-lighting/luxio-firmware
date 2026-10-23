#include "animation_parameters.h"
#include <string>
#include <cstring>
#include <cstdlib>
#include <cmath>

const char *parseAnimationParameters(const char *source, luxio_AnimationParameter *parameters, pb_size_t &count) {
  count = 0;
  bool header = true;
  for (const char *cursor = source; *cursor;) {
    const char *end = strchr(cursor, '\n');
    std::string line(cursor, end ? size_t(end - cursor) : strlen(cursor));
    cursor += line.size() + (end ? 1 : 0);
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos) continue;
    line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
    if (line.compare(0, 13, "-- @parameter") != 0) {
      if (line.compare(0, 2, "--") != 0 || line.compare(0, 3, "--[") == 0) header = false;
      continue;
    }
    if (!header) return "Parameter declarations must be at the top of the animation";
    if (line.compare(0, 14, "-- @parameter ") != 0) return "Invalid parameter declaration";
    if (count >= 16) return "An animation supports at most 16 parameters";
    line = line.substr(14); line = line.substr(0, line.find("//"));
    std::string parts[6]; size_t size = 0, offset = 0;
    while (size < 6) {
      auto comma = line.find(',', offset);
      auto value = line.substr(offset, comma == std::string::npos ? comma : comma - offset);
      const auto start = value.find_first_not_of(" \t\r");
      parts[size++] = start == std::string::npos ? "" : value.substr(start, value.find_last_not_of(" \t\r") - start + 1);
      if (comma == std::string::npos) break;
      offset = comma + 1;
    }
    const auto &name = parts[0];
    if (name.empty() || name.size() > 32 || !((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z'))) return "Invalid parameter name";
    for (char c : name) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ' ')) return "Invalid parameter name";
    for (size_t i = 0; i < count; ++i) if (name == parameters[i].name) return "Duplicate parameter name";
    auto &p = parameters[count]; p = {}; memcpy(p.name, name.c_str(), name.size() + 1);
    if ((parts[1] == "Checkbox" || parts[1] == "Boolean") && size == 3 && (parts[2] == "true" || parts[2] == "false")) {
      p.boolean = true; p.value = p.default_value = parts[2] == "true" ? 1 : 0; p.maximum = 1;
    } else if ((parts[1] == "Slider" || parts[1] == "Number") && size == 5) {
      p.number_input = parts[1] == "Number";
      double values[3];
      for (size_t i = 0; i < 3; ++i) {
        const auto &text = parts[i + 2];
        if (text.empty() || text.find_first_not_of("0123456789+-.eE") != std::string::npos) return "Invalid numeric parameter";
        char *stop = nullptr; values[i] = strtod(text.c_str(), &stop);
        if (stop == text.c_str() || *stop || !std::isfinite(values[i]) || fabs(values[i]) > 1e9) return "Invalid numeric parameter";
      }
      p.value = p.default_value = values[0]; p.minimum = values[1]; p.maximum = values[2];
      if (p.minimum >= p.maximum || p.value < p.minimum || p.value > p.maximum) return "Invalid parameter range or default";
    } else return "Invalid parameter declaration";
    ++count;
  }
  return nullptr;
}

const char *updateAnimationParameters(luxio_AnimationParameter *parameters, size_t count, const luxio_AnimationParameter *patch, size_t patchCount) {
  if (count > 16 || patchCount > 16) return "An animation supports at most 16 parameters";
  // Validate every entry before changing anything, including duplicate names.
  for (size_t i = 0; i < patchCount; ++i) {
    for (size_t j = 0; j < i; ++j) if (!strcmp(patch[i].name, patch[j].name)) return "Duplicate parameter name";
    const luxio_AnimationParameter *found = nullptr;
    for (size_t j = 0; j < count; ++j) if (!strcmp(parameters[j].name, patch[i].name)) found = &parameters[j];
    if (!found) return "Unknown animation parameter";
    if (found->boolean != patch[i].boolean || !std::isfinite(patch[i].value) || patch[i].value < found->minimum || patch[i].value > found->maximum || (found->boolean && patch[i].value != 0 && patch[i].value != 1)) return "Invalid animation parameter value";
  }
  for (size_t i = 0; i < patchCount; ++i) for (size_t j = 0; j < count; ++j) if (!strcmp(parameters[j].name, patch[i].name)) parameters[j].value = patch[i].value;
  return nullptr;
}

const char *parseAnimationPreview(const char *source, luxio_Color *colors, pb_size_t &count) {
  count = 0;
  bool header = true, found = false;
  for (const char *cursor = source; *cursor;) {
    const char *end = strchr(cursor, '\n');
    std::string line(cursor, end ? size_t(end - cursor) : strlen(cursor));
    cursor += line.size() + (end ? 1 : 0);
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos) continue;
    line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
    if (line.compare(0, 11, "-- @preview") != 0) {
      if (line.compare(0, 2, "--") != 0 || line.compare(0, 3, "--[") == 0) header = false;
      continue;
    }
    if (!header) return "Preview must be at the top of the animation";
    if (found) return "Duplicate animation preview";
    if (line.compare(0, 12, "-- @preview ") != 0) return "Invalid animation preview";
    found = true;
    line = line.substr(12); line = line.substr(0, line.find("//"));
    size_t offset = 0;
    for (;;) {
      if (count == 8) return "Preview supports two to eight colors";
      auto comma = line.find(',', offset);
      auto value = line.substr(offset, comma == std::string::npos ? comma : comma - offset);
      const auto start = value.find_first_not_of(" \t\r");
      value = start == std::string::npos ? "" : value.substr(start, value.find_last_not_of(" \t\r") - start + 1);
      if (!value.empty() && value[0] == '#') value.erase(0, 1);
      if (value.size() != 6 || value.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return "Preview colors must be six-digit RGB hex";
      auto rgb = strtoul(value.c_str(), nullptr, 16);
      colors[count++] = {uint32_t(rgb >> 16), uint32_t((rgb >> 8) & 255), uint32_t(rgb & 255), 0, 0, 0};
      if (comma == std::string::npos) break;
      offset = comma + 1;
    }
    if (count < 2) return "Preview supports two to eight colors";
  }
  return nullptr;
}
