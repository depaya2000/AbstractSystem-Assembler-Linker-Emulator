#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <stdexcept>
#include "emulator.h"

bool halt_flag = false;

void Emulator::loadMemoryFromBinaryFile(const std::string& filename) {
    std::vector<SectionContent> sections;   // Sekcije za instrukcije i podatke ulazne
    std::vector<LiteralEntry> literalPool;  // Bazen literala ulazni

    std::ifstream objFile(filename, std::ios::binary);

    if (!objFile.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return;
    }

    // 1. Učitavanje ELF zaglavlja
    ELF32Header elfHeader;
    objFile.read(reinterpret_cast<char*>(&elfHeader), sizeof(ELF32Header));
    if (objFile.gcount() != sizeof(ELF32Header)) {
        std::cerr << "Error: Failed to read ELF header from " << filename << std::endl;
        objFile.close();
        return;
    }

    // 2. Učitavanje tabele zaglavlja sekcija
    objFile.seekg(elfHeader.e_shoff, std::ios::beg); // Offset za tabelu zaglavlja sekcija
    for (size_t i = 0; i < elfHeader.e_shnum; ++i) {
        ELF32_Shdr sectionHeader;
        objFile.read(reinterpret_cast<char*>(&sectionHeader), sizeof(ELF32_Shdr));
        if (objFile.gcount() != sizeof(ELF32_Shdr)) {
            std::cerr << "Error: Failed to read section header " << i << " from " << filename << std::endl;
            break;
        }
        if(sectionHeader.sh_flags & SHF_ALLOC){
            sectionHeaders.push_back(sectionHeader);
        }
    }

    // 3. Učitavanje sadržaja sekcija
    for (const auto& sectionHeader : sectionHeaders) {
        if ((sectionHeader.sh_flags & SHF_ALLOC) && (sectionHeader.sh_type != SHT_LITERALPOOL)) {
            SectionContent section;
            section.headerIndex = &sectionHeader - &sectionHeaders[0];
            objFile.seekg(sectionHeader.sh_offset, std::ios::beg);
            section.content.resize(sectionHeader.sh_size);
            objFile.read(reinterpret_cast<char*>(section.content.data()), sectionHeader.sh_size);
            sections.push_back(section);

            // Smeštanje sadržaja sekcija u memoriju
            if (sectionHeader.sh_addr + sectionHeader.sh_size <= memory.size()) {
                std::copy(section.content.begin(),
                          section.content.end(),
                          memory.begin() + sectionHeader.sh_addr);
            } else {
                std::cerr << "Error: Section exceeds memory bounds." << std::endl;
            }
        } else if (sectionHeader.sh_type == SHT_LITERALPOOL) {
            // 4. Učitavanje bazena literala
            size_t numLiterals = sectionHeader.sh_size / sizeof(LiteralEntry);
            objFile.seekg(sectionHeader.sh_offset, std::ios::beg);

            for (size_t i = 0; i < numLiterals; ++i) {
                LiteralEntry entry;
                objFile.read(reinterpret_cast<char*>(&entry), sizeof(LiteralEntry));
                literalPool.push_back(entry);

                // Smeštanje vrednosti literala u memoriju
                if (sectionHeader.sh_addr + i * sizeof(LiteralEntry) < memory.size()) {
                    uint32_t literalAddr = sectionHeader.sh_addr + i * sizeof(entry.value);
                    std::memcpy(&memory[literalAddr], &entry.value, sizeof(entry.value));
                } else {
                    std::cerr << "Error: Literal exceeds memory bounds." << std::endl;
                }
            }
        }
    }

    objFile.close();

     // Inicijalizacija PC na prvu izvršnu sekciju
    for (const auto& header : sectionHeaders) {
        if (header.sh_flags & SHF_EXECINSTR) {
            registers[15] = header.sh_addr; // PC pokazuje na početak instrukcija
            break;
        }
    }

    // Ako nema izvršnih sekcija, baciti grešku
    if (registers[15] == 0) {
        throw std::runtime_error("No executable section found for initializing PC.");
    }

    std::cout << "Object file loaded successfully into memory." << std::endl;
}

