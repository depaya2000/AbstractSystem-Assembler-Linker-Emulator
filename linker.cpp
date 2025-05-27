#include <iostream>
#include <iomanip>
#include <fstream>
#include <map>
#include <cstdint>
#include <unordered_map>
#include "linker.h"
#include "assembler.h"

using namespace std;

#define ELF32_R_SYM(info) ((info) >> 8)
#define ELF32_R_INFO(sym, type) (((sym) << 8) | (uint8_t)(type))
#define ELF32_R_TYPE(info) ((uint8_t)(info))


// Globalne promenljive
vector<ELF32_Shdr> sectionHeaders; // Tabela zaglavlja sekcija ulazne
vector<string> stringNameTable;     // Tabela sa imenima sekcija i simbola ulazne
vector<ELF32_Sym> symbolTable;      // Tabela simbola ulazne
vector<SectionContent> sections;    // sekcije za instrukcije i podatke ulazne
vector<SectionRelocation> relocationSections; // Relokacione sekcije ulazne
vector<LiteralEntry> literalPool;       // Bazen literala ulazni

vector<ELF32_Shdr> outputSectionHeaders; // Zajednicka tabela zaglavlja sekcija izlazna
vector<string> outputStringNameTable;     // Zajednicka tabela sa imenima sekcija i simbola izlazna 
vector<ELF32_Sym> outputSymbolTable;      // Zajednicka tabela simbola izlazna
vector<SectionContent> outputSections; // Zajednicka sekcije za instrukcije i podatke izlazna 
vector<SectionRelocation> outputRelocationSections; // Zajednicke relokacione sekcije izlazna
vector<LiteralEntry> outputLiteralPool;       // Zajednicki bazen literala izlazni

//Mapa koja cuva adrese pocetke lokalnih sekcija u globalnoj sekciji
unordered_map<std::string, uint32_t> sectionOffsets;
// Evidencija zauzetih opsega adresa
vector<std::pair<uint32_t, uint32_t>> occupiedRanges;
//Mapa za cuvanje indeksa imena sekcija i simbola
unordered_map<std::string, size_t> nameToIndexMap;
//Mapa koja prati naziv sekcije i indeks u tabeli zaglavlja sekcija outputSectionHeaders
unordered_map<std::string, size_t> sectionMap;
// Mapa za indeksiranje simbola u globalnoj tabeli simbola
unordered_map<std::string, size_t> symbolIndexMap; 
// Mapa za praćenje indeksa relokacionih sekcija u outputRelocationSections
unordered_map<std::string, size_t> relocationIndexMap;
// Globalna mapa koja prati odnos između lokalnih i globalnih indeksa u bazenu literala
std::unordered_map<size_t, size_t> literalPoolIndexMap;

// Funkcija za parsiranje komandne linije
int parseCommandLine(int argc, char* argv[], CommandLineOptions& options) {
    // Proveravamo da li je barem jedan argument prosleđen
    if (argc < 2) {
        cerr << "Usage: linker [options] <input_files>..." << endl;
        return 1;
    }

    // Iteriramo kroz argumente komandne linije
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];

        // Ako je opcija -o, postavljamo naziv izlazne datoteke
        if (arg == "-o") {
            if (i + 1 < argc) {
                options.outputFileName = argv[++i];
            } else {
                cerr << "Error: Missing output file name after -o option" << endl;
                return 1;
            }
        } 
        // Ako je opcija --place, postavljamo adresu sekcije
        else if (arg.substr(0, 8) == "--place=") {
            size_t pos = arg.find('@', 8);
            if (pos != string::npos) {
                string sectionName = arg.substr(8, pos - 8);
                uint32_t address = stoi(arg.substr(pos + 1), nullptr, 16); // Pretvaranje adrese iz heksadecimalnog formata
                options.sectionAddresses[sectionName] = address;
            } else {
                cerr << "Error: Invalid --place option format" << endl;
                return 1;
            }
        } 
        // Ako je opcija -hex, postavljamo da se generiše hex zapis
        else if (arg == "-hex") {
            if (options.generateHex) {
                cerr << "Error: -hex option cannot be used more than once" << endl;
                return 1;
            }
            options.generateHex = true;
        } 
        // Ako je opcija neprepoznata, ispisujemo grešku
        else if (arg[0] == '-') {
            cerr << "Error: Unknown option " << arg << endl;
            return 1;
        } 
        // Sve ostale argumente tretiramo kao ulazne datoteke
        else {
            options.inputFiles.push_back(arg);
        }
    }

    // Proveravamo da li su navedene barem jedne ulazne datoteke
    if (options.inputFiles.empty()) {
        cerr << "Error: At least one input file must be specified" << endl;
        return 1;
    }

    return 0;  // Uspešan završetak
}

