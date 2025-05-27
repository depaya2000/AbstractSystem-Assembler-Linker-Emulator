#include <iostream>
#include <fstream>
#include <cstdlib>
#include <iomanip>
#include "assembler.h"

extern FILE *yyin;
extern int yyparse();

// Indeks zaglavlja trenutne sekcije
uint32_t currentSectionIndex = 0;

// Inicijalizacija praznih vektora
    vector<ELF32_Shdr> sectionHeaders; // Tabela zaglavlja sekcija
    vector<string> stringNameTable;     // Tabela sa imenima sekcija i simbola
    vector<ELF32_Sym> symbolTable;      // Tabela simbola
    vector<SectionContent> sections; // sekcije za instrukcije i podatke
    vector<SectionRelocation> relocationSections; // Relokacione sekcije
    vector<ForwardReference> forwardReferenceTable; // Tabela obracanja unapred 
    vector<LiteralEntry> literalPool;      // Bazen literala

// Funkcija za inicijalizaciju svih struktura i vektora
void initialize() {
    
    // Dodaj imena sekcija u stringNameTable
    stringNameTable.push_back(".strtab"); // Sekcija za tabelu stringova
    stringNameTable.push_back(".symtab"); // Sekcija za tabelu simbola
    stringNameTable.push_back(".literal"); // Sekcija za bazen literala

    // Dodaj zaglavlja za stringNameTable, symbolTable, literalPool u sectionHeaders
    // Sekcija za imena sekcija (string table)
    ELF32_Shdr strTabHeader;
    memset(&strTabHeader, 0, sizeof(strTabHeader));
    strTabHeader.sh_name = addStringName(".strtab");
    strTabHeader.sh_type = SHT_STRTAB;
    strTabHeader.sh_flags = 0;
    strTabHeader.sh_addr = 0;
    strTabHeader.sh_offset = 0; // Ovo će biti ažurirano kasnije
    strTabHeader.sh_size = 0;   // Ovo će biti ažurirano kasnije
    strTabHeader.sh_link = 0;
    strTabHeader.sh_info = 2; // indeks ka bazenu literala
    strTabHeader.sh_addralign = 1;
    strTabHeader.sh_entsize = 0;
    sectionHeaders.push_back(strTabHeader);

    // Sekcija za tabelu simbola (symbol table)
    ELF32_Shdr symTabHeader;
    memset(&symTabHeader, 0, sizeof(symTabHeader));
    symTabHeader.sh_name = addStringName(".symtab");
    symTabHeader.sh_type = SHT_SYMTAB;
    symTabHeader.sh_flags = 0;
    symTabHeader.sh_addr = 0;
    symTabHeader.sh_offset = 0; // Ovo će biti ažurirano kasnije
    symTabHeader.sh_size = 0;   // Ovo će biti ažurirano kasnije
    symTabHeader.sh_link = 0;   
    symTabHeader.sh_info = 2;   // indeks ka bazenu literala
    symTabHeader.sh_addralign = 1;
    symTabHeader.sh_entsize = sizeof(ELF32_Sym);
    sectionHeaders.push_back(symTabHeader);

    // Sekcija za bazen literala (literal pool)
    ELF32_Shdr litPoolHeader;
    memset(&litPoolHeader, 0, sizeof(litPoolHeader));
    litPoolHeader.sh_name = addStringName(".literal");
    litPoolHeader.sh_type = SHT_LITERALPOOL;
    litPoolHeader.sh_flags = SHF_ALLOC | SHF_WRITE;;
    litPoolHeader.sh_addr = 0;
    litPoolHeader.sh_offset = 0; // Ovo će biti ažurirano kasnije
    litPoolHeader.sh_size = 0;   // Ovo će biti ažurirano kasnije
    litPoolHeader.sh_link = 0;
    litPoolHeader.sh_info = 2;  // indeks ka bazenu literala
    litPoolHeader.sh_addralign = 1;
    litPoolHeader.sh_entsize = 0;
    sectionHeaders.push_back(litPoolHeader);

    // Ažuriraj sve potrebne sekcije i tabele
    // (Ažuriranje offset-a i veličina sekcija se vrši kasnije kada su podaci poznati)
}

// Funkcija koja pronalazi sekciju po imenu i vraća njen indeks(indeks u tabeli zaglavlja sekcija)
uint32_t findSectionIndex(const char* sectionName) {
    // Konvertujemo 'sectionName' u std::string radi lakšeg poređenja
    std::string sectionNameStr(sectionName);

    // Prođi kroz sve zaglavlja sekcija
    for (size_t i = 0; i < sectionHeaders.size(); ++i) {
        // Uzmemo indeks imena sekcije iz zaglavlja
        uint32_t nameIndex = sectionHeaders[i].sh_name;

        // Proverimo da li ime sekcije u tabeli odgovara prosleđenom imenu
        if (stringNameTable[nameIndex] == sectionNameStr) {
            return static_cast<uint32_t>(i); // Vraćamo indeks pronađene sekcije iz sectionHeaders
        }
    }
    
    // Ako sekcija nije pronađena, vraćamo UINT32_MAX kao oznaku za grešku
    return UINT32_MAX;
}

// Funkcija koja pronalazi simbol po imenu i vraća njegov indeks u symbolTable
uint32_t findSymbolIndex(const char* symbolName) {
    // Konvertujemo 'symbolName' u std::string radi lakšeg poređenja
    std::string symbolNameStr(symbolName);

    // Prođi kroz sve simbole u tabeli simbola
    for (size_t i = 0; i < symbolTable.size(); ++i) {
        // Uzmemo indeks imena simbola iz ELF32_Sym zapisa
        uint32_t nameIndex = symbolTable[i].st_name;

        // Proverimo da li ime simbola u stringNameTable odgovara prosleđenom imenu
        if (stringNameTable[nameIndex] == symbolNameStr) {
            return static_cast<uint32_t>(i); // Vraćamo indeks simbola u symbolTable
        }
    }

    // Ako simbol nije pronađen, vraćamo UINT32_MAX kao oznaku za grešku
    return UINT32_MAX;
}

// Funkcija koja dodaje ime simbola i sekcija u tabelu imena i vraća indeks
uint32_t addStringName(const char* name) {
    // Konvertuj const char* u std::string
    std::string nameStr(name);

    // Pretraži tabelu za ime simbola
    for (size_t i = 0; i < stringNameTable.size(); ++i) {
        if (stringNameTable[i] == nameStr) {
            return static_cast<uint32_t>(i);
        }
    }

    // Dodaj ime u tabelu imena
    stringNameTable.push_back(nameStr);

    // Vraća indeks gde je ime dodato
    return static_cast<uint32_t>(stringNameTable.size() - 1);
}

// Funkcija koja traži literal ili simbol u bazenu i vraća indeks gde je smešten
int findOrAddLiteral(uint8_t type, int32_t index, uint32_t value) {
    // Pretraži literal ili simbol u bazenu
    for (size_t i = 0; i < literalPool.size(); ++i) {
        const LiteralEntry& entry = literalPool[i];
        if (entry.type == type && 
            entry.value == value && 
            entry.index == index) {
            return i; // Vraća indeks postojećeg unosa
        }
    }

    // Ako unos ne postoji, dodajemo ga u bazen i vraćamo novi indeks
    LiteralEntry newEntry;
    newEntry.type = type;
    newEntry.value = value;
    newEntry.index = index;

    literalPool.push_back(newEntry);
    return literalPool.size() - 1; // Indeks novog unosa
}


void addSymbol(const char* symbolName, bool isDefined, uint8_t type) {
    if (currentSectionIndex >= sectionHeaders.size()) {
        // Ispiši grešku na std::cerr i izađi iz funkcije
        std::cerr << "Error: Invalid current section index." << std::endl;
        return;
    }

    // Pokušaj da pronađeš simbol u tabeli simbola pomoću findSymbolIndex
    uint32_t symbolIndex = findSymbolIndex(symbolName);

    if (symbolIndex != UINT32_MAX) {
        // Simbol postoji, proveri da li je nedefinisan
        ELF32_Sym& symbol = symbolTable[symbolIndex];
        if (symbol.st_shndx == SHN_UNDEF) {
            // Simbol je nedefinisan, sada ga definiši
            if (isDefined) {
                symbol.st_value = sectionHeaders[currentSectionIndex].sh_size; // Postavi vrednost simbola
                symbol.st_shndx = static_cast<uint16_t>(currentSectionIndex);  // Postavi indeks trenutne sekcije
            }
        }
        // Simbol je već definisan, nema potrebe za daljom obradom
        return;
    }

    // Ako simbol nije pronađen, dodaj ime u tabelu stringova
    uint32_t nameIndex = addStringName(symbolName);

    // Kreiraj novi simbol
    ELF32_Sym newSymbol;
    newSymbol.st_name = nameIndex;      // Pomeraj u tabeli imena
    newSymbol.st_size = 0;              // Veličina simbola je inicijalno 0
    newSymbol.st_other = 0;             // Vidljivost simbola (STV_DEFAULT)

    if (isDefined) {
        // Ako je simbol definisan (npr. labela)
        newSymbol.st_value = sectionHeaders[currentSectionIndex].sh_size; // Vrednost simbola (trenutna veličina sekcije)
        newSymbol.st_shndx = static_cast<uint16_t>(currentSectionIndex);  // Postavi indeks trenutne sekcije
    } else {
        // Ako je simbol nedefinisan
        newSymbol.st_value = 0;                                           // Nema vrednost (0)
        newSymbol.st_shndx = SHN_UNDEF;                                   // Postavi indeks kao nedefinisan
    }

    // Postavi tip simbola na osnovu parametra `type`
    newSymbol.st_info = (STB_LOCAL << 4) | type;  // Lokalni simbol sa prosleđenim tipom

    // Dodaj simbol u tabelu simbola
    symbolTable.push_back(newSymbol);
}

// Funkcija za dodavanje forward reference
void addForwardReference(uint32_t symbolIndex, uint32_t currentAddress, uint32_t currentInstructionOffset,  uint32_t bitLength, uint32_t relocationType) {

    // Kreiraj ForwardReference
    ForwardReference newRef;
    newRef.unresolvedSymbolIndex = symbolIndex;
    newRef.sectionIndex = currentSectionIndex;
    newRef.address = currentAddress;
    newRef.instructionOffset = currentInstructionOffset;
    newRef.bitLength = bitLength;
    newRef.relocationType = relocationType;

    // Dodaj u tabelu forward reference
    forwardReferenceTable.push_back(newRef);
}

