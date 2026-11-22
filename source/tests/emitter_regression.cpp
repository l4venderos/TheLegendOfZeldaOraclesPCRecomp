#include "oracles/emitter.hpp"
#include "oracles/sm83.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

int main(int argc, char** argv) {
    using namespace oracles;
    try {
        if (argc != 2) throw std::runtime_error("usage: emitter-regression <output.cpp>");

        AnalysisResult analysis;
        NativeBlock block;
        block.start = {0, 0x0100};
        block.instructions.push_back(decode_sm83(0x0100, 0xF1, 0, 0)); // POP AF
        block.instructions.push_back(decode_sm83(0x0101, 0x00, 0, 0)); // NOP
        block.instructions.push_back(decode_sm83(0x0102, 0xF1, 0, 0)); // POP AF again in same block
        block.instructions.push_back(decode_sm83(0x0103, 0x00, 0, 0)); // NOP
        analysis.blocks.push_back(std::move(block));

        NativeBlock conditional;
        conditional.start = {0, 0x0200};
        conditional.instructions.push_back(decode_sm83(0x0200, 0x20, 0x02, 0)); // JR NZ,+2
        analysis.blocks.push_back(std::move(conditional));

        RomImage empty_rom;
        const std::filesystem::path output = argv[1];
        emit_native_cpp("emitter_regression", empty_rom, analysis, output);

        std::ifstream input(output, std::ios::binary);
        if (!input) throw std::runtime_error("could not read generated regression source");
        const std::string generated{std::istreambuf_iterator<char>(input), {}};
        const std::string scoped_pop = "{ const auto value = r.pop();";
        std::size_t count = 0;
        for (std::size_t pos = 0; (pos = generated.find(scoped_pop, pos)) != std::string::npos; pos += scoped_pop.size()) ++count;
        if (count != 2) throw std::runtime_error("expected two separately scoped POP AF temporaries");
        if (generated.find("r.defer_cycles(12)") == std::string::npos)
            throw std::runtime_error("generated instruction is missing its deferred cycle scope");
        if (generated.find("r.defer_cycles((r.condition(0) ? 12 : 8))") == std::string::npos)
            throw std::runtime_error("conditional JR must emit path-dependent CPU/PPU cycle counts");

        std::cout << "Native emitter scope and path-dependent cycle regressions passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Emitter regression failed: " << ex.what() << '\n';
        return 1;
    }
}