void loadObjectFile(const std::string& filename) {
   
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
        objFile.seekg(elfHeader.e_shoff, std::ios::beg); // Offset za ovu tabelu se dobija iz polja `e_shoff` ELF header-a.
        for (size_t i = 0; i < elfHeader.e_shnum; ++i) {
            ELF32_Shdr sectionHeader;
            objFile.read(reinterpret_cast<char*>(&sectionHeader), sizeof(ELF32_Shdr));
            if (objFile.gcount() != sizeof(ELF32_Shdr)) {
                std::cerr << "Error: Failed to read section header " << i << " from " << filename << std::endl;
                break;
            }
            sectionHeaders.push_back(sectionHeader);
        }

        // 3. Učitavanje tabele sa imenima sekcija
        if (elfHeader.e_shstrndx < sectionHeaders.size()) {
            const ELF32_Shdr& strTableHeader = sectionHeaders[elfHeader.e_shstrndx];// Postavljanje read pointer-a na početak string tabele imena sekcija.

            //Proverite da li je velicina validna
            if(strTableHeader.sh_size == 0){
                std::cerr << "Error: String tablesection size is zero.\n";
                return;
            }
            objFile.seekg(strTableHeader.sh_offset, std::ios::beg); // Indeks string table sekcije se dobija iz polja `e_shstrndx` u ELF header-u.
            std::vector<char> strTable(strTableHeader.sh_size);
            objFile.read(strTable.data(), strTableHeader.sh_size);
            if(objFile.gcount() != strTableHeader.sh_size){
                std:: cerr << "Error: Failed to read the entire string table: Expected: "
                << strTableHeader.sh_size << ", Got: " << objFile.gcount() << "\n";
                return;
            }

            //Proverite da li tabela nije prazna
            if(strTable.empty()){
                std::cerr << "Error: String table is empty. \n";
                return;
            }

            //Razdvojite imena sekcija i dodajte u stringNameTable
            size_t start = 0;
            for (size_t i = 0; i < strTable.size(); ++i){
                if(strTable[i] == '\0'){
                    if (i > start) {
                        stringNameTable.emplace_back(&strTable[start], i - start);
                    }
                    start = i + 1;
                }
            }
            
            //Proverite poslednji string ako postoji(ne mora bitit dodatni '\0' na kraju)
            if(start < strTable.size()){
                stringNameTable.emplace_back(&strTable[start]);
            }
        } else{
            std::cerr << "Error: Invalid string table index in ELF header." << elfHeader.e_shstrndx << "\n";
        }

        // 4. Učitavanje sadržaja sekcija
        for (const auto& sectionHeader : sectionHeaders) {
            if (sectionHeader.sh_type == SHT_PROGBITS || sectionHeader.sh_type == SHT_NOBITS) {
                SectionContent section;
                section.headerIndex = &sectionHeader - &sectionHeaders[0];
                objFile.seekg(sectionHeader.sh_offset, std::ios::beg);
                section.content.resize(sectionHeader.sh_size);
                objFile.read(reinterpret_cast<char*>(section.content.data()), sectionHeader.sh_size);
                sections.push_back(section);
            } else if (sectionHeader.sh_type == SHT_SYMTAB) {
                // 5. Učitavanje tabele simbola
                size_t numSymbols = sectionHeader.sh_size / sizeof(ELF32_Sym);
                objFile.seekg(sectionHeader.sh_offset, std::ios::beg);
                for (size_t i = 0; i < numSymbols; ++i) {
                    ELF32_Sym symbol;
                    objFile.read(reinterpret_cast<char*>(&symbol), sizeof(ELF32_Sym));
                    symbolTable.push_back(symbol);
                }
            } else if (sectionHeader.sh_type == SHT_RELA) {
                // 6. Učitavanje relokacionih zapisa
                SectionRelocation relocationSection;
                relocationSection.headerIndex = &sectionHeader - &sectionHeaders[0];
                size_t numRelocations = sectionHeader.sh_size / sizeof(ELF32_Rela);
                objFile.seekg(sectionHeader.sh_offset, std::ios::beg);
                for (size_t i = 0; i < numRelocations; ++i) {
                    ELF32_Rela relocation;
                    objFile.read(reinterpret_cast<char*>(&relocation), sizeof(ELF32_Rela));
                    relocationSection.relocationTable.push_back(relocation);
                }
                relocationSections.push_back(relocationSection);
            } else if (sectionHeader.sh_type == SHT_LITERALPOOL) {
                // 7. Učitavanje bazena literala
                size_t numLiterals = sectionHeader.sh_size / sizeof(LiteralEntry); // Broj unosa baziran na veličini strukture
                objFile.seekg(sectionHeader.sh_offset, std::ios::beg);

                for (size_t i = 0; i < numLiterals; ++i) {
                    LiteralEntry entry;
                    objFile.read(reinterpret_cast<char*>(&entry), sizeof(LiteralEntry));
                    literalPool.push_back(entry);
                }
            }
        }

        objFile.close();
    

    std::cout << "Object files loaded successfully." << std::endl;
}

void mergeStringNameTables() {
    // Prolazak kroz svaku ulaznu tabelu imena
    for (const auto& inputTable : stringNameTable) {
        // Provera da li ime već postoji u izlaznoj tabeli
        if (nameToIndexMap.find(inputTable) == nameToIndexMap.end()) {
            // Ako ne postoji, dodaj ga u izlaznu tabelu i ažuriraj mapu
            size_t newIndex = outputStringNameTable.size();
            nameToIndexMap[inputTable] = newIndex;
            outputStringNameTable.push_back(inputTable);
        }
    }
}

