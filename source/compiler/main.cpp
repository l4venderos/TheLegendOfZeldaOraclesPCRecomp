#include "oracles/analysis.hpp"
#include "oracles/emitter.hpp"
#include "oracles/rom.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
static void usage() {
    std::cout << "Oracles independent static recompiler prototype\n"
              << "Usage: oracles-recompiler --game ages|seasons --input ROM.gbc --output generated.cpp --manifest game.orcmeta [--max-blocks N]\n";
}
int main(int argc,char** argv) {
    try {
        std::string game,input,output,manifest;
        std::size_t max_blocks=12000;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            auto value=[&]() -> std::string { if(i+1>=argc) throw std::runtime_error("Missing value after "+arg); return argv[++i]; };
            if(arg=="--game") game=value();
            else if(arg=="--input") input=value();
            else if(arg=="--output") output=value();
            else if(arg=="--manifest") manifest=value();
            else if(arg=="--max-blocks") max_blocks=static_cast<std::size_t>(std::stoull(value()));
            else if(arg=="--help" || arg=="-h") { usage(); return 0; }
            else throw std::runtime_error("Unknown argument: "+arg);
        }
        if((game!="ages" && game!="seasons") || input.empty() || output.empty() || manifest.empty()) { usage(); return 2; }
        const fs::path input_path=input, output_path=output, manifest_path=manifest;
        fs::create_directories(output_path.parent_path().empty()?fs::path("."):output_path.parent_path());
        fs::create_directories(manifest_path.parent_path().empty()?fs::path("."):manifest_path.parent_path());
        std::cout << "[1/4] Reading user-supplied ROM...\n";
        const auto rom=oracles::RomImage::load(input_path);
        if(rom.bank_count>512) throw std::runtime_error("ROM exceeds the 9-bit MBC5 bank address range.");
        const bool mbc5=rom.cartridge_type>=0x19 && rom.cartridge_type<=0x1E;
        if(!mbc5) std::cerr << "[warning] Header cartridge type is not a known MBC5 type (0x" << std::hex << unsigned(rom.cartridge_type) << std::dec << "). Banked analysis may be incomplete.\n";
        std::cout << "       Title: " << (rom.title.empty()?"(untitled)":rom.title) << "\n"
                  << "       Size: " << rom.bytes.size() << " bytes / " << rom.bank_count << " banks\n";
        std::cout << "[2/4] Discovering statically reachable SM83 blocks...\n";
        const auto analysis=oracles::analyze_rom(rom,max_blocks,32);
        std::cout << "       Native blocks: " << analysis.blocks.size() << "\n"
                  << "       Decoded instructions: " << analysis.decoded_instructions << "\n"
                  << "       Rejected paths: " << analysis.rejected_paths << "\n";
        std::cout << "[3/4] Emitting native C++ basic blocks...\n";
        oracles::emit_native_cpp(game,rom,analysis,output_path);
        std::cout << "[4/4] Writing build metadata...\n";
        oracles::emit_manifest(game,input_path,rom,analysis,manifest_path);
        std::cout << "Done. Generated native code: " << output_path << "\n"
                  << "Note: discovered blocks are native; undiscovered/dynamic targets use the in-tree SM83 runtime fallback.\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "[error] " << e.what() << "\n";
        return 1;
    }
}