void Emulator::printState() const {
    std::cout << "-----------------------------------------------------------------" << std::endl;
    std::cout << "Emulated processor executed halt instruction" << std::endl;
    std::cout << "Emulated processor state:" << std::endl;

    // Ispis registara u formatiranom obliku
    for (int i = 0; i < NUM_REGISTERS; ++i) {
        std::cout << "r" << i << "=0x" 
                  << std::hex << std::setfill('0') << std::setw(8) << registers[i];
        if ((i + 1) % 4 == 0) { // Prelazak u novi red posle svaka 4 registra
            std::cout << std::endl;
        } else {
            std::cout << " ";
        }
    }

    std::cout << "-----------------------------------------------------------------" << std::endl;
}

uint32_t Emulator::fetchInstruction(uint32_t address) const {
    // Pronalaženje sekcije kojoj pripada adresa
    bool isExecutable = false;

    for (const auto& section : sectionHeaders) {
        if ((section.sh_flags & SHF_EXECINSTR) && 
            address >= section.sh_addr && 
            address < section.sh_addr + section.sh_size) {
            isExecutable = true;
            break;
        }
    }

    // Ako adresa nije deo izvršne sekcije, baca se greška
    if (!isExecutable) {
        throw std::invalid_argument("Address does not belong to an executable section");
    }

    // Provera opsega memorije
    if (address + 3 >= memory.size()) {
        throw std::out_of_range("Out of memory bounds");
    }

    // Provera poravnatosti adrese
    if (address % 4 != 0) {
        throw std::invalid_argument("Address must be aligned to 4 bytes");
    }

    // Sklapanje 32-bitne instrukcije iz 4 bajta memorije
    return memory[address] | 
           (memory[address + 1] << 8) | 
           (memory[address + 2] << 16) | 
           (memory[address + 3] << 24);
}



void Emulator::executeInstruction(const Instruction& instr) {
    switch (instr.OC) {
        case 0x0: // HALT
            execute_halt();
            break;
        case 0x1: // INT
            execute_int();
            break;
        case 0x2: // CALL
            execute_call(instr.RegA, instr.RegB, instr.Disp, instr.MOD);
            break;
        case 0x3: // JMP
            execute_jump(instr.RegA, instr.RegB, instr.RegC, instr.Disp, instr.MOD);
            break;
        case 0x4: // XCHG
            execute_xchg(instr.RegB, instr.RegC);
            break;
        case 0x5: // ADD/SUB/MUL/DIV
            execute_arithmetic(instr.RegA, instr.RegB, instr.RegC, instr.MOD);
            break;
        case 0x6: // Logical operations (NOT/AND/OR/XOR)
            execute_logical(instr.RegA, instr.RegB, instr.RegC, instr.MOD);
            break;
        case 0x7: // SHL/SHR
            execute_shift(instr.RegA, instr.RegB, instr.RegC, instr.MOD);
            break;
        case 0x8: // ST
            execute_store(instr.RegA, instr.RegB, instr.RegC, instr.Disp, instr.MOD);
            break;
        case 0x9: // LD/CSRRD/CSRWR/POP
            execute_load(instr.RegA, instr.RegB, instr.RegC, instr.Disp, instr.MOD);
            break;
        default:
            throw std::invalid_argument("Unknown operation code");
    }
}

void Emulator::execute_halt() {
    halt_flag = true;
}

void Emulator::execute_int() {
    //push(registers[15]); // Save PC
    registers[14] -= 4;
    memory[registers[14]] = registers[15];
    //push(status); // Save status
    registers[14] -= 4;
    memory[registers[14]] = status;
    cause = 4; // Set cause
    status &= ~0x1; // Disable interrupts
    registers[15] = handler; // Jump to handler
}