void addRelocationEntry(uint32_t currentSection, uint32_t lc, const ELF32_Sym& symbol, uint32_t index, uint32_t relocationType) {
    ELF32_Shdr& sectionHeader = sectionHeaders[currentSection];

    // Ako je sh_link nula, kreiraj novu relokacionu sekciju
    if (sectionHeader.sh_link == 0) {
        SectionRelocation newRelocationSection;
        newRelocationSection.headerIndex = sectionHeaders.size(); // Indeks za novu relokacionu sekciju
        newRelocationSection.relocationTable = {}; // Početno prazna tabela relokacija

        // Dodaj novu relokacionu sekciju u listu relokacionih sekcija
        relocationSections.push_back(newRelocationSection);

        // Ažuriraj sh_link u zaglavlju sekcije da pokazuje na novu relokacionu sekciju
        sectionHeader.sh_link = newRelocationSection.headerIndex;

        // Kreiraj novo zaglavlje za relokacionu sekciju
        ELF32_Shdr relocationHeader;
        memset(&relocationHeader, 0, sizeof(relocationHeader));
        relocationHeader.sh_name = addStringName((stringNameTable[sectionHeader.sh_name] + "_reloc").c_str());
        relocationHeader.sh_type = SHT_RELA; // Relokaciona sekcija
        relocationHeader.sh_flags = 0; // Nema specifičnih zastavica
        relocationHeader.sh_addr = 0;
        relocationHeader.sh_offset = 0; // Biće postavljeno kasnije
        relocationHeader.sh_size = 0;   // Biće postavljeno kasnije
        relocationHeader.sh_link = currentSection; // Povezuje relokacionu sekciju sa ciljanom sekcijom
        relocationHeader.sh_info = 0;
        relocationHeader.sh_addralign = 1;
        relocationHeader.sh_entsize = sizeof(ELF32_Rela); // Veličina jednog unosa u relokacionoj sekciji

        // Dodaj zaglavlje u vektor zaglavlja
        sectionHeaders.push_back(relocationHeader);
    }

    // Sada možemo da dodamo relokacioni zapis u odgovarajuću sekciju
    uint32_t relocationSectionIndex = sectionHeader.sh_link;
    if (relocationSectionIndex >= sectionHeaders.size()) {
        std::cerr << "Error: Relocation section index out of bounds" << std::endl;
        return;
    }

    // Pronalaženje odgovarajuće SectionRelocation strukture
    bool found = false;
    for (SectionRelocation& relocationSection : relocationSections) {
        if (relocationSection.headerIndex == relocationSectionIndex) {
            ELF32_Rela relocationEntry;
            relocationEntry.r_offset = lc;  // Offset unutar sekcije gde je potrebna relokacija
            if(relocationType == R_ABSWORD32){
                if (ELF32_ST_BIND(symbol.st_info) == STB_LOCAL) {
                    // Obrada lokalnog simbola
                    uint32_t sectionIndex = symbol.st_shndx;  // Sekcija u kojoj je simbol definisan
                    relocationEntry.r_info = (sectionIndex << 8) | R_ABSWORD32_LOCAL;  // Indeks sekcije
                    relocationEntry.r_addend = symbol.st_value;  // Offset simbola unutar sekcije
                } else {
                    // Obrada globalnog simbola
                    relocationEntry.r_info = (index << 8) | R_ABSWORD32_GLOBAL;  // Indeks simbola u tabeli simbola
                    relocationEntry.r_addend = 0;  //Uvek nula za globalne simbole
                }
            } else if(relocationType == R_ABSINST12){
                if (ELF32_ST_BIND(symbol.st_info) == STB_LOCAL) {
                    // Obrada lokalnog simbola
                    relocationEntry.r_info = (index << 8) | R_ABSINST12_LOCAL;
                    relocationEntry.r_addend = 0;
                } else {
                    // Obrada globalnog simbola
                    relocationEntry.r_info = (index << 8) | R_ABSINST12_GLOBAL;  // Indeks literala u bazenu literala
                    relocationEntry.r_addend = 0; 
                }
            }
            // Dodaj relokacioni zapis u tabelu relokacija
            relocationSection.relocationTable.push_back(relocationEntry);
			
			// Ažuriranje sh_size relokacione sekcije
			ELF32_Shdr& relocationHeader = sectionHeaders[relocationSectionIndex];
			relocationHeader.sh_size = relocationSection.relocationTable.size() * sizeof(ELF32_Rela);

            found = true;
            break;
        }
    }

    if (!found) {
        std::cerr << "Error: No matching relocation section found" << std::endl;
    }

}


void addRelocationEntryForLiteral(uint32_t currentSection, uint32_t lc, uint32_t index, uint32_t relocationType) {
    ELF32_Shdr& sectionHeader = sectionHeaders[currentSection];

    // Ako je sh_link nula, kreiraj novu relokacionu sekciju
    if (sectionHeader.sh_link == 0) {
        SectionRelocation newRelocationSection;
        newRelocationSection.headerIndex = sectionHeaders.size(); // Indeks za novu relokacionu sekciju
        newRelocationSection.relocationTable = {}; // Početno prazna tabela relokacija

        // Dodaj novu relokacionu sekciju u listu relokacionih sekcija
        relocationSections.push_back(newRelocationSection);

        // Ažuriraj sh_link u zaglavlju sekcije da pokazuje na novu relokacionu sekciju
        sectionHeader.sh_link = newRelocationSection.headerIndex;

        // Kreiraj novo zaglavlje za relokacionu sekciju
        ELF32_Shdr relocationHeader;
        memset(&relocationHeader, 0, sizeof(relocationHeader));
        relocationHeader.sh_name = addStringName((stringNameTable[sectionHeader.sh_name] + "_reloc").c_str());
        relocationHeader.sh_type = SHT_RELA; // Relokaciona sekcija
        relocationHeader.sh_flags = 0; // Nema specifičnih zastavica
        relocationHeader.sh_addr = 0;
        relocationHeader.sh_offset = 0; // Biće postavljeno kasnije
        relocationHeader.sh_size = 0;   // Biće postavljeno kasnije
        relocationHeader.sh_link = currentSection; // Povezuje relokacionu sekciju sa ciljanom sekcijom
        relocationHeader.sh_info = 0;
        relocationHeader.sh_addralign = 1;
        relocationHeader.sh_entsize = sizeof(ELF32_Rela); // Veličina jednog unosa u relokacionoj sekciji

        // Dodaj zaglavlje u vektor zaglavlja
        sectionHeaders.push_back(relocationHeader);
    }

    // Sada možemo da dodamo relokacioni zapis u odgovarajuću sekciju
    uint32_t relocationSectionIndex = sectionHeader.sh_link;
    if (relocationSectionIndex >= sectionHeaders.size()) {
        std::cerr << "Error: Relocation section index out of bounds" << std::endl;
        return;
    }

    // Pronalaženje odgovarajuće SectionRelocation strukture
    bool found = false;
    for (SectionRelocation& relocationSection : relocationSections) {
        if (relocationSection.headerIndex == relocationSectionIndex) {
            ELF32_Rela relocationEntry;
            relocationEntry.r_offset = lc;  // Offset unutar sekcije gde je potrebna relokacija
            relocationEntry.r_info = (index << 8) | relocationType;  // Indeks simbola u tabeli simbola
            relocationEntry.r_addend = 0;  

            // Dodaj relokacioni zapis u tabelu relokacija
            relocationSection.relocationTable.push_back(relocationEntry);
			
			// Ažuriranje sh_size relokacione sekcije
			ELF32_Shdr& relocationHeader = sectionHeaders[relocationSectionIndex];
			relocationHeader.sh_size = relocationSection.relocationTable.size() * sizeof(ELF32_Rela);

            found = true;
            break;
        }
    }

    if (!found) {
        std::cerr << "Error: No matching relocation section found" << std::endl;
    }

}

void handleSectionDirective(const char* sectionName) {
    // Pretraži tabelu za ime sekcije
    uint32_t sectionNameIndex = addStringName(sectionName);
    
    // Nađi sekciju po imenu
    uint32_t existingSectionIndex = findSectionIndex(sectionName);

    if (existingSectionIndex == UINT32_MAX) {
        // Kreiraj novi zaglavlje sekcije i inicijalizuj ga na 0
        ELF32_Shdr newSectionHeader;
        memset(&newSectionHeader, 0, sizeof(newSectionHeader));
        
        // Postavi ime sekcije koristeći prethodno pronađeni indeks
        newSectionHeader.sh_name = sectionNameIndex;
        
        // Postavi tip sekcije (npr. SHT_PROGBITS za obične sekcije)
        newSectionHeader.sh_type = SHT_PROGBITS;
        
        // Postavi zastavice (npr. SHF_ALLOC za sekcije koje se učitavaju u memoriju)
        newSectionHeader.sh_flags = SHF_ALLOC | SHF_EXECINSTR; // Alocirana i izvršna
        
        // Inicijalizuj offset i veličinu sekcije na 0 (biće postavljeno kasnije)
        newSectionHeader.sh_offset = 0;
        newSectionHeader.sh_size = 0;
        
        // Postavi ostale vrednosti na podrazumevane ili 0, ako nisu bitne
        newSectionHeader.sh_addr = 0;
        newSectionHeader.sh_link = 0;  // Ovo ćemo kasnije postaviti na indeks relokacione sekcije
        newSectionHeader.sh_info = 2;  // indeks ka bazenu literala
        newSectionHeader.sh_addralign = 1;
        newSectionHeader.sh_entsize = 0;

        // Dodaj zaglavlje nove sekcije u vektor zaglavlja sekcija
        sectionHeaders.push_back(newSectionHeader);

        // Indeks novog zaglavlja sekcije
        uint32_t newSectionHeaderIndex = static_cast<uint32_t>(sectionHeaders.size() - 1);

        // Kreiraj novi SectionContent za sadržaj sekcije
        SectionContent newSectionContent;
        newSectionContent.headerIndex = newSectionHeaderIndex; // Indeks zaglavlja
        newSectionContent.content.clear(); // Inicijalizacija praznog sadržaja sekcije 
        
        // Dodaj SectionContent u vektor sekcija
        sections.push_back(newSectionContent);
        
        // Kreiraj zaglavlje relokacione sekcije
        ELF32_Shdr relocationHeader;
        memset(&relocationHeader, 0, sizeof(relocationHeader));
        
        // Postavi ime relokacione sekcije (koristi isti naziv kao sekcija, ali dodaj sufiks "_reloc")
        std::string relocSectionName = std::string(sectionName) + "_reloc";
        uint32_t relocNameIndex = addStringName(relocSectionName.c_str());
        relocationHeader.sh_name = relocNameIndex;
        relocationHeader.sh_type = SHT_RELA; // Može biti SHT_RELA ili SHT_REL u zavisnosti od vašeg izbora
        relocationHeader.sh_flags = 0; // Relokacione sekcije obično nemaju posebne zastavice
        relocationHeader.sh_addr = 0;
        relocationHeader.sh_offset = 0; // Ovo će biti postavljeno kasnije
        relocationHeader.sh_size = 0;   // Ovo će biti postavljeno kasnije
        relocationHeader.sh_link = newSectionHeaderIndex; // Indeks sekcije kojoj je relokaciona sekcija namenjena
        relocationHeader.sh_info = 2;  // indeks ka bazenu literala
        relocationHeader.sh_addralign = 1;
        relocationHeader.sh_entsize = sizeof(ELF32_Rela); // Veličina jednog unosa u relokacionoj sekciji
        
        // Dodaj zaglavlje relokacione sekcije u vektor zaglavlja sekcija
        sectionHeaders.push_back(relocationHeader);

        // Kreiraj novi SectionRelocation za relokacionu sekciju
        SectionRelocation newRelocationSection;
        newRelocationSection.headerIndex = static_cast<uint32_t>(sectionHeaders.size() - 1); // Indeks zaglavlja
        newRelocationSection.relocationTable.clear(); // Inicijalizacija prazne tabele relokacija
        
        // Dodaj SectionRelocation u vektor relokacionih sekcija
        relocationSections.push_back(newRelocationSection);
        
        // Ažuriraj zaglavlje sekcije da pokaže na relokacionu sekciju
        sectionHeaders[newSectionHeaderIndex].sh_link = static_cast<uint32_t>(sectionHeaders.size() - 1);

        // Postavi trenutnu sekciju na novu sekciju
        currentSectionIndex = newSectionHeaderIndex;
        
        // Dodaj simbol u tabelu simbola koristeći addSymbol
        addSymbol(sectionName, true, STT_SECTION);  // Definišemo simbol za sekciju kao definisan

    } else {
        // Ako sekcija već postoji, postavi trenutnu sekciju na postojeću sekciju
        currentSectionIndex = existingSectionIndex;
    }

}


// Funkcija za obradu .global direktive
void handleGlobalDirective(const char* symbol) {
    uint32_t symbolIndex = findSymbolIndex(symbol);
    
    if (symbolIndex == UINT32_MAX) {
        // Ako simbol ne postoji, dodaj ga kao neodređeni globalni simbol
        ELF32_Sym newSymbol;
        memset(&newSymbol, 0, sizeof(newSymbol));
        
        // Postavi ime simbola
        newSymbol.st_name = addStringName(symbol);

        // Postavi ga kao globalan, tip je neodređen (STT_NOTYPE)
        newSymbol.st_info = ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        newSymbol.st_shndx = SHN_UNDEF; // Simbol još nije definisan
        
        // Dodaj simbol u tabelu simbola
        symbolTable.push_back(newSymbol);

    } else {
        // Simbol već postoji, postavi ga kao globalan ako nije već globalan
        ELF32_Sym& existingSymbol = symbolTable[symbolIndex];
        
        // Proveri da li je simbol već globalan
        if (ELF32_ST_BIND(existingSymbol.st_info) != STB_GLOBAL) {
            // Ažuriraj st_info da bude globalan, očuvaj trenutni tip
            existingSymbol.st_info = ELF32_ST_INFO(STB_GLOBAL, ELF32_ST_TYPE(existingSymbol.st_info));
        }
        // Ako je već globalan, ništa ne radimo
    }
}

