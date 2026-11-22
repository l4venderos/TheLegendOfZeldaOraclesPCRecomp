#include "oracles/sm83.hpp"
#include <iomanip>
#include <sstream>

namespace oracles {
namespace {
unsigned length_of(std::uint8_t op) {
    switch (op) {
        case 0x01: case 0x08: case 0x11: case 0x21: case 0x31:
        case 0xC2: case 0xC3: case 0xC4: case 0xCA: case 0xCC: case 0xCD:
        case 0xD2: case 0xD4: case 0xDA: case 0xDC: case 0xEA: case 0xFA: return 3;
        case 0x06: case 0x0E: case 0x10: case 0x16: case 0x18: case 0x1E:
        case 0x20: case 0x26: case 0x28: case 0x2E: case 0x30: case 0x36:
        case 0x38: case 0x3E: case 0xC6: case 0xCE: case 0xD6: case 0xDE:
        case 0xE0: case 0xE6: case 0xE8: case 0xEE: case 0xF0: case 0xF6:
        case 0xF8: case 0xFE: case 0xCB: return 2;
        default: return 1;
    }
}
bool illegal(std::uint8_t op) {
    switch (op) { case 0xD3: case 0xDB: case 0xDD: case 0xE3: case 0xE4: case 0xEB: case 0xEC: case 0xED: case 0xF4: case 0xFC: case 0xFD: return true; default: return false; }
}
std::string hex8(unsigned v) { std::ostringstream s; s << "$" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (v & 0xFF); return s.str(); }
std::string hex16(unsigned v) { std::ostringstream s; s << "$" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << (v & 0xFFFF); return s.str(); }
const char* r8[] = {"B","C","D","E","H","L","(HL)","A"};
const char* r16[] = {"BC","DE","HL","SP"};
const char* cond[] = {"NZ","Z","NC","C"};
}
DecodedInstruction decode_sm83(std::uint16_t address, std::uint8_t op, std::uint8_t b1, std::uint8_t b2) {
    DecodedInstruction d; d.address=address; d.opcode=op; d.length=static_cast<std::uint8_t>(length_of(op)); d.immediate8=b1; d.immediate16=static_cast<std::uint16_t>(b1 | (std::uint16_t(b2)<<8)); d.legal=!illegal(op);
    if (op == 0xCB) { d.is_cb=true; d.extended_opcode=b1; d.length=2; d.legal=true; }
    return d;
}

std::string disassemble_sm83(const DecodedInstruction& i) {
    if (!i.legal) return "ILLEGAL " + hex8(i.opcode);
    if (i.is_cb) {
        const unsigned x=i.extended_opcode>>6, y=(i.extended_opcode>>3)&7, z=i.extended_opcode&7;
        static const char* rot[] = {"RLC","RRC","RL","RR","SLA","SRA","SWAP","SRL"};
        if (x==0) return std::string(rot[y])+" "+r8[z];
        if (x==1) return "BIT "+std::to_string(y)+", "+r8[z];
        if (x==2) return "RES "+std::to_string(y)+", "+r8[z];
        return "SET "+std::to_string(y)+", "+r8[z];
    }
    const unsigned op=i.opcode, x=op>>6, y=(op>>3)&7, z=op&7, p=y>>1, q=y&1;
    if (x==1) { if(op==0x76) return "HALT"; return std::string("LD ")+r8[y]+", "+r8[z]; }
    if (x==2) { static const char* alu[]={"ADD A","ADC A","SUB","SBC A","AND","XOR","OR","CP"}; return std::string(alu[y])+", "+r8[z]; }
    if (x==0) {
        switch(z) {
            case 0: if(y==0)return "NOP"; if(y==1)return "LD ("+hex16(i.immediate16)+"), SP"; if(y==2)return "STOP"; if(y==3)return "JR "+hex8(i.immediate8); return std::string("JR ")+cond[y-4]+", "+hex8(i.immediate8);
            case 1: return q ? std::string("ADD HL, ")+r16[p] : std::string("LD ")+r16[p]+", "+hex16(i.immediate16);
            case 2: { const char* pair[]={"(BC)","(DE)","(HL+)","(HL-)"}; return q ? std::string("LD A, ")+pair[p] : std::string("LD ")+pair[p]+", A"; }
            case 3: return std::string(q?"DEC ":"INC ")+r16[p];
            case 4: return std::string("INC ")+r8[y];
            case 5: return std::string("DEC ")+r8[y];
            case 6: return std::string("LD ")+r8[y]+", "+hex8(i.immediate8);
            case 7: { const char* m[]={"RLCA","RRCA","RLA","RRA","DAA","CPL","SCF","CCF"}; return m[y]; }
        }
    }
    if (x==3) {
        switch(z) {
            case 0: if(y<4)return std::string("RET ")+cond[y]; if(y==4)return "LDH ($FF00+"+hex8(i.immediate8)+"), A"; if(y==5)return "ADD SP, "+hex8(i.immediate8); if(y==6)return "LDH A, ($FF00+"+hex8(i.immediate8)+")"; return "LD HL, SP+"+hex8(i.immediate8);
            case 1: if(!q)return std::string("POP ")+((p==3)?"AF":r16[p]); if(p==0)return "RET"; if(p==1)return "RETI"; if(p==2)return "JP (HL)"; return "LD SP, HL";
            case 2: if(y<4)return std::string("JP ")+cond[y]+", "+hex16(i.immediate16); if(y==4)return "LD ($FF00+C), A"; if(y==5)return "LD ("+hex16(i.immediate16)+"), A"; if(y==6)return "LD A, ($FF00+C)"; return "LD A, ("+hex16(i.immediate16)+")";
            case 3: if(y==0)return "JP "+hex16(i.immediate16); if(y==1)return "PREFIX CB"; if(y==6)return "DI"; if(y==7)return "EI"; return "ILLEGAL";
            case 4: if(y<4)return std::string("CALL ")+cond[y]+", "+hex16(i.immediate16); return "ILLEGAL";
            case 5: if(!q)return std::string("PUSH ")+((p==3)?"AF":r16[p]); if(p==0)return "CALL "+hex16(i.immediate16); return "ILLEGAL";
            case 6: { const char* a[]={"ADD A","ADC A","SUB","SBC A","AND","XOR","OR","CP"}; return std::string(a[y])+", "+hex8(i.immediate8); }
            case 7: return "RST "+hex16(y*8);
        }
    }
    return "DB "+hex8(op);
}

bool is_terminator(const DecodedInstruction& i) {
    if (i.is_cb) return false;
    const auto op=i.opcode;
    if (op==0x18 || op==0xC3 || op==0xCD || op==0xC9 || op==0xD9 || op==0xE9 || op==0x76 || op==0x10) return true;
    if ((op&0xE7)==0x20 || (op&0xE7)==0xC0 || (op&0xE7)==0xC2 || (op&0xE7)==0xC4) return true;
    if ((op&0xC7)==0xC7) return true;
    return false;
}
}