void updateSymbolTable() {
    // Ažurirajte simbole
    for (const auto& symbol : symbolTable) {
        std::string symbolName = stringNameTable[symbol.st_name];
        // Proverava da li je simbol naziv sekcije ili globalni jer samo njih cuvamo u outputSymbolTable, ne cuvamo lokalne simbole
        if (ELF32_ST_TYPE(symbol.st_info) == STT_SECTION) {
            // Pretraga simbola u tabeli simbola koristeći mapu
            auto it = symbolIndexMap.find(symbolName);
            if (it == symbolIndexMap.end()) {
                // Ako simbol ne postoji, dodajte ga u tabelu
                ELF32_Sym newSymbol = symbol;

                newSymbol.st_name = nameToIndexMap[symbolName];

                if (symbol.st_shndx != SHN_UNDEF) {
                    // Proveravamo da li sekcija postoji u ulaznim sekcijama
                    if (symbol.st_shndx >= sectionHeaders.size()) {
                        throw std::runtime_error("Error: Invalid section index for symbol: " + symbolName);
                    }

                    // Prevodimo indeks sekcije u naziv sekcije
                    const std::string& sectionName = stringNameTable[sectionHeaders[symbol.st_shndx].sh_name];
                    
                    // Dohvatamo globalni indeks sekcije
                    auto sectionIt = sectionMap.find(sectionName);
                    if (sectionIt == sectionMap.end()) {
                        throw std::runtime_error("Error: Section '" + sectionName + "' not found in section map.");
                    }
                    uint16_t globalIndex = sectionIt->second;

                    // Koristi sectionOffsets za lokalni offset
                    uint32_t localSectionOffset = sectionOffsets[sectionName];

                    // Ažuriranje vrednosti simbola na osnovu globalnog ofseta
                    newSymbol.st_value += localSectionOffset;
                    newSymbol.st_shndx = globalIndex;
                }

                // Dodajemo simbol u izlaznu tabelu simbola
                outputSymbolTable.push_back(newSymbol);

                // Dodajemo novi simbol u mapu sa njegovim indeksom
                symbolIndexMap[symbolName] = outputSymbolTable.size() - 1;
            } else{
                uint16_t globalSymbolIndex = it->second;

                // Prevodimo indeks sekcije u naziv sekcije
                const std::string& sectionName = stringNameTable[sectionHeaders[symbol.st_shndx].sh_name];

                // Dohvatamo globalni indeks sekcije
                auto sectionIt = sectionMap.find(sectionName);
                if (sectionIt == sectionMap.end()) {
                    throw std::runtime_error("Error: Section '" + sectionName + "' not found in section map.");
                }
                uint16_t globalIndex = sectionIt->second;

                outputSymbolTable[globalSymbolIndex].st_value = outputSectionHeaders[globalIndex].sh_addr;

            }
        } else if (ELF32_ST_BIND(symbol.st_info) == STB_GLOBAL) {
            //bool symbolExists = false;

            // Pretraga simbola u tabeli simbola koristeći mapu
            auto it = symbolIndexMap.find(symbolName);
            if (it != symbolIndexMap.end()) {
                // Ako simbol već postoji u mapiranju
                size_t symbolIndex = it->second;
                ELF32_Sym& globalSymbol = outputSymbolTable[symbolIndex];

                // Provera da li je sekcija simbolu dodeljena
                if (globalSymbol.st_shndx == SHN_UNDEF && symbol.st_shndx != SHN_UNDEF) {
                    // Prevodimo indeks sekcije u naziv sekcije
                    const std::string& sectionName = stringNameTable[sectionHeaders[symbol.st_shndx].sh_name];
                    uint16_t globalIndex = sectionMap[sectionName];
                    //uint32_t globalSectionBaseAddress = outputSectionHeaders[globalIndex].sh_addr;
                    
                    // Koristi sectionOffsets za lokalni offset
                    uint32_t localSectionOffset = sectionOffsets[sectionName];
                    
                    globalSymbol.st_value = symbol.st_value + localSectionOffset;
                    globalSymbol.st_shndx = globalIndex;
                } else if (globalSymbol.st_shndx != SHN_UNDEF && symbol.st_shndx != SHN_UNDEF) {
                    throw std::runtime_error("Error: Multiple definitions of global symbol '" + symbolName + "'");
                }
            } else {
                // Ako simbol ne postoji, dodajte ga u tabelu
                ELF32_Sym newSymbol = symbol;

                newSymbol.st_name = nameToIndexMap[symbolName];

                if (symbol.st_shndx != SHN_UNDEF) {
                    // Prevodimo indeks sekcije u naziv sekcije
                    const std::string& sectionName = stringNameTable[sectionHeaders[symbol.st_shndx].sh_name];
                    uint16_t globalIndex = sectionMap[sectionName];
                    //uint32_t globalSectionBaseAddress = outputSectionHeaders[globalIndex].sh_addr;

                    // Koristi sectionOffsets za lokalni offset
                    uint32_t localSectionOffset = sectionOffsets[sectionName];

                    newSymbol.st_value += localSectionOffset;
                    newSymbol.st_shndx = globalIndex;
                }
                outputSymbolTable.push_back(newSymbol);

                // Dodajemo novi simbol u mapu sa njegovim indeksom
                symbolIndexMap[symbolName] = outputSymbolTable.size() - 1;
            }
        } 
    }
}

void updateSectionLinks(size_t startIndex) {
    for (size_t i = startIndex; i < outputSectionHeaders.size(); ++i) {
        ELF32_Shdr& header = outputSectionHeaders[i];

        if (header.sh_type == SHT_RELA) {
            // Dobijanje imena ciljne sekcije na koju relokaciona sekcija ukazuje
            std::string targetSectionName = stringNameTable[sectionHeaders[header.sh_link].sh_name];

            // Pronađi indeks ciljne sekcije
            auto targetIt = sectionMap.find(targetSectionName);
            if (targetIt != sectionMap.end()) {
                header.sh_link = targetIt->second; // Ažuriraj sh_link
            } else {
                throw std::runtime_error("Error: Target section '" + targetSectionName +
                                         "' for relocation section not found.");
            }
        }
    }
}

// Varijabla koja prati trenutnu adresu sekcije kada se ne koristi opcija --place
uint32_t currentAddress = 0; // globalna promenljiva