void handleExternDirective(const char* symbol) {
     uint32_t symbolIndex = findSymbolIndex(symbol);
    
    if (symbolIndex == UINT32_MAX) {
        // Ako simbol ne postoji, dodaj ga kao neodređeni globalni simbol
        ELF32_Sym newSymbol;
        memset(&newSymbol, 0, sizeof(newSymbol));
        
        // Postavi ime simbola
        newSymbol.st_name = addStringName(symbol);

        // Postavi ga kao globalan, tip je neodređen (STT_NOTYPE)
        newSymbol.st_info = ELF32_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        newSymbol.st_shndx = SHN_UNDEF; // Simbol još nije definisan
        
        // Dodaj simbol u tabelu simbola
        symbolTable.push_back(newSymbol);

    } else {
        // Simbol već postoji, postavi ga kao globalan ako nije već globalan
        ELF32_Sym& existingSymbol = symbolTable[symbolIndex];
        
        // Proveri da li je simbol već globalan
        if (ELF32_ST_BIND(existingSymbol.st_info) != STB_GLOBAL) {
            // Ažuriraj st_info da bude globalan, očuvaj trenutni tip
            existingSymbol.st_info = ELF32_ST_INFO(STB_GLOBAL, ELF32_ST_TYPE(existingSymbol.st_info));
        }
        // Ako je već globalan, ništa ne radimo
    }
}

void handleWordDirectiveWithLiteral(int literal) {
    std::vector<unsigned char> data(4, 0);                      // Alociraj prostor za 4 bajta

    // Ažuriraj `sh_flags` sekcije ako je potrebno
    if (sectionHeaders[currentSectionIndex].sh_flags & SHF_EXECINSTR) {
        // Ukloni SHF_EXECINSTR iz trenutne sekcije
        sectionHeaders[currentSectionIndex].sh_flags &= ~SHF_EXECINSTR;

        // Dodaj SHF_WRITE
        sectionHeaders[currentSectionIndex].sh_flags |= SHF_WRITE;
    }

    // Kopiraj literalnu vrednost u podatke
    memcpy(data.data(), &literal, sizeof(literal));

    // Dodaj podatke u sekciju
    addDataToSection(data);
}

void handleWordDirectiveWithSymbol(const char* symbolName) {
    uint32_t lc = sectionHeaders[currentSectionIndex].sh_size;  // Trenutna pozicija u sekciji
    std::vector<unsigned char> data(4, 0);                      // Alociraj prostor za 4 bajta

    // Ažuriraj `sh_flags` sekcije ako je potrebno
    if (sectionHeaders[currentSectionIndex].sh_flags & SHF_EXECINSTR) {
        // Ukloni SHF_EXECINSTR iz trenutne sekcije
        sectionHeaders[currentSectionIndex].sh_flags &= ~SHF_EXECINSTR;

        // Dodaj SHF_WRITE
        sectionHeaders[currentSectionIndex].sh_flags |= SHF_WRITE;
    }

    uint32_t symbolIndex = findSymbolIndex(symbolName);  // Pronađi indeks simbola

    if (symbolIndex != UINT32_MAX) {
        // Simbol postoji u tabeli simbola
        ELF32_Sym& symbol = symbolTable[symbolIndex];

        if (symbol.st_shndx != SHN_UNDEF) {
            // Simbol je definisan
            uint32_t symbolValue = symbol.st_value;
            memcpy(data.data(), &symbolValue, sizeof(symbolValue));

            // Dodaj relokacioni zapis jer treba stvarnu adresu da upisemo koju dobijamo tek u linkeru, a ne offset unutar sekcije
            addRelocationEntry(currentSectionIndex, lc, symbol, symbolIndex, R_ABSWORD32);
        } else {
            // Simbol je nedefinisan - dodaj u forward reference listu
            addForwardReference(symbolIndex, lc, 0, 32, R_ABSWORD32);
        }
    } else {
        // Simbol nije pronađen - dodaj simbol kao nedefinisan u tabelu simbola
        addSymbol(symbolName, false, STT_NOTYPE);  // Dodaj simbol kao nedefinisan
        symbolIndex = findSymbolIndex(symbolName);  // Osveži simbol nakon dodavanja

        // Dodaj forward referencu na ovu instrukciju
        addForwardReference(symbolIndex, lc, 0, 32, R_ABSWORD32);
    }

    // Dodaj podatke u sekciju (ostavi placeholder vrednosti - nule)
    addDataToSection(data);
}


void handleSkipDirective(int numBytes) {
    // Proveravamo da li je broj bajtova validan
    if (numBytes <= 0) {
        std::cerr << "Error: Invalid number of bytes for .skip directive!" << std::endl;
        return;
    }

    // Kreiramo vektor nula, dužine numBytes
    std::vector<unsigned char> zeroData(numBytes, 0); // numBytes bajtova, sve inicijalizovano na 0

    // Dodajemo podatke (nule) u trenutnu sekciju
    addDataToSection(zeroData);  // Koristimo postojeću funkciju

}

void addDataToSection(const std::vector<unsigned char>& data) {
    // Pronalazak odgovarajuće sekcije na osnovu headerIndex-a
    SectionContent* targetSection = nullptr;
    for (auto& section : sections) {
        if (section.headerIndex == currentSectionIndex) {
            targetSection = &section;
            break;
        }
    }

    // Ako sekcija nije pronađena, ispiši grešku i izađi iz funkcije
    if (targetSection == nullptr) {
        std::cerr << "Error: Current section not found." << std::endl;
        return;
    }

    // Dodaj podatke na kraj sadržaja sekcije
    targetSection->content.insert(targetSection->content.end(), data.begin(), data.end());

    // Ažuriraj veličinu sekcije u ELF zaglavlju
    if (currentSectionIndex < sectionHeaders.size()) {
        sectionHeaders[currentSectionIndex].sh_size += data.size();
    }
}

// Funkcija koja obrađuje HALT instrukciju
void handleHaltInstruction() {
    // Formiranje instrukcije
    uint32_t instruction = 0x00000000;

    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    // Dodavanje instrukcije u trenutnu sekciju koda
    addDataToSection(binaryInstruction);
}

void handleIntInstruction() {
    // Formiranje instrukcije
    uint32_t instruction = 0;

    // Polje OC za softverski prekid (OC = 0001)
    instruction |= (0b0001 << 28);  // OC [31:28] (4 najviša bita postavljena na 0001)

    // Ostatak instrukcije je nula
    instruction |= 0x0000000;  // Ostalih 28 bita su svi 0

    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    // Dodavanje instrukcije u trenutnu sekciju koda
    addDataToSection(binaryInstruction);
}

void handleIretInstruction() {
    // 1. Formiranje instrukcije za add sp, 8
    uint32_t instructionAddSP = 0;
    // Polje za OC (1001) - instrukcija za učitavanje podataka
    instructionAddSP |= (0x9 << 28);  // OC = 1001
    // Polje MMMM = 0b0001 za add (gpr[A] <= gpr[B] + D)
    instructionAddSP |= (0x1 << 24);  // MMMM = 0b0001
    // Polje AAAA: registar SP
    int spReg = 14;  // SP je registar r14
    instructionAddSP |= (spReg << 20);  // Postavljamo registar A (SP) u polje AAAA
    // Polje BBBB: registar SP
    instructionAddSP |= (spReg << 16);  // Postavljamo registar B (SP) u polje BBBB
    // Polje DDDD DDDD DDDD DDDD: pomeraj 8
    instructionAddSP |= (8 & 0xFFFF);  // Postavljamo pomeraj D na 8

    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstructionAddSP(4);
    binaryInstructionAddSP[0] = instructionAddSP & 0xFF;
    binaryInstructionAddSP[1] = (instructionAddSP >> 8) & 0xFF;
    binaryInstructionAddSP[2] = (instructionAddSP >> 16) & 0xFF;
    binaryInstructionAddSP[3] = (instructionAddSP >> 24) & 0xFF;

    // Dodavanje instrukcije za add sp, 8 u sekciju koda
    addDataToSection(binaryInstructionAddSP);

    // 2. Formiranje instrukcije za ld status
    uint32_t instructionLdStatus = 0;
    // Polje za OC (1001) - instrukcija za učitavanje podataka
    instructionLdStatus |= (0x9 << 28);  // OC = 1001
    // Polje MMMM = 0b0110 za ld status bez azuriranja SP
    instructionLdStatus |= (0x6 << 24);  // MMMM = 0b0110 (csr[A] <= mem32[gpr[B] + gpr[C] + D])
    // Polje AAAA: registar Status
    int statusReg = 0;  // Status registar (CSR)
    instructionLdStatus |= (statusReg << 20);  // Postavljamo CSR A (Status)
    // Polje BBBB: registar SP (r14)
    instructionLdStatus |= (spReg << 16);  // Postavljamo registar B (SP)
    // Polje CCCC: pomocni registar ili 0 ako nema C registra
    int cReg = 0;  // Ovde ćemo koristiti 0 za C registar
    instructionLdStatus |= (cReg << 12);  // Postavljamo C registar na 0
    // Polje DDDD DDDD DDDD DDDD: pomeraj 0
    instructionLdStatus |= (0x0);  // Offset postavljamo na 0

    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstructionLdStatus(4);
    binaryInstructionLdStatus[0] = instructionLdStatus & 0xFF;
    binaryInstructionLdStatus[1] = (instructionLdStatus >> 8) & 0xFF;
    binaryInstructionLdStatus[2] = (instructionLdStatus >> 16) & 0xFF;
    binaryInstructionLdStatus[3] = (instructionLdStatus >> 24) & 0xFF;

    // Dodavanje instrukcije za ld status u sekciju koda
    addDataToSection(binaryInstructionLdStatus);

    // 3. Formiranje instrukcije za ld pc
    uint32_t instructionLdPC = 0;
    // Polje za OC (1001) - instrukcija za učitavanje podataka
    instructionLdPC |= (0x9 << 28);  // OC = 1001
    // Polje MMMM = 0b0010 za ld pc bez azuriranja SP
    instructionLdPC |= (0x2 << 24);  // MMMM = 0b0010 (gpr[A] <= mem32[gpr[B] + gpr[C] + D])
    // Polje AAAA: registar PC
    int pcReg = 15;  // PC je registar r15
    instructionLdPC |= (pcReg << 20);  // Postavljamo registar A (PC)
    // Polje BBBB: registar SP
    instructionLdPC |= (spReg << 16);  // Postavljamo registar B (SP)
    // Polje CCCC: pomocni registar ili 0 ako nema C registra
    instructionLdPC |= (cReg << 12);  // Postavljamo C registar na 0
    // Polje DDDD DDDD DDDD DDDD: offset 0
    instructionLdPC |= (0x0);  // Offset postavljamo na 0

    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstructionLdPC(4);
    binaryInstructionLdPC[0] = instructionLdPC & 0xFF;
    binaryInstructionLdPC[1] = (instructionLdPC >> 8) & 0xFF;
    binaryInstructionLdPC[2] = (instructionLdPC >> 16) & 0xFF;
    binaryInstructionLdPC[3] = (instructionLdPC >> 24) & 0xFF;

    // Dodavanje instrukcije za ld pc u sekciju koda
    addDataToSection(binaryInstructionLdPC);
}