void Emulator::execute_call(uint8_t regA, uint8_t regB, int16_t disp, uint8_t mod) {
    // Sačuvaj povratnu adresu
    //push(registers[15]) - save PC
    registers[14] -= 4;
    memory[registers[14]] = registers[15];

    uint32_t literalValue = 0; // Vrednost iz literal pool-a
    uint32_t literalPoolBase = 0;

    // Pronalaženje baze literal pool-a
    for (const auto& section : sectionHeaders) {
        if (section.sh_type == SHT_LITERALPOOL) {
            literalPoolBase = section.sh_addr;
            break;
        }
    }

     if (literalPoolBase == 0) {
         throw std::runtime_error("Error: Literal pool section not found.");
    }

    // Izračunavanje adrese u literal pool-u
    uint32_t literalAddress = literalPoolBase + 4 * disp;

    // Provera opsega memorije
    if (literalAddress + 3 >= memory.size()) {
          throw std::out_of_range("Error: Literal pool access out of memory bounds.");
    }

    // Sklapanje stvarne vrednosti iz bazena
    literalValue = memory[literalAddress] |
                      (memory[literalAddress + 1] << 8) |
                      (memory[literalAddress + 2] << 16) |
                      (memory[literalAddress + 3] << 24);
    

    // Izvršenje instrukcije CALL
    if (mod == 0x0) {
        // Direkno sabiranje registara i pomeraja
        registers[15] = registers[regA] + registers[regB] + literalValue;
    } else if (mod == 0x1) {
        // Sabiranje sa literalom iz bazena
        registers[15] = registers[regA] + registers[regB] + literalValue;
    } else {
        throw std::invalid_argument("Invalid MOD for CALL");
    }
}


void Emulator::execute_jump(uint8_t regA, uint8_t regB, uint8_t regC, int16_t disp, uint8_t mod) {

    uint32_t literalValue = 0; // Vrednost iz literal pool-a
    uint32_t literalPoolBase = 0;

    // Pronalaženje baze literal pool-a
    for (const auto& section : sectionHeaders) {
        if (section.sh_type == SHT_LITERALPOOL) {
            literalPoolBase = section.sh_addr;
            break;
        }
    }

     if (literalPoolBase == 0) {
         throw std::runtime_error("Error: Literal pool section not found.");
    }

    // Izračunavanje adrese u literal pool-u
    uint32_t literalAddress = literalPoolBase + 4 * disp;

    // Provera opsega memorije
    if (literalAddress + 3 >= memory.size()) {
          throw std::out_of_range("Error: Literal pool access out of memory bounds.");
    }

    // Sklapanje stvarne vrednosti iz bazena
    literalValue = memory[literalAddress] |
                      (memory[literalAddress + 1] << 8) |
                      (memory[literalAddress + 2] << 16) |
                      (memory[literalAddress + 3] << 24);
    

    switch (mod) {
        case 0x0:
            registers[15] = registers[regA] + literalValue;
            break;
        case 0x1:
            if (registers[regB] == registers[regC]) registers[15] = registers[regA] + literalValue;
            break;
        case 0x2:
            if (registers[regB] != registers[regC]) registers[15] = registers[regA] + literalValue;
            break;
        case 0x3:
            if ((int32_t)registers[regB] > (int32_t)registers[regC]) registers[15] = registers[regA] + literalValue;
            break;
        case 0x8:
            registers[15] = memory[registers[regA] + literalValue];
            break;
        case 0x9:
            if (registers[regB] == registers[regC]) registers[15] = memory[registers[regA] + literalValue];
            break;
        case 0xA:
            if (registers[regB] != registers[regC]) registers[15] = memory[registers[regA] + literalValue];
            break;
        case 0xB:
            if ((int32_t)registers[regB] > (int32_t)registers[regC]) registers[15] = memory[registers[regA] + literalValue];
            break;
        default:
            throw std::invalid_argument("Invalid MOD for JMP");
    }
}

void Emulator::execute_xchg(uint8_t regB, uint8_t regC) {
    uint32_t temp = registers[regB];
    registers[regB] = registers[regC];
    registers[regC] = temp;
}