void mergeSectionHeaders(const CommandLineOptions& options) {
    // Lambda funkcija za proveru preklapanja
    auto checkOverlap = [](uint32_t start, uint32_t end, const std::vector<std::pair<uint32_t, uint32_t>>& ranges) {
        for (const auto& range : ranges) {
            if (!(end < range.first || start > range.second)) {
                return true; // Postoji preklapanje
            }
        }
        return false;
    };

    size_t startIndex = outputSectionHeaders.size(); //Pocetni indeks novih sekcija

    // Obrada svih ulaznih zaglavlja sekcija
    for (size_t i = 0; i < sectionHeaders.size(); ++i) {
        const ELF32_Shdr& inputHeader = sectionHeaders[i];
        const std::string& sectionName = stringNameTable[inputHeader.sh_name]; // Dobijanje imena sekcije

        // Pronađi indeks imena u outputStringNameTable
        size_t nameIndex;
        auto nameIt = nameToIndexMap.find(sectionName);
        if (nameIt != nameToIndexMap.end()) {
            nameIndex = nameIt->second;
        } else {
            throw std::runtime_error("Error: Section name '" + sectionName + "' not found in outputStringNameTable.");
        }

        auto it = sectionMap.find(sectionName);

        if (it != sectionMap.end()) {
            // Sekcija već postoji, samo povećaj veličinu
            size_t outputIndex = it->second;

            // Proveri da li sekcija ima SHF_ALLOC
            if ((outputSectionHeaders[outputIndex].sh_flags & SHF_ALLOC) && 
                (outputSectionHeaders[outputIndex].sh_type != SHT_LITERALPOOL)) {
                // Postavi offset za trenutnu lokalnu sekciju
                sectionOffsets[sectionName] = outputSectionHeaders[outputIndex].sh_addr + outputSectionHeaders[outputIndex].sh_size;

                // Proveri preklapanje i pomeranje sekcija ako je potrebno
                bool needsRealocation = false;
                for (size_t j = outputIndex + 1; j < outputSectionHeaders.size(); ++j) {
                    uint32_t nextStart = outputSectionHeaders[j].sh_addr;
                    uint32_t nextEnd = nextStart + outputSectionHeaders[j].sh_size;
                    if (checkOverlap(outputSectionHeaders[outputIndex].sh_addr,
                                     outputSectionHeaders[outputIndex].sh_addr + outputSectionHeaders[outputIndex].sh_size + inputHeader.sh_size,
                                     {{nextStart, nextEnd}})) {
                        needsRealocation = true;
                        break;
                    }
                }

                if (needsRealocation) {
                    // Ako nema dovoljno prostora, pomeri sledeće sekcije
                    for (size_t j = outputIndex + 1; j < outputSectionHeaders.size(); ++j) {
                        // Ažuriraj 'sectionOffsets' za sve pomerene sekcije sa SHF_ALLOC
                        if ((outputSectionHeaders[j].sh_flags & SHF_ALLOC) && (outputSectionHeaders[outputIndex].sh_type != SHT_LITERALPOOL)) {
                            outputSectionHeaders[j].sh_addr += inputHeader.sh_size;
                            const std::string& movedSectionName = outputStringNameTable[outputSectionHeaders[j].sh_name];
                            uint32_t localSectionOffset = sectionOffsets[movedSectionName];
                            sectionOffsets[movedSectionName] = localSectionOffset + inputHeader.sh_size;
                        }
                    }
                }
              //Povecaj currentAddress
              currentAddress += inputHeader.sh_size;
                
            }

            // Povećaj veličinu globalne sekcije
            outputSectionHeaders[outputIndex].sh_size += inputHeader.sh_size;

            // Dodaj novi opseg zauzetih adresa u 'occupiedRanges'
            if ((outputSectionHeaders[outputIndex].sh_flags & SHF_ALLOC) && (outputSectionHeaders[outputIndex].sh_type != SHT_LITERALPOOL)) {
                uint32_t newStart = outputSectionHeaders[outputIndex].sh_addr;
                uint32_t newEnd = newStart + outputSectionHeaders[outputIndex].sh_size;
                occupiedRanges.push_back({newStart, newEnd});
            }

        } else {
            // Dodaj novu sekciju u outputSectionHeaders
            ELF32_Shdr newHeader = inputHeader;

            // Ažuriranje polja sh_name za novo zaglavlje
            newHeader.sh_name = nameIndex;

            if ((newHeader.sh_flags & SHF_ALLOC) && (newHeader.sh_type != SHT_LITERALPOOL)) {
                // Proveri da li postoji specifična adresa u komandnoj liniji
                auto optIt = options.sectionAddresses.find(sectionName);
                if (optIt != options.sectionAddresses.end()) {
                    uint32_t explicitAddress = optIt->second;
                    uint32_t sectionEnd = explicitAddress + newHeader.sh_size - 1;

                    // Proveri preklapanje
                    if (checkOverlap(explicitAddress, sectionEnd, occupiedRanges)) {
                        throw std::runtime_error("Error: Section overlap detected for section '" + sectionName +
                                                 "' at range " + std::to_string(explicitAddress) + " - " + std::to_string(sectionEnd));
                    }

                    // Dodaj opseg u zauzete opsege
                    occupiedRanges.emplace_back(explicitAddress, sectionEnd);
                    newHeader.sh_addr = explicitAddress;

                    // Dodaj sekciju u mapu sectionOffsets
                    sectionOffsets[sectionName] = explicitAddress;

                } else {
                    // Koristi podrazumevanu adresu
                    newHeader.sh_addr = currentAddress;

                    // Dodaj sekciju u mapu sectionOffsets
                    sectionOffsets[sectionName] = currentAddress;

                    currentAddress += newHeader.sh_size;
                }
            } else {
                // Sekcija nema SHF_ALLOC, sh_addr ostaje 0
                newHeader.sh_addr = 0;
            }

            // Dodaj zaglavlje i mapiraj ime sekcije
            sectionMap[sectionName] = outputSectionHeaders.size();
            outputSectionHeaders.push_back(newHeader);
        }
    }

    // Ažuriranje `sh_link` nakon obrade svih sekcija
    updateSectionLinks(startIndex);

    // Prolaz kroz sva zaglavlja i ažuriranje `sh_addr` za sekcije sa `sh_type == SHT_LITERALPOOL`
    for (auto& header : outputSectionHeaders) {
        if (header.sh_type == SHT_LITERALPOOL) {
            header.sh_addr = currentAddress;
        }
    }
}