void handleCallInstruction(const char* operand) {
    // 1. Generišemo instrukciju push za trenutno stanje pc
    uint32_t pushInstruction = 0;
    pushInstruction |= (0x8 << 28);  // OC = 1000 za push
    pushInstruction |= (0x1 << 24);  // MMMM = 0b0001 za push
    int spReg = 14; // sp registar je r14
    pushInstruction |= (spReg << 20);  // Postavljamo sp (registar A) u polje AAAA
    int pcReg = 15; // pc registar je r15
    pushInstruction |= (pcReg << 8);  // Postavljamo pc (registar C) u polje CCCC

    // Offset za smanjenje steka
    int32_t offset = -4;
    pushInstruction |= (offset & 0xFFF); // Postavljamo 12-bitno polje D u instrukciji

    // Konvertujemo instrukciju u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryPushInstruction(4);
    binaryPushInstruction[0] = pushInstruction & 0xFF;
    binaryPushInstruction[1] = (pushInstruction >> 8) & 0xFF;
    binaryPushInstruction[2] = (pushInstruction >> 16) & 0xFF;
    binaryPushInstruction[3] = (pushInstruction >> 24) & 0xFF;

    // Dodajemo push instrukciju u trenutnu sekciju koda
    addDataToSection(binaryPushInstruction);

    // 2. Generišemo instrukciju jump za operand
    handleJumpInstruction(operand);
}

void handleRetInstruction() {
    uint32_t instruction = 0;
    // Polje OC za pop (1001)
    instruction |= (0x9 << 28);  // Postavljamo prvih 4 bita na 1001 za pop operaciju
    // Polje MMMM, 0b0011 za pop gde gpr[A]<=mem32[gpr[B]]; gpr[B]<=gpr[B]+D
    instruction |= (0x3 << 24);  // MMMM = 0b0011 za pop
    // Polje AAAA: registar PC
    int PcReg = 15; 
    instruction |= (PcReg << 20);  // Postavljamo registar A (PC) u polje AAAA
    // Polje BBBB: registar SP
    int spReg = 14;
    instruction |= (spReg << 16);  // Postavljamo registar B (SP) u polje BBBB
    // Polje DDDD DDDD DDDD DDDD: offset 4
    int16_t offset = 4;
    instruction |= (offset & 0xFFFF);  // Postavljamo 16-bitni offset u polje D

    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    // Dodavanje instrukcije u trenutnu sekciju koda
    addDataToSection(binaryInstruction);
}

// Funkcija za dobijanje indeksa registra
int32_t getRegisterIndex(const std::string& reg) {
    if (reg == "%r0") return 0;
    if (reg == "%r1") return 1;
    if (reg == "%r2") return 2;
    if (reg == "%r3") return 3;
    if (reg == "%r4") return 4;
    if (reg == "%r5") return 5;
    if (reg == "%r6") return 6;
    if (reg == "%r7") return 7;
    if (reg == "%r8") return 8;
    if (reg == "%r9") return 9;
    if (reg == "%r10") return 10;
    if (reg == "%r11") return 11;
    if (reg == "%r12") return 12; 
    if (reg == "%r13") return 13;
    if (reg == "%r14") return 14; // SP registar
    if (reg == "%r15") return 15; // PC registar

    // Greška ako je nevalidan registar
    std::cerr << "Error: Invalid register '" << reg << "'." << std::endl;
    return -1; // Vraća -1 kao indikator greške
}

// Funkcija za dobijanje indeksa CSR registra prema imenu
int getCSRIndex(const std::string& csrName) {
    if (csrName == "%status") return 0;
    if (csrName == "%handler") return 1;
    if (csrName == "%cause") return 2;
    return -1; // Nevalidan CSR registar
}

void handleJumpInstruction(const char* operand){
    uint32_t lc = sectionHeaders[currentSectionIndex].sh_size;  // Trenutna pozicija u sekciji
    uint32_t instruction = 0;
    // Postavljamo OC na 0011 (instrukcija skoka) 
    instruction |= (0x3 << 28); // OC = 0011  
        // Literal ili simbol, direktna vrednost
    if (isdigit(operand[0]) || (operand[0] == '-' && isdigit(operand[1]))) { 
        // <literal> - literalna vrednost
        instruction |= (0x1 << 24); // MMMM = 0b0000
        std::string literalStr(operand);
        int literal = std::stoi(literalStr.substr(1)); // Konverzija stringa u broj
        int literalIndex = findOrAddLiteral(LITERAL_VALUE, -1, literal);
        if (literalIndex >= (1 << 12)) {
            std::cerr << "Error: Literal index exceeds 12 bits limit!" << std::endl;
            return;
        }
        instruction |= (literalIndex & 0xFFF); // 12-bitno polje D

        // Dodaj relokacioni zapis kako bi u njemu pamtili indeks literala u bazenu literala
        addRelocationEntryForLiteral(currentSectionIndex, lc, literalIndex, R_ABSINST12);
    } else {
        // <simbol> - vrednost simbola
        const char* symbolName = operand ; 
        uint32_t symbolIndex = findSymbolIndex(symbolName);
        

        if (symbolIndex != UINT32_MAX) {
            ELF32_Sym& symbol = symbolTable[symbolIndex];

            if (symbol.st_shndx != SHN_UNDEF) {
                uint32_t symbolValue = symbol.st_value;
                uint32_t literalIndex;
                if (ELF32_ST_BIND(symbol.st_info) == STB_LOCAL){
                    literalIndex = findOrAddLiteral(LOCAL_SYMBOL, symbol.st_shndx, symbolValue);
                }
                else if(ELF32_ST_BIND(symbol.st_info) == STB_GLOBAL){
                    literalIndex = findOrAddLiteral(GLOBAL_SYMBOL, symbolIndex, symbolValue);
                }

                // Dodaj relokacioni zapis
                addRelocationEntry(currentSectionIndex, lc, symbol, literalIndex, R_ABSINST12);
                
            } else {
                addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
                instruction |= 0x0;
            }
        } else {
            addSymbol(symbolName, false, STT_NOTYPE);
            symbolIndex = findSymbolIndex(symbolName); // Osveži simbol nakon dodavanja
            addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
            instruction |= 0x0;
        }
    }
    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    // Dodavanje instrukcije u trenutnu sekciju koda
    addDataToSection(binaryInstruction);
}

void handleBranchInstruction(const char* gpr1, const char* gpr2, const char* operand, const std::string& instructionType) {

    uint32_t lc = sectionHeaders[currentSectionIndex].sh_size;  // Trenutna pozicija u sekciji

    uint32_t instruction = 0;

    // Postavljamo OC na 0011 (instrukcija skoka)
    instruction |= (0x3 << 28); // OC = 0011

    // Postavljanje MMMM na osnovu vrste instrukcije
    uint32_t mmmmValue;
    if (instructionType == "beq") {
        mmmmValue = 0x1; // MMMM = 0b0001 za beq
    } else if (instructionType == "bne") {
        mmmmValue = 0x2; // MMMM = 0b0010 za bne
    } else if (instructionType == "bgt") {
        mmmmValue = 0x3; // MMMM = 0b0011 za bgt
    } else {
        std::cerr << "Error: Invalid instruction type." << std::endl;
        return;
    }
    instruction |= (mmmmValue << 24);

    // Konverzija const char* u std::string
    std::string reg1_str(gpr1);
    std::string reg2_str(gpr2);

    // Pronalaženje indeksa registara
    int reg1 = getRegisterIndex(reg1_str); // Funkcija koja vraća indeks za registar
    int reg2 = getRegisterIndex(reg2_str); // Funkcija koja vraća indeks za registar

    // Provera da li su registri validni
    if (reg1 == -1 || reg2 == -1) {
        std::cerr << "Error: Invalid register in branch instruction." << std::endl;
        return;
    }

    instruction |= (reg1 << 16); // BBBB
    instruction |= (reg2 << 12); // CCCC

    // Obrada operanda - literal ili simbol
    std::string operand_str(operand);
    if (isdigit(operand_str[0]) || (operand_str[0] == '-' && isdigit(operand_str[1]))) {
        // <literal> - literalna vrednost
        int literal = std::stoi(operand_str); // Konverzija stringa u broj
        uint32_t literalIndex = findOrAddLiteral(LITERAL_VALUE, -1, literal);

        if (literalIndex >= (1 << 12)) {
            std::cerr << "Error: Literal index exceeds 12 bits limit!" << std::endl;
            return;
        }

        instruction |= (literalIndex & 0xFFF); // 12-bitno polje D

        
        // Dodaj relokacioni zapis kako bi u njemu pamtili indeks literala u bazenu literala
        addRelocationEntryForLiteral(currentSectionIndex, lc, literalIndex, R_ABSINST12);
    } else {
        // <simbol> - vrednost simbola
        const char* symbolName = operand_str.c_str();
        uint32_t symbolIndex = findSymbolIndex(symbolName);
        

        if (symbolIndex != UINT32_MAX) {
            ELF32_Sym& symbol = symbolTable[symbolIndex];

            if (symbol.st_shndx != SHN_UNDEF) {
                int32_t symbolValue = static_cast<int32_t>(symbol.st_value); // Koristimo potpisani 32-bitni broj
                uint32_t literalIndex;
                if (ELF32_ST_BIND(symbol.st_info) == STB_LOCAL){
                    literalIndex = findOrAddLiteral(LOCAL_SYMBOL, symbol.st_shndx, symbolValue);

                } else {
                    literalIndex = findOrAddLiteral(GLOBAL_SYMBOL, symbolIndex, symbolValue);
                }

                if (literalIndex >= (1 << 12)) {
                    std::cerr << "Error: Literal index exceeds 12 bits limit!" << std::endl;
                    return;
                }

                instruction |= (literalIndex & 0xFFF);

                // Dodaj relokacioni zapis
                addRelocationEntry(currentSectionIndex, lc, symbol, literalIndex, R_ABSINST12);

            } else {
                addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
                instruction |= (0x0 & 0xFFF); // Placeholder za buduće rešavanje
            }
        } else {
            addSymbol(symbolName, false, STT_NOTYPE);
            symbolIndex = findSymbolIndex(symbolName); // Osveži simbol nakon dodavanja
            addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
            instruction |= (0x0 & 0xFFF); // Placeholder za buduće rešavanje
        }
    }

    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    // Dodavanje instrukcije u trenutnu sekciju koda
    addDataToSection(binaryInstruction);
}

void addArithmeticInstruction(const std::string& operation, const char* gprS, const char* gprD) {
    // Konverzija const char* u std::string
    std::string regS_str(gprS);
    std::string regD_str(gprD);

    // Pronalaženje indeksa registara
    int regS = getRegisterIndex(regS_str);  // Funkcija koja vraća indeks za registar
    int regD = getRegisterIndex(regD_str);  // Funkcija koja vraća indeks za registar

    // Provera da li su registri validni
    if (regS == -1 || regD == -1) {
        std::cerr << "Error: Invalid register in arithmetic instruction." << std::endl;
        return;
    }

    // Formiranje instrukcije
    uint32_t instruction = 0;

    // Polje OC za aritmetičke operacije, OC = 0101 (za sve ove operacije)
    instruction |= (0b0101 << 28);  // OC [31:28]

    // Postavi vrednost modifikatora (bitovi 24-27) na osnovu operacije
    if (operation == "add") {
        instruction |= (0b0000 << 24);  // MMMM = 0000 za sabiranje
    } else if (operation == "sub") {
        instruction |= (0b0001 << 24);  // MMMM = 0001 za oduzimanje
    } else if (operation == "mul") {
        instruction |= (0b0010 << 24);  // MMMM = 0010 za množenje
    } else if (operation == "div") {
        instruction |= (0b0011 << 24);  // MMMM = 0011 za deljenje
    } else {
        std::cerr << "Error: Unknown arithmetic operation: " << operation << std::endl;
        return;
    }

    // Postavljanje RegA, RegB, RegC
    instruction |= (regD << 20);  // RegA [23:20]
    instruction |= (regD << 16);  // RegB [19:16]
    instruction |= (regS << 12);  // RegC [15:12]

    // Polje Disp je nula za ove instrukcije
    instruction |= 0x000;  // Disp [11:0] je 000000000000

    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    // Dodavanje instrukcije u trenutnu sekciju koda
    addDataToSection(binaryInstruction);
}

