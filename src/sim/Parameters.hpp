#pragma once

#include <span>

// Generic description of a tweakable parameter. Scenarios expose a list of
// these and the UI renders them, so scenarios never depend on ImGui and the UI
// can be restyled without touching simulation code.
namespace bh {

struct ParamDesc {
  enum class Type { Float, Int, Bool, Enum };

  Type type = Type::Float;
  const char* label = "";
  const char* help = nullptr;  // tooltip
  void* value = nullptr;       // float*, int*, bool* or int* (enum index)
  float min = 0.0f;
  float max = 1.0f;
  const char* format = nullptr;
  bool logarithmic = false;
  bool requiresReset = false;  // the scenario must be re-spawned after a change
  std::span<const char* const> options{};

  static ParamDesc slider(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f",
                          bool requiresReset = false, const char* help = nullptr, bool log = false) {
    ParamDesc d;
    d.type = Type::Float;
    d.label = label;
    d.value = v;
    d.min = lo;
    d.max = hi;
    d.format = fmt;
    d.requiresReset = requiresReset;
    d.help = help;
    d.logarithmic = log;
    return d;
  }
  static ParamDesc integer(const char* label, int* v, int lo, int hi, bool requiresReset = true,
                           const char* help = nullptr, bool log = false) {
    ParamDesc d;
    d.type = Type::Int;
    d.label = label;
    d.value = v;
    d.min = static_cast<float>(lo);
    d.max = static_cast<float>(hi);
    d.requiresReset = requiresReset;
    d.help = help;
    d.logarithmic = log;
    return d;
  }
  static ParamDesc toggle(const char* label, bool* v, bool requiresReset = false, const char* help = nullptr) {
    ParamDesc d;
    d.type = Type::Bool;
    d.label = label;
    d.value = v;
    d.requiresReset = requiresReset;
    d.help = help;
    return d;
  }
  static ParamDesc choice(const char* label, int* v, std::span<const char* const> options, bool requiresReset = true,
                          const char* help = nullptr) {
    ParamDesc d;
    d.type = Type::Enum;
    d.label = label;
    d.value = v;
    d.options = options;
    d.requiresReset = requiresReset;
    d.help = help;
    return d;
  }
};

}  // namespace bh
