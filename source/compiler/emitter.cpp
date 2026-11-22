#include "oracles/emitter.hpp"
#include "oracles/rom.hpp"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace oracles {
namespace {
std::string hx(unsigned v, unsigned width=4) {
    std::ostringstream s; s << "0x" << std::hex << std::uppercase << std::setw(width) << std::setfill('0') << v; return s.str();
}
void line(std::ostream& o, const std::string& s) { o << "    " << s << "\n"; }
std::string cycles_for(const DecodedInstruction& i) {
    if (i.is_cb) {
        if ((i.extended_opcode & 7) != 6) return "8";
        return ((i.extended_opcode >> 6) == 1) ? "12" : "16";
    }
    const unsigned op = i.opcode;
    if (op == 0x76) return "4";                         // HALT
    if ((op & 0xE7) == 0x20) {
        const unsigned condition = ((op >> 3) & 7) - 4;
        return "(r.condition(" + std::to_string(condition) + ") ? 12 : 8)";
    }
    if ((op & 0xE7) == 0xC2) {
        const unsigned condition = (op >> 3) & 3;
        return "(r.condition(" + std::to_string(condition) + ") ? 16 : 12)";
    }
    if ((op & 0xE7) == 0xC4) {
        const unsigned condition = (op >> 3) & 3;
        return "(r.condition(" + std::to_string(condition) + ") ? 24 : 12)";
    }
    if ((op & 0xE7) == 0xC0) {
        const unsigned condition = (op >> 3) & 3;
        return "(r.condition(" + std::to_string(condition) + ") ? 20 : 8)";
    }

    if (op == 0x08) return "20";                         // LD (a16),SP
    if (op == 0xCD) return "24";                         // CALL a16
    if (op == 0xC3 || op == 0xEA || op == 0xFA) return "16"; // JP / LD (a16),A / LD A,(a16)
    if (op == 0xC9 || op == 0xD9) return "16";           // RET / RETI
    if (op == 0xE8) return "16";                         // ADD SP,e8
    if (op == 0xF8) return "12";                         // LD HL,SP+e8
    if (op == 0xF9) return "8";                          // LD SP,HL
    if (op == 0xE0 || op == 0xF0) return "12";           // LDH (a8),A / A,(a8)
    if (op == 0xE2 || op == 0xF2) return "8";            // LDH (C),A / A,(C)
    if (op == 0x18) return "12";                         // JR e8
    if (op == 0xE9) return "4";                          // JP HL
    if ((op & 0xCF) == 0xC1) return "12";               // POP rr
    if ((op & 0xCF) == 0xC5) return "16";               // PUSH rr
    if ((op & 0xCF) == 0x09) return "8";                // ADD HL,rr
    if ((op & 0xCF) == 0x01) return "12";               // LD rr,d16
    if ((op & 0xCF) == 0x03 || (op & 0xCF) == 0x0B) return "8";
    if (op == 0x02 || op == 0x12 || op == 0x22 || op == 0x32 ||
        op == 0x0A || op == 0x1A || op == 0x2A || op == 0x3A) return "8";
    if (op == 0x36) return "12";
    if ((op & 0xC7) == 0x06) return (((op >> 3) & 7) == 6) ? "12" : "8";
    if ((op & 0xC7) == 0x04 || (op & 0xC7) == 0x05)
        return (((op >> 3) & 7) == 6) ? "12" : "4";
    if ((op & 0xC0) == 0x40)
        return ((op & 7) == 6 || ((op >> 3) & 7) == 6) ? "8" : "4";
    if ((op & 0xC0) == 0x80) return ((op & 7) == 6) ? "8" : "4";
    if ((op & 0xC7) == 0xC7) return "16";               // RST
    if (op == 0x00 || op == 0x10 || op == 0x76 || op == 0xF3 || op == 0xFB) return "4";
    return "4";
}
void emit_return_next(std::ostream& o, std::uint16_t next) {
    line(o, "c.pc = " + hx(next) + ";");
    line(o, "return c.pc;");
}
void emit_instruction(std::ostream& o, const DecodedInstruction& i, std::uint16_t bank) {
    const unsigned op=i.opcode, x=op>>6, y=(op>>3)&7, z=op&7, p=y>>1, q=y&1;
    const auto next=static_cast<std::uint16_t>(i.address+i.length);
    if (!i.legal) { line(o, "c.faulted = true; return kNoNativeBlock;"); return; }
    if (i.is_cb) {
        const unsigned cb=i.extended_opcode, cx=cb>>6, cy=(cb>>3)&7, cz=cb&7;
        if(cx==0) line(o, "r.cb_rotate("+std::to_string(cy)+", "+std::to_string(cz)+");");
        else if(cx==1) line(o, "r.cb_bit("+std::to_string(cy)+", "+std::to_string(cz)+");");
        else if(cx==2) line(o, "r.cb_res("+std::to_string(cy)+", "+std::to_string(cz)+");");
        else line(o, "r.cb_set("+std::to_string(cy)+", "+std::to_string(cz)+");");
        return;
    }
    if (x==1) {
        if (op==0x76) { line(o,"if (!c.ime && (r.read8(0xFF0F) & r.interrupt_enable)) c.halt_bug = true; else c.halted = true;"); emit_return_next(o,next); return; }
        line(o,"r.set_r8("+std::to_string(y)+", r.get_r8("+std::to_string(z)+"));"); return;
    }
    if (x==2) { line(o,"r.alu("+std::to_string(y)+", r.get_r8("+std::to_string(z)+"));"); return; }
    if (x==0) {
        switch(z) {
        case 0:
            if(y==0) return;
            if(y==1) { line(o,"r.write16("+hx(i.immediate16)+", c.sp);"); return; }
            if(y==2) { line(o,"if (!r.handle_stop_speed_switch()) c.stopped = true;"); emit_return_next(o,next); return; }
            if(y==3 || y>=4) {
                const unsigned condition=y<4?0:y-4;
                const auto target=static_cast<std::uint16_t>(next+static_cast<std::int8_t>(i.immediate8));
                if(y==3) { line(o,"c.pc = "+hx(target)+";"); line(o,"return c.pc;"); }
                else { line(o,"if (r.condition("+std::to_string(condition)+")) c.pc = "+hx(target)+"; else c.pc = "+hx(next)+";"); line(o,"return c.pc;"); }
                return;
            }
            break;
        case 1:
            if(!q) line(o,"r.set_r16("+std::to_string(p)+", "+hx(i.immediate16)+");");
            else line(o,"r.add_hl(r.get_r16("+std::to_string(p)+"));");
            return;
        case 2: {
            const unsigned pair=p;
            const char* regs[]={"r.get_r16(0)","r.get_r16(1)","r.get_hl()","r.get_hl()"};
            const char* addr=regs[pair];
            if(!q) line(o,std::string("r.write8(")+addr+", c.a);");
            else line(o,std::string("c.a = r.read8(")+addr+");");
            if(pair==2) line(o,"r.set_hl(static_cast<std::uint16_t>(r.get_hl()+1));");
            if(pair==3) line(o,"r.set_hl(static_cast<std::uint16_t>(r.get_hl()-1));");
            return;
        }
        case 3: line(o,"r.set_r16("+std::to_string(p)+", static_cast<std::uint16_t>(r.get_r16("+std::to_string(p)+") "+(q?"-":"+")+" 1));"); return;
        case 4: line(o,"r.inc8("+std::to_string(y)+");"); return;
        case 5: line(o,"r.dec8("+std::to_string(y)+");"); return;
        case 6: line(o,"r.set_r8("+std::to_string(y)+", "+hx(i.immediate8,2)+");"); return;
        case 7:
            if(y<=3) line(o,"r.rotate_a("+std::to_string(y)+");");
            else if(y==4) line(o,"r.decimal_adjust();");
            else if(y==5) line(o,"r.complement_a();");
            else if(y==6) line(o,"r.set_carry(true);");
            else line(o,"r.complement_carry();");
            return;
        }
    }
    if (x==3) {
        switch(z) {
        case 0:
            if(y<4) {
                const auto cond=y;
                line(o,"if (r.condition("+std::to_string(cond)+")) c.pc = r.pop(); else c.pc = "+hx(next)+";");
                line(o,"return c.pc;"); return;
            }
            if(y==4) { line(o,"r.write8(static_cast<std::uint16_t>(0xFF00 | "+hx(i.immediate8,2)+"), c.a);"); return; }
            if(y==5) { line(o,"r.add_sp_signed(static_cast<std::int8_t>("+hx(i.immediate8,2)+"), false);"); return; }
            if(y==6) { line(o,"c.a = r.read8(static_cast<std::uint16_t>(0xFF00 | "+hx(i.immediate8,2)+"));"); return; }
            line(o,"r.add_sp_signed(static_cast<std::int8_t>("+hx(i.immediate8,2)+"), true);"); return;
        case 1:
            if(!q) { if(p==3) { line(o,"{ const auto value = r.pop(); c.a = static_cast<std::uint8_t>(value >> 8); c.f = static_cast<std::uint8_t>(value) & 0xF0; }"); } else line(o,"r.set_r16("+std::to_string(p)+", r.pop());"); return; }
            if(p==0 || p==1) { line(o,"c.pc = r.pop();"); if(p==1) line(o,"c.ime = true; c.ime_delay = 0;"); line(o,"return c.pc;"); return; }
            if(p==2) { line(o,"c.pc = r.get_hl(); return c.pc;"); return; }
            line(o,"c.sp = r.get_hl();"); return;
        case 2:
            if(y<4) {
                line(o,"if (r.condition("+std::to_string(y)+")) c.pc = "+hx(i.immediate16)+"; else c.pc = "+hx(next)+";");
                line(o,"return c.pc;"); return;
            }
            if(y==4) { line(o,"r.write8(static_cast<std::uint16_t>(0xFF00 | c.c), c.a);"); return; }
            if(y==5) { line(o,"r.write8("+hx(i.immediate16)+", c.a);"); return; }
            if(y==6) { line(o,"c.a = r.read8(static_cast<std::uint16_t>(0xFF00 | c.c));"); return; }
            line(o,"c.a = r.read8("+hx(i.immediate16)+");"); return;
        case 3:
            if(y==0) { line(o,"c.pc = "+hx(i.immediate16)+"; return c.pc;"); return; }
            if(y==6) { line(o,"c.ime = false; c.ime_delay = 0;"); return; }
            if(y==7) { line(o,"if (!c.ime) c.ime_delay = 2;"); return; }
            line(o,"c.faulted = true; return kNoNativeBlock;"); return;
        case 4:
            if(y<4) {
                line(o,"if (r.condition("+std::to_string(y)+")) { r.push("+hx(next)+"); c.pc = "+hx(i.immediate16)+"; } else c.pc = "+hx(next)+";");
                line(o,"return c.pc;"); return;
            }
            line(o,"c.faulted = true; return kNoNativeBlock;"); return;
        case 5:
            if(!q) {
                if(p==3) line(o,"r.push(static_cast<std::uint16_t>((std::uint16_t(c.a)<<8) | (c.f & 0xF0)));");
                else line(o,"r.push(r.get_r16("+std::to_string(p)+"));");
                return;
            }
            if(p==0) { line(o,"r.push("+hx(next)+"); c.pc = "+hx(i.immediate16)+"; return c.pc;"); return; }
            line(o,"c.faulted = true; return kNoNativeBlock;"); return;
        case 6: line(o,"r.alu("+std::to_string(y)+", "+hx(i.immediate8,2)+");"); return;
        case 7: line(o,"r.push("+hx(next)+"); c.pc = "+hx(y*8)+"; return c.pc;"); return;
        }
    }
    line(o,"c.faulted = true; return kNoNativeBlock;");
    (void)bank;
}
}