void addLogicalInstruction(const std::string& operation, const char* gprS, const char* gprD) {
    // Pronalaženje indeksa registara
    int regS = getRegisterIndex(gprS);  // Funkcija koja vraća indeks za registar
    int regD = (gprD != nullptr) ? getRegisterIndex(gprD) : -1;  // Ako gprD nije nullptr, pronađi indeks

    // Provera da li je registar validan
    if (regS == -1 || (gprD != nullptr && regD == -1)) {
        std::cerr << "Error: Invalid register in logical instruction." << std::endl;
        return;
    }

    // Formiranje instrukcije
    uint32_t instruction = 0;

    // Polje OC za logičke operacije, OC = 0110
    instruction |= (0b0110 << 28);  // OC [31:28]

    // Postavi vrednost modifikatora (bitovi 24-27) na osnovu operacije
    if (operation == "not") {
        instruction |= (0b0000 << 24);  // MMMM = 0000 za not
        instruction |= (regS << 20);    // RegA [23:20]
        instruction |= (0xD << 16);     // RegB [19:16] (konstantna vrednost za not)
        instruction |= (0x0 << 12);     // RegC [15:12] nije korišćen
    } else if (operation == "and") {
        instruction |= (0b0001 << 24);  // MMMM = 0001 za and
        instruction |= (regD << 20);    // RegA [23:20]
        instruction |= (regD << 16);    // RegB [19:16]
        instruction |= (regS << 12);    // RegC [15:12]
    } else if (operation == "or") {
        instruction |= (0b0010 << 24);  // MMMM = 0010 za or
        instruction |= (regD << 20);    // RegA [23:20]
        instruction |= (regD << 16);    // RegB [19:16]
        instruction |= (regS << 12);    // RegC [15:12]
    } else if (operation == "xor") {
        instruction |= (0b0011 << 24);  // MMMM = 0011 za xor
        instruction |= (regD << 20);    // RegA [23:20]
        instruction |= (regD << 16);    // RegB [19:16]
        instruction |= (regS << 12);    // RegC [15:12]
    } else {
        std::cerr << "Error: Unknown logical operation: " << operation << std::endl;
        return;
    }

    // Polje Disp je nula za ovu instrukciju
    instruction |= 0x000;  // Disp [11:0] je 000000000000

    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    // Dodavanje instrukcije u trenutnu sekciju koda
    addDataToSection(binaryInstruction);
}


void addShiftInstruction(const std::string& operation, const char* gprS, const char* gprD) {
    // Konverzija const char* u std::string
    std::string regS_str(gprS);
    std::string regD_str(gprD);

    // Pronalaženje indeksa registara
    int regS = getRegisterIndex(regS_str);  // Funkcija koja vraća indeks za registar
    int regD = getRegisterIndex(regD_str);  // Funkcija koja vraća indeks za registar

    // Provera da li su registri validni
    if (regS == -1 || regD == -1) {
        std::cerr << "Error: Invalid register in shift instruction." << std::endl;
        return;
    }

    // Formiranje instrukcije
    uint32_t instruction = 0;

    // Polje OC za pomeračke operacije, OC = 0111
    instruction |= (0b0111 << 28);  // OC [31:28]

    // Postavi vrednost modifikatora (bitovi 24-27) na osnovu operacije
    if (operation == "shl") {
        instruction |= (0b0000 << 24);  // MMMM = 0000 za pomeranje ulevo
    } else if (operation == "shr") {
        instruction |= (0b0001 << 24);  // MMMM = 0001 za pomeranje udesno
    } else {
        std::cerr << "Error: Unknown shift operation: " << operation << std::endl;
        return;
    }

    // Postavljanje RegA, RegB, RegC
    instruction |= (regD << 20);  // RegA [23:20] - odredišni registar
    instruction |= (regD << 16);  // RegB [19:16] - registar sa vrednošću za pomeranje
    instruction |= (regS << 12);  // RegC [15:12] - registar koji sadrži broj bitova za pomeranje

    // Polje Disp je nula za ovu instrukciju
    instruction |= 0x000;  // Disp [11:0] je 000000000000

    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    // Dodavanje instrukcije u trenutnu sekciju koda
    addDataToSection(binaryInstruction);
}

void handleXchgInstruction(const char* gprS, const char* gprD) {
    // Konverzija const char* u std::string
    std::string regS_str(gprS);
    std::string regD_str(gprD);

    // Pronalazak indeksa registara
    int regS = getRegisterIndex(regS_str);  // Funkcija koja vraća indeks za registar
    int regD = getRegisterIndex(regD_str);  // Funkcija koja vraća indeks za registar

    // Provera da li su registri validni
    if (regS == -1 || regD == -1) {
        std::cerr << "Error: Invalid register in xchg instruction." << std::endl;
        return;
    }

    // Formiranje instrukcije
    uint32_t instruction = 0;

    // Polje OC za atomičnu zamenu vrednosti, OC = 0100 (za ovu operaciju)
    instruction |= (0b0100 << 28);  // OC [31:28]

    // Postavljanje RegB i RegC
    instruction |= (regD << 16);  // RegB [19:16] (gprD je odredište)
    instruction |= (regS << 12);  // RegC [15:12] (gprS je izvor)

    // Polje Disp je nula za ovu instrukciju
    instruction |= 0x000;  // Disp [11:0] je 000000000000

    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    // Dodavanje instrukcije u trenutnu sekciju koda
    addDataToSection(binaryInstruction);
}

void handlePushInstruction(const char* gpr) {
    // Pretvaranje imena registra u string
    std::string reg_str(gpr);
    
    // Dobijanje indeksa registra
    int reg = getRegisterIndex(reg_str);
    
    // Provera da li je registar validan
    if (reg == -1) {
        std::cerr << "Error: Invalid register in push instruction." << std::endl;
        return;
    }
    // 1. Formiranje instrukcije
    uint32_t instruction = 0;
    // Polje za OC (1000) - smeštanje podatka
    instruction |= (0x8 << 28);  // Postavljamo prvih 4 bita na 1000 za push operaciju
    // Polje MMMM, može biti 0b0001 za tipičan push gde mem32[gpr[A]] <= gpr[C]
    instruction |= (0x1 << 24);  // MMMM = 0b0001 za push
    // Polje AAAA: registar sp (r14)
    int spReg = 14; // sp registar je r14
    instruction |= (spReg << 20);  // Postavljamo sp (registar A) u polje AAAA
    // Polje CCCC: registar koji želimo da sačuvamo (gpr)
    instruction |= (reg << 8);  // Postavljamo gpr u polje CCCC
    // DDDD DDDD DDDD DDDD: ovde možemo staviti offset -4 jer smanjujemo stek
    int32_t offset = -4;
 
    instruction |= (offset & 0xFFF); // Postavljamo 12-bitno polje D u instrukciji 
    // 2. Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    // 3. Dodavanje instrukcije u trenutnu sekciju koda
    addDataToSection(binaryInstruction);
}

void handlePopInstruction(const char* gpr) {
    // Pretvaranje imena registra u string
    std::string reg_str(gpr);
    
    // Dobijanje indeksa registra
    int reg = getRegisterIndex(reg_str);
    
    // Provera da li je registar validan
    if (reg == -1) {
        std::cerr << "Error: Invalid register in pop instruction." << std::endl;
        return;
    }
    // 1. Formiranje instrukcije
    uint32_t instruction = 0;
    // Polje za OC (1001) - učitavanje podatka
    instruction |= (0x9 << 28);  // Postavljamo prvih 4 bita na 1001 za pop operaciju
    // Polje MMMM, može biti 0b0011 za pop gde gpr[A]<=mem32[gpr[B]]; gpr[B]<=gpr[B]+D
    instruction |= (0x3 << 24);  // MMMM = 0b0011 za pop
    // Polje AAAA: registar A (gpr) u koji učitavamo podatak
    instruction |= (reg << 20);  // Postavljamo registar A (gpr) u polje AAAA
    // Polje BBBB: registar B (sp) koji pokazuje na memorijsku adresu
    int spReg = 14; // sp registar je r14
    instruction |= (spReg << 16);  // Postavljamo sp (registar B) u polje BBBB
    // DDDD DDDD DDDD DDDD: offset 4
     // Offset za smanjenje steka
    uint32_t offset = 4;
    instruction |= (offset & 0xFFF); // Postavljamo 12-bitno polje D u instrukciji
    // 2. Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    addDataToSection(binaryInstruction);
}

void handleCsrrdInstruction(const char* csr, const char* gpr) {
    // Pretvaranje imena CSR i GPR registara u stringove
    std::string csr_str(csr);
    std::string reg_str(gpr);
    
    // Dobijanje indeksa CSR i GPR registara
    int csrIndex = getCSRIndex(csr_str);
    int regIndex = getRegisterIndex(reg_str);
    
    // Provera da li su registri validni
    if (csrIndex == -1 || regIndex == -1) {
        std::cerr << "Error: Invalid CSR or GPR register in csrrd instruction." << std::endl;
        return;
    }

    // 1. Formiranje instrukcije
    uint32_t instruction = 0;

    // Polje za OC (1001) - učitavanje podatka
    instruction |= (0x9 << 28);  // Postavljamo prvih 4 bita na 1001 za csrrd operaciju
    
    // Polje MMMM, može biti 0b0000 za csrrd gde gpr[A]<=csr[B]
    instruction |= (0x0 << 24);  // MMMM = 0b0000 za csrrd

    // Polje AAAA: registar A (gpr) u koji učitavamo podatak
    instruction |= (regIndex << 20);  // Postavljamo registar A (gpr) u polje AAAA
    
    // Polje BBBB: registar B (csr) iz kojeg učitavamo podatak
    instruction |= (csrIndex << 16);  // Postavljamo CSR registar u polje BBBB

    // DDDD DDDD DDDD: offset 0 (nije korišćen u ovom slučaju)
    instruction |= (0 & 0xFFF);  // Postavljamo 12-bitni offset u polje DDDD

    // 2. Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    // 3. Dodavanje instrukcije u trenutnu sekciju koda
    addDataToSection(binaryInstruction);
}

void handleCsrwrInstruction(const char* gpr, const char* csr) {
    // Dobijamo indeks registrara za CSR i GPR
    int csrReg = getCSRIndex(std::string(csr));  // Funkcija za pretvaranje oznake CSR u indeks
    int gprReg = getRegisterIndex(std::string(gpr));  // Funkcija za pretvaranje oznake GPR u indeks

    // Provera da li su registri validni
    if (csrReg == -1 || gprReg == -1) {
        std::cerr << "Error: Invalid CSR or GPR register." << std::endl;
        return;
    }

    // Formiranje instrukcije
    uint32_t instruction = 0;
    // Polje OC za učitavanje (1001)
    instruction |= (0x9 << 28);  // Postavljamo prvih 4 bita na 1001 za operaciju pisanja u CSR
    // Polje MMMM, 0b0100 za csrwr gde csr[A]<=gpr[B]
    instruction |= (0x4 << 24);  // MMMM = 0b0100 za csrwr
    // Polje AAAA: registar CSR
    instruction |= (csrReg << 20);  // Postavljamo registar A (CSR) u polje AAAA
    // Polje BBBB: registar GPR koji sadrži podatak
    instruction |= (gprReg << 16);  // Postavljamo registar B (GPR) u polje BBBB
    // Polje DDDD DDDD DDDD DDDD: offset 0 (nije potreban, može biti 0)
    instruction |= (0x0);  // Offset postavljamo na 0

    // Konvertovanje instrukcije u niz bajtova (little-endian raspored)
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    // Dodavanje instrukcije u trenutnu sekciju koda
    addDataToSection(binaryInstruction);
}

