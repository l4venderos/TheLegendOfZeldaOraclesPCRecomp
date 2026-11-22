#include "oracles/sm83.hpp"
#include "oracles/analysis.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <iostream>
int main() {
    using namespace oracles;
    auto a=decode_sm83(0x0100,0x3E,0x42,0);
    assert(a.length==2 && a.legal && disassemble_sm83(a)=="LD A, $42");
    auto b=decode_sm83(0x2000,0xCB,0x7C,0);
    assert(b.is_cb && b.length==2 && disassemble_sm83(b)=="BIT 7, H");
    auto c=decode_sm83(0x3000,0xC3,0x34,0x12);
    assert(c.length==3 && c.immediate16==0x1234 && is_terminator(c));
    auto d=decode_sm83(0x4000,0xD3,0,0);
    assert(!d.legal);

    // A possible MBC5 mapper write must end the current native block. The
    // next dispatch must observe the newly selected ROM bank before fetching
    // the following instruction.
    RomImage fixture;
    fixture.bytes.assign(2*0x4000,0x00);
    fixture.bank_count=2;
    fixture.bytes[0x0100]=0x3E; fixture.bytes[0x0101]=0x01; // LD A,$01
    fixture.bytes[0x0102]=0xEA; fixture.bytes[0x0103]=0x00; fixture.bytes[0x0104]=0x20; // LD ($2000),A
    fixture.bytes[0x0105]=0x00; // NOP after bank switch
    const auto graph=analyze_rom(fixture,64,32);
    const auto entry=std::find_if(graph.blocks.begin(),graph.blocks.end(),[](const NativeBlock& block) {
        return block.start.bank==0 && block.start.address==0x0100;
    });
    assert(entry!=graph.blocks.end());
    assert(entry->instructions.size()==2);
    assert(entry->instructions.back().opcode==0xEA);
    std::cout << "SM83 decoder and bank-switch block-boundary tests passed.\n";
}