void mergeSections() {
    // Iteriraj kroz ulazne sekcije i dodaj ih u odgovarajuće izlazne sekcije
    for (auto& section : sections) { // Koristimo `auto&` jer ćemo ažurirati `section.headerIndex`
        const std::string& sectionName = stringNameTable[sectionHeaders[section.headerIndex].sh_name];

        // Pronalaženje izlazne sekcije na osnovu mape
        auto it = sectionMap.find(sectionName);
        if (it != sectionMap.end()) {
            uint16_t globalIndex = it->second; //Globalni indeks izlazne sekcije
            bool found = false;

            for(auto& outputSection: outputSections){
                if(outputSection.headerIndex == globalIndex){
                    //Ako postoji dodaj sadrzaj ulazne sekcije
                    outputSection.content.insert(outputSection.content.end(),
                                                section.content.begin(),
                                                section.content.end());
                    found = true;
                    break;
                }
            }

            if(!found){
                SectionContent newSection = section; //Kopiraj sadrzaj ulazne sekcije
                newSection.headerIndex = globalIndex; // Azuriraj headerIndex na globalni
                outputSections.push_back(newSection);
            }
            
        } else {
            throw std::runtime_error("Error: Section '" + sectionName + "' not found in output map.");
        }
    }
}

void mergeAndMapRelocations() {
    // Prolaz kroz sve ulazne relokacione sekcije
    for (auto& relSec : relocationSections) {
        // Dohvatanje naziva sekcije kojoj pripada trenutna relokaciona sekcija
        const std::string& sectionName = stringNameTable[sectionHeaders[relSec.headerIndex].sh_name];

        // Pronalaženje odgovarajuće izlazne sekcije
        auto sectionIt = sectionMap.find(sectionName);
        if (sectionIt == sectionMap.end()) {
            throw std::runtime_error("Error: Section for relocation '" + sectionName + "' not found in section map.");
        }
        size_t outputSectionIndex = sectionIt->second;


        // Prolaz kroz sve relokacione zapise u ulaznoj sekciji
        for (auto& rela : relSec.relocationTable) {
            size_t targetSectionIndex = sectionHeaders[relSec.headerIndex].sh_link;
            const std::string& targetSectionName = stringNameTable[sectionHeaders[targetSectionIndex].sh_name];

            auto it = sectionMap.find(targetSectionName);
            if (it == sectionMap.end()) {
                throw std::runtime_error("Error: Section '" + targetSectionName + "' not found in section index map.");
            }
            size_t outputTargetSectionIndex = it->second;
            rela.r_offset += (outputSectionHeaders[outputTargetSectionIndex].sh_size - sectionHeaders[targetSectionIndex].sh_size);

            if (ELF32_R_TYPE(rela.r_info) == R_ABSWORD32_LOCAL){
                uint32_t index = ELF32_R_SYM(rela.r_info);
                //U pitanju je lokalni simbol i cuvano je u r_info indeks sekcije, a u r_addend st_value tog lokalnog simbola
                const std::string& sectionName = stringNameTable[sectionHeaders[index].sh_name];
                auto sectionIt = sectionMap.find(sectionName);
                if (sectionIt == sectionMap.end()) {
                    throw std::runtime_error("Error: Section '" + sectionName + "' not found in section index map.");
                }
                size_t sectionIndex = sectionIt->second;
                rela.r_info = ELF32_R_INFO(sectionIndex, ELF32_R_TYPE(rela.r_info));
                rela.r_addend += sectionOffsets[sectionName];
            } else if (ELF32_R_TYPE(rela.r_info) == R_ABSWORD32_GLOBAL){
                uint32_t index = ELF32_R_SYM(rela.r_info);
                 // Ažuriranje r_info
                const std::string& symbolName = stringNameTable[symbolTable[index].st_name];
                auto symbolIt = symbolIndexMap.find(symbolName);
                if (symbolIt == symbolIndexMap.end()) {
                    throw std::runtime_error("Error: Symbol '" + symbolName + "' not found in symbol index map.");
                }
                size_t symbolIndex = symbolIt->second;
                rela.r_info = ELF32_R_INFO(symbolIndex, ELF32_R_TYPE(rela.r_info));
            } else if(ELF32_R_TYPE(rela.r_info) == R_ABSINST12_GLOBAL || ELF32_R_TYPE(rela.r_info) == R_ABSINST12_LOCAL || ELF32_R_TYPE(rela.r_info) == R_ABSINST12){
                uint32_t literalIndex = ELF32_R_SYM(rela.r_info);
                auto literalIt = literalPoolIndexMap.find(literalIndex);
                if (literalIt == literalPoolIndexMap.end()) {
                    throw std::runtime_error("Error: Literal with index not found in literal pool index map.");
                }
                size_t outputLiteralIndex = literalIt->second;
                rela.r_info = ELF32_R_INFO(outputLiteralIndex, ELF32_R_TYPE(rela.r_info));

            }
        }

        // Ažuriranje headerIndex za relokacionu sekciju
        relSec.headerIndex = outputSectionIndex;

        // Dohvatanje ili kreiranje relokacione sekcije u outputRelocationSections
        auto relSecIt = relocationIndexMap.find(sectionName);

        if (relSecIt == relocationIndexMap.end()) {
            // Kreiraj novu relokacionu sekciju
            SectionRelocation newRelSec = relSec;
            outputRelocationSections.push_back(newRelSec);
            size_t outputRelocationIndex = outputRelocationSections.size() - 1;
            relocationIndexMap[sectionName] = outputRelocationIndex;
        } else {
            // Sekcija već postoji, dodaj relokacione zapise
            size_t existingRelSecIndex = relSecIt->second;
            SectionRelocation& existingRelSec = outputRelocationSections[existingRelSecIndex];
            existingRelSec.relocationTable.insert(
                existingRelSec.relocationTable.end(),
                relSec.relocationTable.begin(),
                relSec.relocationTable.end()
            );
        }
    }
}

