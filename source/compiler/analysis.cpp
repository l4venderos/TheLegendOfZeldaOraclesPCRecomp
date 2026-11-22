#include "oracles/analysis.hpp"
#include <array>
#include <deque>
#include <set>
#include <tuple>

namespace oracles {
namespace {
struct Key { std::uint16_t bank, pc; bool operator<(const Key& o) const { return std::tie(bank,pc)<std::tie(o.bank,o.pc); } };
bool is_conditional_jr(std::uint8_t op) { return op==0x20 || op==0x28 || op==0x30 || op==0x38; }
bool is_call(std::uint8_t op) { return op==0xCD || op==0xC4||op==0xCC||op==0xD4||op==0xDC; }
bool is_jp(std::uint8_t op) { return op==0xC3||op==0xE9||op==0xC2||op==0xCA||op==0xD2||op==0xDA; }
bool is_jr(std::uint8_t op) { return op==0x18||is_conditional_jr(op); }
bool is_ret(std::uint8_t op) { return op==0xC9||op==0xD9||op==0xC0||op==0xC8||op==0xD0||op==0xD8; }
// A block must end after a store that could touch the cartridge mapper. The
// next dispatcher lookup then observes the newly selected MBC5 bank instead
// of continuing through C++ decoded from the previous bank.
bool may_write_mapper(const DecodedInstruction& ins) {
    if (ins.is_cb) {
        const unsigned x=ins.extended_opcode>>6;
        const unsigned z=ins.extended_opcode&7;
        return z==6 && x!=1; // CB read/modify/write on (HL), excluding BIT.
    }
    const auto op=ins.opcode;
    if (op==0x02 || op==0x12 || op==0x22 || op==0x32 ||
        op==0x34 || op==0x35 || op==0x36 ||
        (op>=0x70 && op<=0x75) || op==0x77 ||
        (op&0xCF)==0xC5) return true; // Indirect stores, (HL) stores, PUSH.
    if (op==0x08 || op==0xEA) return ins.immediate16<0x8000;
    return false;
}
std::uint16_t mapped_bank(std::uint16_t bank, std::uint16_t address) { return address < 0x4000 ? 0 : bank; }
}
AnalysisResult analyze_rom(const RomImage& rom, std::size_t max_blocks, std::size_t max_ins) {
    AnalysisResult result;
    if (rom.bank_count==0) return result;
    std::deque<Key> work;
    std::set<Key> queued, emitted;
    auto add=[&](std::uint16_t bank,std::uint16_t pc) {
        if (pc>=0x8000) return;
        bank = mapped_bank(bank,pc);
        if (bank>=rom.bank_count) return;
        Key k{bank,pc};
        if (queued.insert(k).second) work.push_back(k);
    };
    add(0,0x0100);
    for (std::uint16_t v : std::array<std::uint16_t,5>{0x0040,0x0048,0x0050,0x0058,0x0060}) add(0,v);
    // Seed each switchable ROM bank's entry window. Direct calls/jumps found in
    // the graph add further concrete addresses in their corresponding banks.
    for (std::size_t b=1;b<rom.bank_count;++b) add(static_cast<std::uint16_t>(b),0x4000);

    while (!work.empty() && result.blocks.size()<max_blocks) {
        const Key k=work.front(); work.pop_front();
        if (emitted.contains(k)) continue;
        emitted.insert(k);
        NativeBlock block; block.start={k.bank,k.pc};
        std::uint16_t pc=k.pc;
        for (std::size_t count=0;count<max_ins;++count) {
            if (pc>=0x8000 || (pc<0x4000 && k.bank!=0)) break;
            const auto b0=rom.read(pc,k.bank);
            const auto b1=rom.read(static_cast<std::uint16_t>(pc+1),k.bank);
            const auto b2=rom.read(static_cast<std::uint16_t>(pc+2),k.bank);
            auto ins=decode_sm83(pc,b0,b1,b2);
            if (!ins.legal) { ++result.rejected_paths; break; }
            block.instructions.push_back(ins);
            ++result.decoded_instructions;
            const auto next=static_cast<std::uint16_t>(pc+ins.length);
            const auto op=ins.opcode;
            if (ins.is_cb) {
                if (may_write_mapper(ins)) { add(k.bank,next); break; }
                pc=next;
                continue;
            }
            if (is_jr(op)) {
                const auto target=static_cast<std::uint16_t>(next+static_cast<std::int8_t>(ins.immediate8));
                add(k.bank,target);
                if (op!=0x18) add(k.bank,next);
                break;
            }
            if (is_jp(op)) {
                if (op!=0xE9) add(k.bank,ins.immediate16);
                if (op==0xC2 || op==0xCA || op==0xD2 || op==0xDA) add(k.bank,next);
                break;
            }
            if (is_call(op) || (op&0xC7)==0xC7) {
                if ((op&0xC7)==0xC7) add(0,static_cast<std::uint16_t>(op&0x38));
                else add(k.bank,ins.immediate16);
                add(k.bank,next);
                break;
            }
            if (is_ret(op)) {
                if (op==0xC0 || op==0xC8 || op==0xD0 || op==0xD8) add(k.bank,next);
                break;
            }
            if (op==0x76 || op==0x10) { add(k.bank,next); break; }
            if (may_write_mapper(ins)) { add(k.bank,next); break; }
            pc=next;
            if (count+1==max_ins) add(k.bank,pc);
        }
        if (!block.instructions.empty()) result.blocks.push_back(std::move(block));
    }
    return result;
}
}