void handleLoadInstruction(const char* operand, const char* gpr) {

    uint32_t lc = sectionHeaders[currentSectionIndex].sh_size;  // Trenutna pozicija u sekciji

    // 1. Dekodiramo registar gpr
    int regA = getRegisterIndex(std::string(gpr));

    // 2. Dekodiramo operand i formiramo binarnu instrukciju na osnovu operanda
    uint32_t instruction = 0;
    // Postavljamo OC na 1001 (ld instrukcija)
    instruction |= (0x9 << 28); // OC = 1001

    // Ovde smeštamo registar regA u odgovarajuće polje instrukcije
    instruction |= (regA << 20); // Smeštamo regG u polje od 20-23 bita

    if (operand[0] == '$') {
        // Literal ili simbol, direktna vrednost
        if (isdigit(operand[1])) {
              // $<literal> - literalna vrednost
            instruction |= (0x1 << 24); // MMMM = 0b0001
            // Pretvorimo literal u broj
            std::string literalStr(operand);
            int literal = std::stoi(literalStr.substr(1)); // Konverzija stringa u broj
            // Pronađemo ili dodamo literal u bazen
            int literalIndex = findOrAddLiteral(LITERAL_VALUE, -1, literal);
            // Proverimo da li indeks staje u 12 bita
            if (literalIndex >= (1 << 12)) {
                std::cerr << "Error: Literal index exceeds 12 bits limit!" << std::endl;
                return;
            }
            // Upisujemo indeks iz literalPool u instrukciju, koristeći poslednjih 12 bita (DDDD DDDD DDDD)
            instruction |= (literalIndex & 0xFFF); // 12-bitno polje D

            // Dodaj relokacioni zapis kako bi u njemu pamtili indeks literala u bazenu literala
            addRelocationEntryForLiteral(currentSectionIndex, lc, literalIndex, R_ABSINST12);
        } else {
            // $<simbol> - vrednost simbola
            instruction |= (0x1 << 24); // MMMM = 0b0001 (Označava da je operand simbol)

            // Uzimamo ime simbola, podrazumevajući da je 'operand' string koji sadrži $<simbol>
            const char* symbolName = operand + 1; // Skidamo '$'

            // Pokušaj da pronađeš simbol u tabeli simbola
            uint32_t symbolIndex = findSymbolIndex(symbolName);

            if (symbolIndex != UINT32_MAX) {
                // Simbol je pronađen
                ELF32_Sym& symbol = symbolTable[symbolIndex];

                // Proveri da li je simbol definisan
                if (symbol.st_shndx != SHN_UNDEF) {
                    // Simbol je definisan, koristimo njegovu vrednost
                    
                    uint32_t symbolValue = symbol.st_value;
                    uint32_t literalIndex;
                    if (ELF32_ST_BIND(symbol.st_info) == STB_LOCAL){
                        literalIndex = findOrAddLiteral(LOCAL_SYMBOL, symbol.st_shndx, symbolValue);
                    } else {
                        literalIndex = findOrAddLiteral(GLOBAL_SYMBOL, symbolIndex, symbolValue);
                }
                    instruction |= (literalIndex & 0xFFF); // Pretpostavljamo da indeks staje u 12 bita

                    // Dodaj relokacioni zapis
                    addRelocationEntry(currentSectionIndex, lc, symbol, literalIndex, R_ABSINST12);

                } else {
                    // Simbol postoji, ali je nedefinisan - dodaj forward referencu
                    addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);

                    // Napomena: U instrukciji možeš staviti privremenu vrednost (npr. 0 ili poseban marker)
                    instruction |= 0x0; // Privremeno, jer će se vrednost postaviti nakon backpatching-a
                }
            } else {
                // Simbol nije pronađen - dodaj simbol kao nedefinisani u tabelu simbola
                addSymbol(symbolName, false, STT_NOTYPE);
                symbolIndex = findSymbolIndex(symbolName); // Osveži simbol nakon dodavanja
                // Dodaj forward referencu na ovu instrukciju
                addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
                instruction |= 0x0; // Privremeno, jer će se vrednost postaviti nakon backpatching-a
            }
        }
        
    } else if (operand[0] == '%') {
        // %<reg> - vrednost iz registra
        std::string regBStr(operand + 1); // Ukloni '%' sa početka stringa
        int regB = getRegisterIndex(regBStr); // Dobij indeks registra

        if (regB == -1) {
            // Ako je registar nevalidan, obavesti korisnika i izađi
            std::cerr << "Unknown register in operand: " << operand << std::endl;
            return;
        }

        instruction |= (0x0 << 24); // MMMM = 0b0000 za ld iz registra CSR
        instruction |= (regB << 16); // Postavi regB
    } else if (operand[0] == '[') {
        // Memorijska adresa, npr. [%<reg>] ili [%<reg> + <literal>]
        std::string memOperand(operand + 1, strlen(operand) - 2); // Skidamo zagrade
        size_t plusPos = memOperand.find(" + ");
        if (plusPos != std::string::npos) {
            // [%<reg> + <literal>] ili [%<reg> + <simbol>]
            std::string regBStr = memOperand.substr(1, plusPos - 1);
            std::string offsetStr = memOperand.substr(plusPos + 3);

            int regB = getRegisterIndex(regBStr); // Koristi funkciju za indeks registra
            if (regB == -1) {
                // Ako registar nije validan, prekidamo funkciju
                return;
            }

            if (isdigit(offsetStr[0])) {
                // Literalan offset
                int literal = std::stoi(offsetStr);
                
                // Upisujemo literal u instrukciju, koristeći poslednjih 12 bita (DDDD DDDD DDDD)
                instruction |= (0x2 << 24); // MMMM = 0b0010
                instruction |= (regB << 16); // Postavi regB
                instruction |= (literal & 0xFFF); // Postavi offset
            } else {
                // Simbolički offset
                unsigned int symbolIndex = findSymbolIndex(offsetStr.c_str());
                uint32_t lc = sectionHeaders[currentSectionIndex].sh_size;  // Trenutna pozicija u sekciji
                if (symbolIndex != UINT32_MAX) {
                    // Simbol je pronađen
                    ELF32_Sym& symbol = symbolTable[symbolIndex];

                    // Proveri da li je simbol definisan
                    if (symbol.st_shndx != SHN_UNDEF) {
                        // Simbol je definisan, koristimo njegovu vrednost
                        uint32_t symbolValue = symbol.st_value;
                        instruction |= (0x2 << 24); // MMMM = 0b0010
                        instruction |= (regB << 16); // Postavi regB
                        //instruction |= (symbolValue & 0xFFF); // Postavi offset
                        uint32_t literalIndex;
                        if (ELF32_ST_BIND(symbol.st_info) == STB_LOCAL){
                            literalIndex = findOrAddLiteral(LOCAL_SYMBOL, symbol.st_shndx, symbolValue);
                        } else {
                            literalIndex = findOrAddLiteral(GLOBAL_SYMBOL, symbolIndex, symbolValue);
                        }
                        instruction |= (literalIndex & 0xFFF);

                        // Dodaj relokacioni zapis
                        addRelocationEntry(currentSectionIndex, lc, symbol, literalIndex, R_ABSINST12);
                    } else {
                        // Simbol postoji, ali je nedefinisan - dodaj forward referencu
                        addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);

                        // Napomena: U instrukciji možeš staviti privremenu vrednost (npr. 0 ili poseban marker)
                        instruction |= (0x2 << 24); // MMMM = 0b0010
                        instruction |= (regB << 16); // Postavi regB
                        instruction |= 0x0; // Privremeno, jer će se vrednost postaviti nakon backpatching-a
                    }
                } else {
                    // Simbol nije pronađen - dodaj simbol kao nedefinisani u tabelu simbola
                    addSymbol(offsetStr.c_str(), false, STT_NOTYPE);
                    symbolIndex = findSymbolIndex(offsetStr.c_str()); // Osveži simbol nakon dodavanja
                    // Dodaj forward referencu na ovu instrukciju
                    addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);

                   // Napomena: U instrukciji možeš staviti privremenu vrednost (npr. 0 ili poseban marker)
                    instruction |= (0x2 << 24); // MMMM = 0b0010
                    instruction |= (regB << 16); // Postavi regB
                    instruction |= 0x0; // Privremeno, jer će se vrednost postaviti nakon backpatching-a
                }
            }
        } else {
            // [%<reg>] - vrednost iz memorije na adresi <reg>
            std::string regBStr = memOperand.substr(1);
            int regB = getRegisterIndex(regBStr); // Koristi funkciju za indeks registra
            if (regB == -1) {
                // Ako registar nije validan, prekidamo funkciju
                return;
            }
            instruction |= (0x3 << 24); // MMMM = 0b0011
            instruction |= (regB << 16); // Postavi regB
        }
    } else {
        // Literal ili simbol bez $ - vrednost iz memorije
        if (isdigit(operand[0])) {
            // <literal> - adresa u memoriji
            int literal = std::stoi(operand);
            int literalIndex = findOrAddLiteral(LITERAL_VALUE, -1, literal);
            // Proverimo da li indeks staje u 12 bita
            if (literalIndex >= (1 << 12)) {
                std::cerr << "Error: Literal index exceeds 12 bits limit!" << std::endl;
                 return;
            }
            // Upisujemo indeks iz literalPool u instrukciju, koristeći poslednjih 12 bita (DDDD DDDD DDDD)
            instruction |= (0x2 << 24); // MMMM = 0b0010
            instruction |= (literalIndex & 0xFFF); // Postavi offset
            uint32_t lc = sectionHeaders[currentSectionIndex].sh_size;  // Trenutna pozicija u sekciji
            // Dodaj relokacioni zapis kako bi u njemu pamtili indeks literala u bazenu literala
            addRelocationEntryForLiteral(currentSectionIndex, lc, literalIndex, R_ABSINST12);
        } else {
            // <simbol> - vrednost iz memorije
            unsigned int symbolIndex = findSymbolIndex(operand);
            uint32_t lc = sectionHeaders[currentSectionIndex].sh_size;  // Trenutna pozicija u sekciji
            if (symbolIndex != UINT32_MAX) {
                ELF32_Sym& symbol = symbolTable[symbolIndex];

                if (symbol.st_shndx != SHN_UNDEF) {
                    uint32_t symbolValue = symbol.st_value;
                    uint32_t literalIndex;
                    if (ELF32_ST_BIND(symbol.st_info) == STB_LOCAL){
                        literalIndex = findOrAddLiteral(LOCAL_SYMBOL, symbol.st_shndx, symbolValue);
                    } else {
                        literalIndex = findOrAddLiteral(GLOBAL_SYMBOL, symbolIndex, symbolValue);
                    }
                    instruction |= (0x2 << 24);
                    instruction |= (literalIndex & 0xFFF);

                    // Dodaj relokacioni zapis
                    addRelocationEntry(currentSectionIndex, lc, symbol, literalIndex, R_ABSINST12);
                } else {
                    addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
                    instruction |= (0x2 << 24);
                    instruction |= 0x0;
                }
            } else {
                addSymbol(operand, false, STT_NOTYPE);
                symbolIndex = findSymbolIndex(operand); // Osveži simbol nakon dodavanja
                addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
                instruction |= (0x2 << 24);
                instruction |= 0x0;
            }
        }
    }

    // 3. Konvertujemo instrukciju u niz bajtova i dodajemo u sekciju
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

    addDataToSection(binaryInstruction); 
}