void mergeLiteralPools() {
    // Prolazak kroz sve literalne unose u ulaznom literalPool
    for (size_t localIndex = 0; localIndex < literalPool.size(); ++localIndex) {
        const auto& literalEntry = literalPool[localIndex];

        if (literalEntry.type == LITERAL_VALUE) {
            // Ako je literal, direktno ga dodajemo u izlazni bazen
            outputLiteralPool.push_back(literalEntry);

            // Mapiramo lokalni indeks na indeks u izlaznom bazenu literala
            literalPoolIndexMap[localIndex] = outputLiteralPool.size() - 1;
        } else if(literalEntry.type == GLOBAL_SYMBOL) {
            // Ako je simbol globalni, dohvatamo ime simbola koristeći ulaznu symbolTable
            const ELF32_Sym& inputSymbol = symbolTable[literalEntry.index];
            const std::string& symbolName = stringNameTable[inputSymbol.st_name];

            // Pronađi indeks simbola u globalnoj tabeli simbola koristeći symbolIndexMap
            auto symbolIt = symbolIndexMap.find(symbolName);
            if (symbolIt == symbolIndexMap.end()) {
                throw std::runtime_error("Error: Symbol '" + symbolName + "' not found in symbol index map.");
            }

            size_t globalSymbolIndex = symbolIt->second;

            // Dohvati simbol iz globalne izlazne tabele simbola
            const ELF32_Sym& globalSymbol = outputSymbolTable[globalSymbolIndex];

            // Ažuriraj vrednost simbola na osnovu globalnog ofseta
            uint32_t updatedValue = globalSymbol.st_value;

            // Kreiraj novi unos u izlaznom bazenu literala
            LiteralEntry newEntry = {
                .type = literalEntry.type,
                .value = updatedValue,
                .index = static_cast<int32_t>(globalSymbolIndex)
            };
        
            // Dodaj ažuriranu vrednost u izlazni bazen literala
            outputLiteralPool.push_back(newEntry);

            // Mapiramo lokalni indeks na indeks u izlaznom bazenu literala
            literalPoolIndexMap[localIndex] = outputLiteralPool.size() - 1;
        } else if (literalEntry.type == LOCAL_SYMBOL){
            const std::string& sectionName = stringNameTable[sectionHeaders[literalEntry.index].sh_name];

            // Ažuriraj vrednost simbola na osnovu ofseta za tu sekciju
            uint32_t updatedValue = literalEntry.value + sectionOffsets[sectionName];

            //Azuriraj indeks sekcije na osnovu mape sekcija
            uint32_t globalSectionIndex = sectionMap[sectionName];

            // Kreiraj novi unos u izlaznom bazenu literala
            LiteralEntry newEntry = {
                .type = literalEntry.type,
                .value = updatedValue,
                .index = static_cast<int32_t>(globalSectionIndex) //Za lokalne simbole u bazenu literala index je index sekcije u tabeli zaglavlja
            };
        
            // Dodaj ažuriranu vrednost u izlazni bazen literala
            outputLiteralPool.push_back(newEntry);

            // Mapiramo lokalni indeks na indeks u izlaznom bazenu literala
            literalPoolIndexMap[localIndex] = outputLiteralPool.size() - 1;
        }
    }
}