void Emulator::execute_arithmetic(uint8_t regA, uint8_t regB, uint8_t regC, uint8_t mod) {
    switch (mod) {
        case 0x0: registers[regA] = registers[regB] + registers[regC]; break;
        case 0x1: registers[regA] = registers[regB] - registers[regC]; break;
        case 0x2: registers[regA] = registers[regB] * registers[regC]; break;
        case 0x3: registers[regA] = registers[regB] / registers[regC]; break;
        default:
            throw std::invalid_argument("Invalid MOD for arithmetic operation");
    }
}

void Emulator::execute_logical(uint8_t regA, uint8_t regB, uint8_t regC, uint8_t mod) {
    switch (mod) {
        case 0x0: registers[regA] = ~registers[regB]; break;
        case 0x1: registers[regA] = registers[regB] & registers[regC]; break;
        case 0x2: registers[regA] = registers[regB] | registers[regC]; break;
        case 0x3: registers[regA] = registers[regB] ^ registers[regC]; break;
        default:
            throw std::invalid_argument("Invalid MOD for logical operation");
    }
}

void Emulator::execute_shift(uint8_t regA, uint8_t regB, uint8_t regC, uint8_t mod) {
    switch (mod) {
        case 0x0: registers[regA] = registers[regB] << registers[regC]; break;
        case 0x1: registers[regA] = registers[regB] >> registers[regC]; break;
        default:
            throw std::invalid_argument("Invalid MOD for shift operation");
    }
}

void Emulator::execute_store(uint32_t regA, uint32_t regB, uint32_t regC, uint32_t disp, uint8_t mod) {

    uint32_t literalValue = 0; // Vrednost iz literal pool-a
    uint32_t literalPoolBase = 0;

    if(mod == 0b0000 || mod == 0b0010) {
        // Pronalaženje baze literal pool-a
        for (const auto& section : sectionHeaders) {
            if (section.sh_type == SHT_LITERALPOOL) {
                literalPoolBase = section.sh_addr;
                break;
            }
        }

        if (literalPoolBase == 0) {
            throw std::runtime_error("Error: Literal pool section not found.");
        }

        // Izračunavanje adrese u literal pool-u
        uint32_t literalAddress = literalPoolBase + 4 * disp;

        // Provera opsega memorije
        if (literalAddress + 3 >= memory.size()) {
            throw std::out_of_range("Error: Literal pool access out of memory bounds.");
        }

        // Sklapanje stvarne vrednosti iz bazena
        literalValue = memory[literalAddress] |
                        (memory[literalAddress + 1] << 8) |
                        (memory[literalAddress + 2] << 16) |
                        (memory[literalAddress + 3] << 24);
    }    

    switch (mod) {
        case 0b0000: // mem32[registers[A] + registers[B] + D] <= registers[C];
            memory[registers[regA] + registers[regB] + literalValue] = registers[regC];
            break;

        case 0b0001: // registers[A] += D; mem32[registers[A]] <= registers[C];
            registers[regA] += disp;
            memory[registers[regA]] = registers[regC];
            break;

        case 0b0010: // mem32[mem32[registers[A] + registers[B] + D]] <= registers[C];
            memory[memory[registers[regA] + registers[regB] + literalValue]] = registers[regC];
            break;

        default:
            throw std::runtime_error("Invalid modifier for ST instruction");
    }
}