void handleStoreInstruction(const char* gpr, const char* operand) {

    uint32_t lc = sectionHeaders[currentSectionIndex].sh_size;  // Trenutna pozicija u sekciji

    // 1. Dekodiramo registar gpr
    int regG = getRegisterIndex(std::string(gpr));
    if (regG == -1) {
        std::cerr << "Unknown register in gpr: " << gpr << std::endl;
        return;
    }

    // 2. Dekodiramo operand i formiramo binarnu instrukciju na osnovu operanda
    uint32_t instruction = 0;
    // Postavljamo OC na 1010 (st instrukcija)
    instruction |= (0x8 << 28); // OC = 1000

    // Ovde smeštamo registar regG u odgovarajuće polje instrukcije
    instruction |= (regG << 12); // Smeštamo regG u polje od 12-16 bita

    if (operand[0] == '$') {
        // Literal ili simbol, direktna vrednost
        if (isdigit(operand[1])) {
            // $<literal> - literalna vrednost
            instruction |= (0x1 << 24); // MMMM = 0b0000
            std::string literalStr(operand);
            int literal = std::stoi(literalStr.substr(1)); // Konverzija stringa u broj
            int literalIndex = findOrAddLiteral(LITERAL_VALUE, -1, literal);
            if (literalIndex >= (1 << 12)) {
                std::cerr << "Error: Literal index exceeds 12 bits limit!" << std::endl;
                return;
            }
            instruction |= (literalIndex & 0xFFF); // 12-bitno polje D
            // Dodaj relokacioni zapis kako bi u njemu pamtili indeks literala u bazenu literala
            addRelocationEntryForLiteral(currentSectionIndex, lc, literalIndex, R_ABSINST12);
            
        } else {
            // $<simbol> - vrednost simbola
            const char* symbolName = operand + 1; // Skidamo '$'

            uint32_t symbolIndex = findSymbolIndex(symbolName);
            
            if (symbolIndex != UINT32_MAX) {
                ELF32_Sym& symbol = symbolTable[symbolIndex];

                if (symbol.st_shndx != SHN_UNDEF) {
                    uint32_t symbolValue = symbol.st_value;
                    uint32_t literalIndex;
                    if (ELF32_ST_BIND(symbol.st_info) == STB_LOCAL){
                        literalIndex = findOrAddLiteral(LOCAL_SYMBOL, symbol.st_shndx, symbolValue);
                    } else {
                        literalIndex = findOrAddLiteral(GLOBAL_SYMBOL, symbolIndex, symbolValue);
                    }
                    instruction |= (literalIndex & 0xFFF);

                    // Dodaj relokacioni zapis
                    addRelocationEntry(currentSectionIndex, lc, symbol, literalIndex, R_ABSINST12);
                } else {
                    addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
                    instruction |= 0x0;
                }
            } else {
                addSymbol(symbolName, false, STT_NOTYPE);
                symbolIndex = findSymbolIndex(symbolName); // Osveži simbol nakon dodavanja
                addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
                instruction |= 0x0;
            }
        }
    } else if (operand[0] == '%') {
        // %<reg> - vrednost iz registra
        std::string regOpStr(operand + 1);
        int regOp = getRegisterIndex(regOpStr);

        if (regOp == -1) {
            std::cerr << "Unknown register in operand: " << operand << std::endl;
            return;
        }

        instruction |= (0x0 << 24); // MMMM = 0b0000 za st iz registra
        instruction |= (regOp << 16); // Postavi regOp
    } else if (operand[0] == '[') {
        // Memorijska adresa, npr. [%<reg>] ili [%<reg> + <literal>]
        std::string memOperand(operand + 1, strlen(operand) - 2);
        size_t plusPos = memOperand.find(" + ");
        if (plusPos != std::string::npos) {
            std::string regOpStr = memOperand.substr(1, plusPos - 1);
            std::string offsetStr = memOperand.substr(plusPos + 3);

            int regOp = getRegisterIndex(regOpStr);
            if (regOp == -1) {
                return;
            }

            if (isdigit(offsetStr[0])) {
                int literal = std::stoi(offsetStr);
                instruction |= (0x2 << 24); // MMMM = 0b0010
                instruction |= (regOp << 16);
                instruction |= (literal & 0xFFF);
            } else {
                unsigned int symbolIndex = findSymbolIndex(offsetStr.c_str());
                uint32_t lc = sectionHeaders[currentSectionIndex].sh_size;  // Trenutna pozicija u sekciji
                if (symbolIndex != UINT32_MAX) {
                    ELF32_Sym& symbol = symbolTable[symbolIndex];

                    if (symbol.st_shndx != SHN_UNDEF) {
                        uint32_t symbolValue = symbol.st_value;
                        instruction |= (0x2 << 24); // MMMM = 0b0010
                        instruction |= (regOp << 16);
                        instruction |= (symbolValue & 0xFFF);

                        // Dodaj relokacioni zapis
                        addRelocationEntry(currentSectionIndex, lc, symbol, symbolIndex, R_ABSINST12);
                    } else {
                        addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
                        instruction |= (0x2 << 24); // MMMM = 0b0010
                        instruction |= (regOp << 16);
                        instruction |= 0x0;
                    }
                } else {
                    addSymbol(offsetStr.c_str(), false, STT_NOTYPE);
                    symbolIndex = findSymbolIndex(offsetStr.c_str()); // Osveži simbol nakon dodavanja
                    addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
                    instruction |= (0x2 << 24);
                    instruction |= (regOp << 16);
                    instruction |= 0x0;
                }
            }
        } else {
            std::string regOpStr = memOperand.substr(1);
            int regOp = getRegisterIndex(regOpStr);
            if (regOp == -1) {
                return;
            }
            instruction |= (0x2 << 24); // MMMM = 0b0010
            instruction |= (regOp << 16);
        }
    } else {
        // Literal ili simbol bez $ - vrednost iz memorije
        if (isdigit(operand[0])) {
            int literal = std::stoi(operand);
            int literalIndex = findOrAddLiteral(LITERAL_VALUE, -1, literal);
            if (literalIndex >= (1 << 12)) {
                std::cerr << "Error: Literal index exceeds 12 bits limit!" << std::endl;
                return;
            }
            instruction |= (0x2 << 24); // MMMM = 0b0010
            instruction |= (literalIndex & 0xFFF);
            uint32_t lc = sectionHeaders[currentSectionIndex].sh_size;  // Trenutna pozicija u sekciji
            // Dodaj relokacioni zapis kako bi u njemu pamtili indeks literala u bazenu literala
            addRelocationEntryForLiteral(currentSectionIndex, lc, literalIndex, R_ABSINST12);
        } else {
            unsigned int symbolIndex = findSymbolIndex(operand);
            
            if (symbolIndex != UINT32_MAX) {
                ELF32_Sym& symbol = symbolTable[symbolIndex];

                if (symbol.st_shndx != SHN_UNDEF) {
                    uint32_t symbolValue = symbol.st_value;
                    uint32_t literalIndex;
                    if (ELF32_ST_BIND(symbol.st_info) == STB_LOCAL){
                        literalIndex = findOrAddLiteral(LOCAL_SYMBOL, symbol.st_shndx, symbolValue);
                    } else {
                        literalIndex = findOrAddLiteral(GLOBAL_SYMBOL, symbolIndex, symbolValue);
                    }
                    instruction |= (0x2 << 24);
                    instruction |= (literalIndex & 0xFFF);

                    // Dodaj relokacioni zapis
                    addRelocationEntry(currentSectionIndex, lc, symbol, literalIndex, R_ABSINST12);
                } else {
                    addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
                    instruction |= (0x2 << 24);
                    instruction |= 0x0;
                }
            } else {
                addSymbol(operand, false, STT_NOTYPE);
                symbolIndex = findSymbolIndex(operand); // Osveži simbol nakon dodavanja
                addForwardReference(symbolIndex, sectionHeaders[currentSectionIndex].sh_size, 0, 12, R_ABSINST12);
                instruction |= (0x2 << 24);
                instruction |= 0x0;
            }
        }
    }
    // 3. Konvertujemo instrukciju u niz bajtova i dodajemo u sekciju
    std::vector<unsigned char> binaryInstruction(4);
    binaryInstruction[0] = instruction & 0xFF;
    binaryInstruction[1] = (instruction >> 8) & 0xFF;
    binaryInstruction[2] = (instruction >> 16) & 0xFF;
    binaryInstruction[3] = (instruction >> 24) & 0xFF;

     addDataToSection(binaryInstruction);
    
}

void backpatching() {
    // Prolazak kroz sve forward reference
    for (const ForwardReference& ref : forwardReferenceTable) {
        ELF32_Sym& symbol = symbolTable[ref.unresolvedSymbolIndex];

        // Proveravamo da li je simbol lokalni
        if (ELF32_ST_BIND(symbol.st_info) == STB_LOCAL && symbol.st_shndx == SHN_UNDEF) {
            // Lokalni simbol je nedefinisan - prijavljujemo grešku
            std::cerr << "Error: Lokalni simbol '" << stringNameTable[symbol.st_name]
                      << "' je nedefinisan!" << std::endl;
            continue;  // Preskačemo ovu forward reference
        }

        // Pronađi sekciju koristeći headerIndex iz sectionContent
        SectionContent* section = nullptr;
        for (SectionContent& sec : sections) {
            if (sec.headerIndex == ref.sectionIndex) {
                section = &sec;
                break;
            }
        }
        if (!section) {
            std::cerr << "Error: Section not found for index " << ref.sectionIndex << std::endl;
            continue;
        }

        // Pronađi mesto gde treba popraviti instrukciju
        uint32_t instructionAddress = ref.address;

        // Proveravamo da li je instrukcija validna
        if (instructionAddress + sizeof(uint32_t) > section->content.size()) {
            std::cerr << "Error: Invalid instruction address in section" << std::endl;
            continue;
        }

        // Čitanje trenutne instrukcije
        uint32_t currentInstruction = *(uint32_t*)(&section->content[instructionAddress]);

        // Proveravamo da li je simbol definisan (st_shndx != SHN_UNDEF)
        if (symbol.st_shndx != SHN_UNDEF) {
            // Simbol je definisan, koristimo st_value da popravimo instrukciju
            uint32_t symbolValue = symbol.st_value;

            // Očistimo bitove koji se odnose na simbol (bitLength određuje koliko bita menjamo)
            uint32_t mask = (1 << ref.bitLength) - 1;
            currentInstruction &= ~(mask << ref.instructionOffset);

            // Postavimo novu vrednost simbola unutar instrukcije
            currentInstruction |= (symbolValue & mask) << ref.instructionOffset;

            // Ažuriraj instrukciju u sekciji
            *(uint32_t*)(&section->content[instructionAddress]) = currentInstruction;

            // Dodaj relokacioni zapis prema tipu relokacije
            if (ref.relocationType == R_ABSWORD32) {
                addRelocationEntry(ref.sectionIndex, ref.address, symbol, ref.unresolvedSymbolIndex, ref.relocationType);
            } else if (ref.relocationType == R_ABSINST12) {
                uint32_t literalIndex;
                if (ELF32_ST_BIND(symbol.st_info) == STB_LOCAL){
                    literalIndex = findOrAddLiteral(LOCAL_SYMBOL, symbol.st_shndx, symbol.st_value);
                } else {
                    literalIndex = findOrAddLiteral(GLOBAL_SYMBOL, ref.unresolvedSymbolIndex, symbol.st_value);
                }
                addRelocationEntry(ref.sectionIndex, ref.address, symbol, literalIndex, ref.relocationType);
            } else {
                std::cerr << "Error: Unsupported relocation type " << ref.relocationType << "." << std::endl;
            }

        } else {
            // Simbol nije definisan, potrebno je kreirati relokacioni zapis
            if (ref.relocationType == R_ABSWORD32) {
                addRelocationEntry(ref.sectionIndex, ref.address, symbol, ref.unresolvedSymbolIndex, ref.relocationType);
            } else if (ref.relocationType == R_ABSINST12) {
                uint32_t literalIndex;
                if (ELF32_ST_BIND(symbol.st_info) == STB_LOCAL){
                    literalIndex = findOrAddLiteral(LOCAL_SYMBOL, symbol.st_shndx, symbol.st_value);
                } else {
                    literalIndex = findOrAddLiteral(GLOBAL_SYMBOL, ref.unresolvedSymbolIndex, symbol.st_value);
                }
                addRelocationEntry(ref.sectionIndex, ref.address, symbol, literalIndex, ref.relocationType);
            } else {
                std::cerr << "Error: Unsupported relocation type " << ref.relocationType << "." << std::endl;
            }
        }
    }
}



