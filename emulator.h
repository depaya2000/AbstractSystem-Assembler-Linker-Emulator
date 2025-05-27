#ifndef EMULATOR_H
#define EMULATOR_H

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include "assembler.h"

// Veličina memorije (4GB za 32-bitni sistem)
constexpr size_t MEMORY_SIZE = 1ull << 32;

// Broj opštenamenskih registara
constexpr size_t NUM_REGISTERS = 16;

// Emulator: Globalni status
struct Emulator {
    std::vector<uint8_t> memory;    // Memorija emulatora
    uint32_t registers[NUM_REGISTERS]; // Registarska datoteka
    uint32_t status;                // Status registar
    uint32_t handler;               // Handler registar
    uint32_t cause;                 // Cause registar
    std::vector<ELF32_Shdr> sectionHeaders; // Zaglavlja sekcija

    // Konstruktor: Inicijalizuje memoriju i registre
    Emulator() : memory(MEMORY_SIZE, 0), status(0), handler(0), cause(0) {
        // Postavljanje registara na početne vrednosti
        std::memset(registers, 0, sizeof(registers)); // Svi registri na 0
        registers[0] = 0;                             // r0 je uvek 0
        //registers[15] = 0x40000000;                   // r15 (PC) na 0x40000000
        registers[14] = 0x7FFFFFFF;                   // r14 (SP) na adresu steka (primer)

    }

    // Funkcije
    void loadMemoryFromBinaryFile(const std::string& filename);
    void printState() const;
    uint32_t fetchInstruction(uint32_t address) const;
    void executeInstruction(const Instruction& instr);
    void execute_halt();
    void execute_int();
    void execute_call(uint8_t regA, uint8_t regB, int16_t disp, uint8_t mod);
    void execute_jump(uint8_t regA, uint8_t regB, uint8_t regC, int16_t disp, uint8_t mod);
    void execute_xchg(uint8_t regB, uint8_t regC);
    void execute_arithmetic(uint8_t regA, uint8_t regB, uint8_t regC, uint8_t mod);
    void execute_logical(uint8_t regA, uint8_t regB, uint8_t regC, uint8_t mod);
    void execute_shift(uint8_t regA, uint8_t regB, uint8_t regC, uint8_t mod);
    void execute_store(uint32_t regA, uint32_t regB, uint32_t regC, uint32_t disp, uint8_t mod);
    void execute_load(uint32_t regA, uint32_t regB, uint32_t regC, uint32_t disp, uint8_t mod);
    void emulate();
};

// Dekodiranje instrukcije
struct Instruction {
    uint8_t OC;
    uint8_t MOD;
    uint8_t RegA;
    uint8_t RegB;
    uint8_t RegC;
    int16_t Disp; // Sign-extended pomeraj

    explicit Instruction(uint32_t word) {
        OC = (word >> 28) & 0xF;
        MOD = (word >> 24) & 0xF;
        RegA = (word >> 20) & 0xF;
        RegB = (word >> 16) & 0xF;
        RegC = (word >> 12) & 0xF;
        Disp = static_cast<int16_t>(word & 0xFFF); // Sign-extend
    }
};

#endif // EMULATOR_H
