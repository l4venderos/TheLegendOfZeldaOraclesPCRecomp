#pragma once
#include "oracles/analysis.hpp"
#include <filesystem>
#include <string>

namespace oracles {
void emit_native_cpp(const std::string& game_id, const RomImage& rom,
                     const AnalysisResult& analysis,
                     const std::filesystem::path& output);
void emit_manifest(const std::string& game_id, const std::filesystem::path& input,
                   const RomImage& rom, const AnalysisResult& analysis,
                   const std::filesystem::path& output);
}