void Emulator::execute_load(uint32_t regA, uint32_t regB, uint32_t regC, uint32_t disp, uint8_t mod) {

    uint32_t literalValue = 0; // Vrednost iz literal pool-a
    uint32_t literalPoolBase = 0;
    
    if(mod == 0b0001 || mod == 0b0010 || mod == 0b0101 || mod == 0b0110 || mod == 0b0111) {
        // Pronalaženje baze literal pool-a
        for (const auto& section : sectionHeaders) {
            if (section.sh_type == SHT_LITERALPOOL) {
                literalPoolBase = section.sh_addr;
                break;
            }
        }

        if (literalPoolBase == 0) {
            throw std::runtime_error("Error: Literal pool section not found.");
        }

        // Izračunavanje adrese u literal pool-u
        uint32_t literalAddress = literalPoolBase + 4 * disp;

        // Provera opsega memorije
        if (literalAddress + 3 >= memory.size()) {
            throw std::out_of_range("Error: Literal pool access out of memory bounds.");
        }

        // Sklapanje stvarne vrednosti iz bazena
        literalValue = memory[literalAddress] |
                        (memory[literalAddress + 1] << 8) |
                        (memory[literalAddress + 2] << 16) |
                        (memory[literalAddress + 3] << 24);
    }

    switch (mod) {
        case 0b0000: // registers[regA] <= CSR (kontrolni/status registar)
            if (regB == 0) {
                registers[regA] = status;
            } else if (regB == 1) {
                registers[regA] = handler;
            } else if (regB == 2) {
                registers[regA] = cause;
            } else {
                throw std::runtime_error("Invalid CSR index for LD instruction");
            }
            break;

        case 0b0001: // registers[regA] <= registers[regB] + disp;
            registers[regA] = registers[regB] + literalValue;
            break;

        case 0b0010: // registers[regA] <= memory[registers[regB] + registers[regC] + disp];
            registers[regA] = memory[registers[regB] + registers[regC] + literalValue];
            break;

        case 0b0011: // registers[regA] <= memory[registers[regB]]; registers[regB] += disp;
            registers[regA] = memory[registers[regB]];
            registers[regB] += disp;
            break;

        case 0b0100: // CSR (kontrolni/status registar) <= registers[regB]
            if (regA == 0) {
                status = registers[regB];
            } else if (regA == 1) {
                handler = registers[regB];
            } else if (regA == 2) {
                cause = registers[regB];
            } else {
                throw std::runtime_error("Invalid CSR index for LD instruction");
            }
            break;

        case 0b0101: // CSR (kontrolni/status registar) <= CSR | disp
            if (regA == 0) {
                status |= literalValue;
            } else if (regA == 1) {
                handler |= literalValue;
            } else if (regA == 2) {
                cause |= literalValue;
            } else {
                throw std::runtime_error("Invalid CSR index for LD instruction");
            }
            break;

        case 0b0110: // CSR <= memory[registers[regB] + registers[regC] + disp]
            if (regA == 0) {
                status = memory[registers[regB] + registers[regC] + literalValue];
            } else if (regA == 1) {
                handler = memory[registers[regB] + registers[regC] + literalValue];
            } else if (regA == 2) {
                cause = memory[registers[regB] + registers[regC] + literalValue];
            } else {
                throw std::runtime_error("Invalid CSR index for LD instruction");
            }
            break;

        case 0b0111: // CSR <= memory[registers[regB]]; registers[regB] += disp;
            if (regA == 0) {
                status = memory[registers[regB]];
            } else if (regA == 1) {
                handler = memory[registers[regB]];
            } else if (regA == 2) {
                cause = memory[registers[regB]];
            } else {
                throw std::runtime_error("Invalid CSR index for LD instruction");
            }
            registers[regB] += literalValue;
            break;

        default:
            throw std::runtime_error("Invalid modifier for LD instruction");
    }
}



// Emulacija procesora
void Emulator::emulate() {
    try {
        while (!halt_flag) {
            uint32_t pc = registers[15];
            
            // Učitaj instrukciju
            uint32_t word = fetchInstruction(pc);
            Instruction instr(word); // Pretpostavljam da dekodira instrukciju iz word-a

            std::cout << "Executing instruction at 0x" << std::hex << pc << "\n";

            // Izvrši instrukciju
            executeInstruction(instr);

            // Ažuriraj PC
            registers[15] += 4;
        }
    } catch (const std::runtime_error& e) {
        std::cout << "Emulation stopped: " << e.what() << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <hex_file>" << std::endl;
        return 1;
    }

    try {
        Emulator emulator;
        std::cout << argv[1];
        emulator.loadMemoryFromBinaryFile(argv[1]);

        emulator.emulate();

        emulator.printState();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}