void generateObjectFile(const char* fileName) {
    std::ofstream objFile(fileName, std::ios::binary);
    
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
    elfHeader.e_shnum = sectionHeaders.size();  // Broj sekcija
    elfHeader.e_shstrndx = 0;      // Stavicemo na nulti indeks stringName sekciju pa ce ujedno i odgovarati da je nula

    objFile.write(reinterpret_cast<const char*>(&elfHeader), sizeof(ELF32Header));

    // 2. Ažuriraj offset i veličinu za sekciju tabele stringova 
    for (ELF32_Shdr& sh : sectionHeaders) {
        if (sh.sh_type == SHT_STRTAB) { 
             sh.sh_offset = currentOffset; // Offset gde počinje tabela stringova
             sh.sh_size = 0; // Inicijalizacija veličine

            // Izračunavanje veličine tabele stringova
            for (const std::string& name : stringNameTable) {
                    sh.sh_size += name.size() + 1; // Dodavanje veličine stringa + null terminator
            }

            currentOffset += sh.sh_size; // Ažuriraj trenutni offset
            break;
         }
    }

    // Upis imena sekcija i simbola (String Name Table)
    for (const std::string& name : stringNameTable) {
        objFile.write(name.c_str(), name.size() + 1); // +1 zbog null terminatora
    }

    // 3. Ažuriranje offseta sekcija i upis sadržaja sekcija
    for (SectionContent& section : sections) {
        // Postavi offset sekcije u odgovarajuće zaglavlje
        sectionHeaders[section.headerIndex].sh_offset = currentOffset;
        sectionHeaders[section.headerIndex].sh_size = section.content.size(); // Ažuriraj veličinu sekcije

        // Upis sadržaja sekcije
        objFile.write(reinterpret_cast<const char*>(section.content.data()), section.content.size());

        // Ažuriraj trenutni offset
        currentOffset += section.content.size();
    }

    // 4. Upis tabele simbola (Symbol Table)
    if (!symbolTable.empty()) {
        for (ELF32_Sym& symbol : symbolTable) {
            // Upis simbola
            objFile.write(reinterpret_cast<const char*>(&symbol), sizeof(ELF32_Sym));
        }

        // Ažuriraj offset za sekciju tabele simbola
        for (ELF32_Shdr& sh : sectionHeaders) {
            if (sh.sh_type == SHT_SYMTAB) {
                sh.sh_offset = currentOffset;
                sh.sh_size = symbolTable.size() * sizeof(ELF32_Sym); // Ažuriraj veličinu tabele simbola
                currentOffset += sh.sh_size;
                break;
            }
        }
    }

    // 5. Upis relokacionih sekcija
    for (SectionRelocation& relocationSection : relocationSections) {
        // Postavi offset za relokacionu sekciju
        sectionHeaders[relocationSection.headerIndex].sh_offset = currentOffset;
        sectionHeaders[relocationSection.headerIndex].sh_size = relocationSection.relocationTable.size() * sizeof(ELF32_Rela);

        for (const ELF32_Rela& relocationEntry : relocationSection.relocationTable) {
            objFile.write(reinterpret_cast<const char*>(&relocationEntry), sizeof(ELF32_Rela));
        }

        // Ažuriraj trenutni offset
        currentOffset += relocationSection.relocationTable.size() * sizeof(ELF32_Rela);
    }

   // 6. Upis bazena literala (Literal Pool)
    if (!literalPool.empty()) {
        // Pronalaženje sekcije za bazen literala
        for (ELF32_Shdr& sh : sectionHeaders) {
            if (sh.sh_type == SHT_LITERALPOOL) {
                sh.sh_offset = currentOffset;
                sh.sh_size = literalPool.size() * sizeof(LiteralEntry);
                break;
            }
        }

        // Upis literala (LiteralEntry struktura)
        for (const LiteralEntry& entry : literalPool) {
            objFile.write(reinterpret_cast<const char*>(&entry), sizeof(LiteralEntry));
        }

        // Ažuriraj trenutni offset
        currentOffset += literalPool.size() * sizeof(LiteralEntry);
    }


    // 7. Upis zaglavlja sekcija (Section Headers)
    // Ažuriramo ELF header pre nego što pišemo zaglavlja sekcija
    elfHeader.e_shoff = currentOffset;
    objFile.seekp(0, std::ios::beg);
    objFile.write(reinterpret_cast<const char*>(&elfHeader), sizeof(ELF32Header));

    // Sada upisujemo zaglavlja sekcija
    objFile.seekp(currentOffset, std::ios::beg);
    for (const ELF32_Shdr& sectionHeader : sectionHeaders) {
        objFile.write(reinterpret_cast<const char*>(&sectionHeader), sizeof(ELF32_Shdr));
    }

    // Zatvaranje fajla
    objFile.close();

    std::cout << "Object file generated successfully: " << fileName << std::endl;
}


// Funkcija za generisanje debug fajla
void generateDebugFile(const char* fileName) {
    std::ofstream debugFile(fileName);

    if (!debugFile.is_open()) {
        std::cerr << "Error: Could not open debug file for writing." << std::endl;
        return;
    }

       

    debugFile << "SECTION HEADER TABLE:\n";
    debugFile << std::left << std::setw(6) << "Num" 
          << std::setw(18) << "Addr" 
          << std::setw(12) << "Size" 
          << std::setw(15) << "Type" 
          << std::setw(20) << "Flags" 
          << std::setw(25) << "Name" 
          << "\n";

    for (size_t i = 0; i < sectionHeaders.size(); ++i) {
        const ELF32_Shdr& section = sectionHeaders[i];
        
        // Pronađi ime sekcije iz string tabele
        std::string sectionName = (section.sh_name < stringNameTable.size()) ? stringNameTable[section.sh_name] : "unknown";
        
        // Tip sekcije kao string
        std::string sectionType;
        switch (section.sh_type) {
            case SHT_PROGBITS: sectionType = "PROGBITS"; break;
            case SHT_SYMTAB: sectionType = "SYMTAB"; break;
            case SHT_STRTAB: sectionType = "STRTAB"; break;
            case SHT_LITERALPOOL: sectionType = "LITERALPOOL"; break;
            case SHT_RELA: sectionType = "RELA"; break;
            default: sectionType = "UNKNOWN"; break;
        }
        
        // Zastavice sekcije kao string
        std::string sectionFlags;
        if (section.sh_flags & SHF_WRITE) sectionFlags += "WRITE ";
        if (section.sh_flags & SHF_ALLOC) sectionFlags += "ALLOC ";
        if (section.sh_flags & SHF_EXECINSTR) sectionFlags += "EXEC ";
        if (sectionFlags.empty()) sectionFlags = "NONE";

        // Ručno formatiranje za adresu i veličinu
        std::stringstream addrStream, sizeStream;
        addrStream << std::hex << std::setw(8) << std::setfill('0') << section.sh_addr;
        sizeStream << std::hex << std::setw(8) << std::setfill('0') << section.sh_size;

        debugFile << std::left << std::setw(6) << i
                << std::setw(18) << addrStream.str()  // Ručno formatiran heksadecimalni prikaz
                << std::setw(12) << sizeStream.str()  // Ručno formatiran heksadecimalni prikaz
                << std::setw(15) << sectionType 
                << std::setw(20) << sectionFlags 
                << std::setw(25) << sectionName 
                << "\n";
        }



            // Ispis simbola
            debugFile << "SYMBOL TABLE:\n";
            debugFile << std::left << std::setw(20) << "Symbol" 
                    << std::setw(20) << "Value" 
                    << std::setw(25) << "Section" 
                    << "Binding" << "\n";

            for (const ELF32_Sym& symbol : symbolTable) {
                // Ručno formatiraj vrednost u heksadecimalni string
                std::stringstream valueStream;
                valueStream << std::hex << std::setw(8) << std::setfill('0') << symbol.st_value;

                // Ispis simbola
                debugFile << std::left << std::setw(20) << stringNameTable[symbol.st_name] 
                        << std::setw(20) << valueStream.str()  // Ručno formatiran heksadecimalni prikaz
                        << std::setfill(' ') 
                        << std::setw(25) 
                        << ((symbol.st_shndx == 0) ? "undefined" : stringNameTable[sectionHeaders[symbol.st_shndx].sh_name]) 
                        << std::setw(8) 
                        << ((ELF32_ST_BIND(symbol.st_info) & 0x1) ? "Global" : "Local") 
                        << "\n";
            }


            // Ispis relokacionih tabela
            debugFile << "\nRELOCATION TABLES:\n";

            for (const SectionRelocation& relocSection : relocationSections) {
                // Ispis naziva relokacione sekcije
                debugFile << "Section: " 
                        << stringNameTable[sectionHeaders[relocSection.headerIndex].sh_name] << "\n";

                // Ispis naslova za Offset, Info i Addend
                debugFile << std::left << std::setw(10) << "Offset" 
                        << std::setw(10) << "Info" 
                        << "Addend" << "\n";

                // Ispis svake relokacione stavke
                for (const ELF32_Rela& rel : relocSection.relocationTable) {
                    // Ručno formatiraj vrednosti sa preciznim razdvajanjem
                    std::stringstream offsetStream, infoStream, addendStream;

                    // Formatiranje offset-a u heksadecimalni prikaz
                    offsetStream << std::hex << std::setw(8) << std::setfill('0') << rel.r_offset;

                    // Formatiranje info u heksadecimalni prikaz
                    infoStream << std::hex << std::setw(8) << std::setfill('0') << rel.r_info;

                    // Formatiranje addend-a kao decimalni broj
                    addendStream << std::dec << rel.r_addend;

                    // Ispis svake relokacione stavke
                    debugFile << std::setw(10) << offsetStream.str()  // Ručno formatiran offset
                            << std::setw(10) << infoStream.str()    // Ručno formatiran info
                            << std::setw(8)  << addendStream.str()  // Addend kao decimalni
                            << "\n";
                }

                // Ispis praznog reda nakon svake sekcije
                debugFile << "\n";
            }


            // Ispis heksadecimalnih podataka
            debugFile << "\nHEX DATA:\n";
            for (const SectionContent& section : sections) {
                debugFile << "Section: " << stringNameTable[sectionHeaders[section.headerIndex].sh_name] << "\n";
                for (size_t i = 0; i < section.content.size(); ++i) {
                    // Svakih 16 bajtova ispisuje novu adresu
                    if (i % 16 == 0) 
                        debugFile << std::hex << std::setw(8) << std::setfill('0') << (i * 1) << "\t";  // Uvećavamo adresu za 16 bajtova po redu (u bajtovima)
                    
                    // Ispis svakog bajta u heksadecimalnom formatu
                    debugFile << std::hex << std::setw(2) << std::setfill('0') << (int)section.content[i] << " ";
                    
                    // Ako smo ispisali 16 bajtova, prelazimo u novi red
                    if (i % 16 == 15) 
                        debugFile << "\n";
                }
                
                // Ako nismo završili na granici od 16 bajtova, dodaj novi red
                if (section.content.size() % 16 != 0)
                    debugFile << "\n";
            }




        debugFile.close();

        std::cout << "Debug file generated successfully: " << fileName << std::endl;
}

void cleanupResources() {
    // Oslobađanje memorije za imena simbola
    for (auto& name : stringNameTable) {
        free(&name[0]);  // Oslobađanje memorije za ime simbola
    }
    stringNameTable.clear();  // Očisti tabelu imena simbola

    // Oslobađanje memorije za inicijalizatore
    forwardReferenceTable.clear();  // Očisti tabelu referenci unapred

    // Ako su u upotrebi druge dinamicki alocirane strukture (npr. stringovi u stringNameTable)
    // oslobodite ih
    // Oslobađanje memorije za sekcije
    for (auto& section : sections) {
        section.content.clear();  // Očisti sadržaj sekcije
        // Nema potrebe za oslobađanjem dodatnih resursa za SectionContent
        // jer ne koristimo dinamički alociranu memoriju unutar njega
    }
    sections.clear();  // Očisti sve sekcije
    // Oslobađanje memorije za simboličke i relokacione tabele
    relocationSections.clear();  // Očisti tabelu relokacionih zapisa
}


void handleEndDirective() {
    backpatching();
    //Generisanje objektnog fajla na osnovu sekcija i simbola
    // Oslobađanje resursa
    //cleanupResources();
}

// Glavna funkcija
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: assembler <input_files...> [-o <output_prefix>] [-d <debug_file>]" << std::endl;
        return -1;
    }

    const char* outputPrefix = "a";
    const char* debugFile = nullptr;

    std::vector<std::string> inputFiles;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            outputPrefix = argv[++i];
        } else if (std::strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            debugFile = argv[++i];
        } else {
            inputFiles.emplace_back(argv[i]);
        }
    }

    for (const auto& inputFile : inputFiles) {
        std::cout << "Processing file: " << inputFile << std::endl;

        FILE* myfile = fopen(inputFile.c_str(), "r");
        if (!myfile) {
            std::cerr << "Failed to open file: " << inputFile << std::endl;
            return -1;
        }
        yyin = myfile;

        initialize();

        while (!feof(yyin)) {
            int parseResult = yyparse();
            if (parseResult != 0) {
                std::cerr << "Parsing failed for file: " << inputFile << std::endl;
                fclose(myfile);
                return -1;
            }
        }

        fclose(myfile);

        std::string outputFile = std::string(outputPrefix) + ".o";
        generateObjectFile(outputFile.c_str());
 
        if (debugFile) {
            generateDebugFile(debugFile);
        }
    }

    return 0;
}