void resolveRelocations() {
    // Iteriraj kroz sve relokacione sekcije
    for (const auto& relSec : outputRelocationSections) {
        // Indeks sekcije na koju se relokacija odnosi (iz sh_link)
        const uint32_t targetHeaderIndex = outputSectionHeaders[relSec.headerIndex].sh_link;
        //const ELF32_Shdr& targetSectionHeader = outputSectionHeaders[targetHeaderIndex];

        // Iteriraj kroz sve relokacione zapise u sekciji
        for (const auto& rela : relSec.relocationTable) {
            // Izvuci indeks simbola i tip relokacije
            size_t index = ELF32_R_SYM(rela.r_info);
            uint32_t relocationType = ELF32_R_TYPE(rela.r_info);

            // Lokacija gde se relokacija primenjuje relativno na sekciju
            uint32_t relocationOffset = rela.r_offset;

            // Pronađi odgovarajuću sekciju u `outputSections` po `targetHeaderIndex`
            SectionContent* targetSection = nullptr;
            for (auto& section : outputSections) {
                if (section.headerIndex == targetHeaderIndex) {
                    targetSection = &section;
                    break;
                }
            }

            // Ako sekcija nije pronađena, prijavi grešku
            if (targetSection == nullptr) {
                std::cerr << "Error: Target section not found for headerIndex " << targetHeaderIndex << "." << std::endl;
                continue;
            }

            // Proveri da li je offset validan u sadržaju sekcije
            if (relocationOffset + sizeof(uint32_t) > targetSection->content.size()) {
                std::cerr << "Error: Relocation offset out of bounds in section content." << std::endl;
                continue;
            }

            // Pristupi lokaciji u sadržaju sekcije
            uint32_t* relocationTarget = reinterpret_cast<uint32_t*>(
                &targetSection->content[relocationOffset]);

            // Izračunaj novu vrednost na osnovu tipa relokacije
            uint32_t updatedValue = 0;
            if (relocationType == R_ABSWORD32_GLOBAL) {
                // Dohvati simbol iz tabele simbola
                const ELF32_Sym& symbol = outputSymbolTable[index];

                // Izračunaj osnovnu vrednost simbola
                uint32_t symbolValue = symbol.st_value;

                // Izmena svih 32 bita
                updatedValue = symbolValue + rela.r_addend;
                *relocationTarget = updatedValue;
            } else if(relocationType == R_ABSWORD32_LOCAL){
                //Dohvati naziv sekcije jer index kod lokalnog oznacava indeks sekcije
                const std::string& sectionName = stringNameTable[sectionHeaders[index].sh_name];
                //Dohvati adresu te lokalne sekcije u globalnoj sekciji
                uint32_t localSectionOffset = sectionOffsets[sectionName];
                //Izmena svih 32 bita
                uint32_t updatedValue = localSectionOffset + rela.r_addend; // u r_addend kod lokalnih simbola se nalazi offset simbola unutar sekcije
                *relocationTarget = updatedValue;

            } else if (relocationType == R_ABSINST12_GLOBAL || relocationType == R_ABSINST12_LOCAL || relocationType == R_ABSINST12) {
                //Pronadji globalni indeks simbola bilo da je lokalni ili globalni simbol ili literalna vrednsot samo indeks u bazenu literala smestamo
                updatedValue = (index) & 0xFFF; // Zadrži samo donjih 12 bita

                // Sačuvaj ostatak originalne vrednosti osim 12 bitova koje menjamo
                *relocationTarget &= ~0xFFF; // Očisti donjih 12 bita
                *relocationTarget |= updatedValue; // Postavi novih 12 bita
            } else {
                std::cerr << "Error: Unsupported relocation type " << relocationType << "." << std::endl;
            }
        }
    }
}

void generateHexOutput(const std::string& filename) {
    // Otvori fajl za upis
    std::ofstream hexFile(filename);
    if (!hexFile.is_open()) {
        throw std::runtime_error("Error: Unable to open file for HEX output.");
    }

    uint32_t literalPoolAddress = 0; // Početna adresa bazena literala

    // Iteriraj kroz zaglavlja sekcija da pronađeš bazen literala
    for (const auto& header : outputSectionHeaders) {
        if (header.sh_type == SHT_LITERALPOOL) {
            literalPoolAddress = header.sh_addr;
            break; // Pronašli smo bazen literala, izlazimo iz petlje
        }
    }

    if (literalPoolAddress == 0) {
        throw std::runtime_error("Error: Literal pool section not found.");
    }

    // Iteriraj kroz sve sekcije
    for (const auto& section : outputSections) {
        const ELF32_Shdr& header = outputSectionHeaders[section.headerIndex];

        // Preskoči sekcije koje nemaju sadržaj (npr. SHT_NULL ili prazne sekcije)
        if (header.sh_size == 0 || section.content.empty()) {
            continue;
        }

        // Dohvati osnovnu adresu sekcije
        uint32_t baseAddress = header.sh_addr;

        // Iteriraj kroz podatke sekcije u koracima od 8 bajtova
        size_t dataSize = section.content.size();
        for (size_t offset = 0; offset < dataSize; offset += 8) {
            // Ispis adrese
            hexFile << std::setw(4) << std::setfill('0') << std::hex << (baseAddress + offset) << ": ";

            // Ispis sadržaja, do 8 bajtova po liniji
            for (size_t i = 0; i < 8 && (offset + i) < dataSize; ++i) {
                hexFile << std::setw(2) << std::setfill('0') << std::hex
                        << static_cast<int>(section.content[offset + i]) << " ";
            }

            // Prelazak u novi red
            hexFile << "\n";
        }
    }

    // Početna adresa za literal pool
    uint32_t baseAddress = literalPoolAddress;

    // Iteriraj kroz literal pool i ispiši podatke u koracima od 8 bajtova (dve vrednosti po liniji)
    size_t literalCount = outputLiteralPool.size();
    for (size_t offset = 0; offset < literalCount; offset += 2) {
        // Ispis adrese
        hexFile << std::setw(4) << std::setfill('0') << std::hex << (baseAddress + offset * 4) << ": ";

        // Ispis prve vrednosti (ako postoji)
        if (offset < literalCount) {
            const LiteralEntry& literal = outputLiteralPool[offset];
            for (int i = 3; i >= 0; --i) { // Ispis bajtova od najvišeg do najnižeg
                hexFile << std::setw(2) << std::setfill('0') << std::hex
                        << ((literal.value >> (i * 8)) & 0xFF) << " ";
            }
        }

        // Ispis druge vrednosti (ako postoji)
        if (offset + 1 < literalCount) {
            const LiteralEntry& literal = outputLiteralPool[offset + 1];
            for (int i = 3; i >= 0; --i) {
                hexFile << std::setw(2) << std::setfill('0') << std::hex
                        << ((literal.value >> (i * 8)) & 0xFF) << " ";
            }
        }

        // Prelazak u novi red
        hexFile << "\n";
    }

    // Zatvori fajl
    hexFile.close();
}