void emit_native_cpp(const std::string& game_id, const RomImage&, const AnalysisResult& analysis, const std::filesystem::path& output) {
    const auto id=sanitize_identifier(game_id);
    std::ofstream o(output);
    if(!o) throw std::runtime_error("Cannot write generated C++: "+output.string());
    o << "// Generated by the independent Oracles PC Recomp compiler. Do not edit.\n"
      << "#include \"oracles/runtime.hpp\"\n#include <cstdint>\n\nnamespace oracles::generated {\n";
    std::vector<std::pair<std::uint32_t,std::string>> map;
    for (const auto& block : analysis.blocks) {
        const auto bname=id+"_b"+std::to_string(block.start.bank)+"_p"+std::to_string(block.start.address);
        o << "static std::uint16_t " << bname << "(Runtime& r) {\n    auto& c = r.cpu;\n";
        bool ended=false;
        for (const auto& ins : block.instructions) {
            line(o, "{ auto cycle_scope = r.defer_cycles(" + cycles_for(ins) + ");");
            emit_instruction(o,ins,block.start.bank);
            line(o, "}");
            const auto op=ins.opcode;
            const bool control_boundary = !ins.is_cb &&
                (op==0x18 || op==0xC3 || op==0xCD || op==0xC9 || op==0xD9 || op==0xE9 || op==0x76 || op==0x10 ||
                 (op&0xE7)==0x20 || (op&0xE7)==0xC0 || (op&0xE7)==0xC2 || (op&0xE7)==0xC4 || (op&0xC7)==0xC7);
            if (control_boundary) { ended=true; break; }
            const auto next = static_cast<std::uint16_t>(ins.address + ins.length);
            line(o, "c.pc = " + hx(next) + ";");
            line(o, "if (r.should_yield_native_block()) return c.pc;");
        }
        if (!ended) {
            const auto& last=block.instructions.back();
            emit_return_next(o,static_cast<std::uint16_t>(last.address+last.length));
        }
        o << "}\n";
        const auto key=(std::uint32_t(block.start.bank)<<16)|block.start.address;
        map.emplace_back(key,bname);
    }
    o << "\nstd::uint16_t " << id << "_step(Runtime& r, std::uint16_t bank, std::uint16_t pc) {\n"
      << "    const std::uint32_t key = (std::uint32_t(pc < 0x4000 ? 0 : bank) << 16) | pc;\n    switch (key) {\n";
    for (const auto& [key,name] : map) o << "    case " << hx(key,8) << ": return " << name << "(r);\n";
    o << "    default: return kNoNativeBlock;\n    }\n}\n"
      << "std::size_t " << id << "_block_count() { return " << map.size() << "; }\n"
      << "}\n";
}