void generateBinaryOutput(const std::string& filename) {
     std::ofstream objFile(filename, std::ios::binary);
    
    if (!objFile.is_open()) {
        std::cerr << "Error: Could not open file for writing." << std::endl;
        return;
    }

    // Trenutni offset u fajlu - počinje odmah posle ELF header-a
    uint32_t currentOffset = sizeof(ELF32Header);

    // 1. Upis ELF zaglavlja
    ELF32Header elfHeader;
    memset(&elfHeader, 0, sizeof(elfHeader));
    
    // Postavi veličine i indekse za ELF zaglavlje
    elfHeader.e_ident[0] = 0x7f;  // ELF magic number
    elfHeader.e_ident[1] = 'E';
    elfHeader.e_ident[2] = 'L';
    elfHeader.e_ident[3] = 'F';
    elfHeader.e_ident[4] = 1; // ELF klasa (32-bit)
    elfHeader.e_ident[5] = 1; // Endianost (little-endian)
    elfHeader.e_ident[6] = 1; // Verzija ELF
    elfHeader.e_ident[7] = 0; // Rezervisano
    elfHeader.e_type = 1;         // Relocatable file
    elfHeader.e_machine = 3;      // Intel 80386
    elfHeader.e_version = 1;      // ELF version 1
    elfHeader.e_entry = 0; // Početna adresa izvršavanja
    elfHeader.e_ehsize = sizeof(elfHeader);
    elfHeader.e_shoff = currentOffset; // Ovo će biti ažurirano kasnije
    elfHeader.e_shentsize = sizeof(ELF32_Shdr);
    elfHeader.e_shnum = outputSectionHeaders.size();  // Broj sekcija
    elfHeader.e_shstrndx = 0;      // Stavicemo na nulti indeks stringName sekciju pa ce ujedno i odgovarati da je nula

    objFile.write(reinterpret_cast<const char*>(&elfHeader), sizeof(ELF32Header));

    // 2. Ažuriranje offseta sekcija i upis sadržaja sekcija
    for (SectionContent& section : outputSections) {
        // Postavi offset sekcije u odgovarajuće zaglavlje
        outputSectionHeaders[section.headerIndex].sh_offset = currentOffset;
        outputSectionHeaders[section.headerIndex].sh_size = section.content.size(); // Ažuriraj veličinu sekcije

        // Upis sadržaja sekcije
        objFile.write(reinterpret_cast<const char*>(section.content.data()), section.content.size());

        // Ažuriraj trenutni offset
        currentOffset += section.content.size();
    }

   // 3. Upis bazena literala (Literal Pool)
    if (!outputLiteralPool.empty()) {
        // Pronalaženje sekcije za bazen literala
        for (ELF32_Shdr& sh : outputSectionHeaders) {
            if (sh.sh_type == SHT_LITERALPOOL) {
                sh.sh_offset = currentOffset;
                sh.sh_size = outputLiteralPool.size() * sizeof(LiteralEntry);
                break;
            }
        }

        // Upis literala (LiteralEntry struktura)
        for (const LiteralEntry& entry : outputLiteralPool) {
            objFile.write(reinterpret_cast<const char*>(&entry), sizeof(LiteralEntry));
        }

        // Ažuriraj trenutni offset
        currentOffset += outputLiteralPool.size() * sizeof(LiteralEntry);
    }


    // 4. Upis zaglavlja sekcija (Section Headers)
    // Ažuriramo ELF header pre nego što pišemo zaglavlja sekcija
    elfHeader.e_shoff = currentOffset;
    objFile.seekp(0, std::ios::beg);
    objFile.write(reinterpret_cast<const char*>(&elfHeader), sizeof(ELF32Header));

    // Sada upisujemo zaglavlja sekcija
    objFile.seekp(currentOffset, std::ios::beg);
    for (const ELF32_Shdr& sectionHeader : outputSectionHeaders) {
        objFile.write(reinterpret_cast<const char*>(&sectionHeader), sizeof(ELF32_Shdr));
    }

    // Zatvaranje fajla
    objFile.close();

    std::cout << "Object file generated successfully: " << filename << std::endl;
}

//void generateRelocatableOutput(const std::string& filename) {
    // Implementirajte generisanje relocabilnog izlaza
//}

int main(int argc, char* argv[]) {
    CommandLineOptions options;
    int result = parseCommandLine(argc, argv, options);

    if (result != 0) {
        return result; // Završavamo ako je parsiranje komandne linije neuspešno
    }

    // Učitavanje i obrada svakog objektnog fajla
    for (const auto& filename : options.inputFiles) {
        // Očistite prethodne ulazne podatke
        sectionHeaders.clear();
        stringNameTable.clear();
        symbolTable.clear();
        sections.clear();
        relocationSections.clear();
        literalPool.clear();
        literalPoolIndexMap.clear();

        loadObjectFile(filename); // Učitaj objektni fajl
        mergeStringNameTables();
        mergeSectionHeaders(options);
        updateSymbolTable();
        mergeSections();
        mergeLiteralPools();
        mergeAndMapRelocations();
        
    }

    // Nakon učitavanja i kombinovanja sekcija, rešavajte relokacije
    resolveRelocations();

    // Generisanje HEX izlaza ako je potrebno
    if (options.generateHex) {
        generateHexOutput(options.outputFileName);
    }

    //Generisanje binarnog izlaza sa fiksnim nazivom
    generateBinaryOutput("linker.o");

    return 0;
}