void emit_manifest(const std::string& game_id, const std::filesystem::path& input, const RomImage& rom,
                   const AnalysisResult& analysis, const std::filesystem::path& output) {
    std::ofstream o(output);
    if(!o) throw std::runtime_error("Cannot write manifest: "+output.string());
    o << "{\n"
      << "  \"format\": \"oracles-native-block-map-v1\",\n"
      << "  \"game\": \"" << game_id << "\",\n"
      << "  \"source_rom_filename\": \"" << input.filename().string() << "\",\n"
      << "  \"source_rom_sha256\": \"" << sha256_file(input) << "\",\n"
      << "  \"source_rom_bytes\": " << rom.bytes.size() << ",\n"
      << "  \"cartridge_title\": \"" << rom.title << "\",\n"
      << "  \"cartridge_type\": " << unsigned(rom.cartridge_type) << ",\n"
      << "  \"rom_banks\": " << rom.bank_count << ",\n"
      << "  \"native_blocks\": " << analysis.blocks.size() << ",\n"
      << "  \"decoded_instructions\": " << analysis.decoded_instructions << ",\n"
      << "  \"rejected_paths\": " << analysis.rejected_paths << ",\n"
      << "  \"note\": \"Compiler-development artifact; this manifest does not certify complete game coverage.\"\n"
      << "}\n";
}
}
