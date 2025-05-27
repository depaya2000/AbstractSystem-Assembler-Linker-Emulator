#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include <vector>
#include <cstring> // za std::strlen, std::strcpy, std::strdup, std::free
#include <cstdint>
#include <string> 

using namespace std;

#define EI_NIDENT 16
#define SHF_ALLOC 0x2  // Sekcija mora biti učitana u memoriju
#define SHF_WRITE 0x1  // Sekcija je zapisiva (moguća je promena podataka u sekciji)
#define SHF_EXECINSTR 0x4 // sekcija koja sadrzi izvrsni kod
#define ELF32_ST_INFO(bind, type) (((bind) << 4) + ((type) & 0xf))
#define ELF32_ST_BIND(info) ((info) >> 4)
#define ELF32_ST_TYPE(info) ((info) & 0xF)
#define STB_LOCAL  0  // Simbol je lokalni
#define STB_GLOBAL  1  // Simbol je globalni
#define STB_EXTERN  4  // Simbol je externi
#define STT_SECTION  3  // Simbol je sekcija
#define STT_NOTYPE  0  // Simbol nema definisan tip
#define SHN_UNDEF    0x0  // Nepoznata sekcija (neodređeni simbol)
#define SHT_STRTAB 3 //string tabela
#define SHT_RELA 4 //relokaciona tabela
#define SHT_PROGBITS 1 // program data
#define SHT_SYMTAB 2 //tabela simbola
#define SHT_LITERALPOOL 5 //bazen literala
#define SHT_NOBITS 8 //.bssss sekcija
#define R_X86_64_32S 11 //tip relokacije 
#define R_X86_64_PC32 2 //tip relokacije
#define R_386_32 1 //tip relokacije
#define R_ABSWORD32 3            // Apsolutna relokacija 32-bitna
#define R_ABSINST12 6            // Apsolutna relokacija 12-bitna
#define R_ABSWORD32_GLOBAL 4     // Apsolutna relokacija 32-bitna za globalne simbole
#define R_ABSWORD32_LOCAL 5      // Apsolutna relokacija 32-bitna za lokalne simbole (sekcije)
#define R_ABSINST12_GLOBAL 7     // Apsolutna relokacija 12-bitna za globalne simbole
#define R_ABSINST12_LOCAL 8      // Apsolutna relokacija 12-bitna za lokalne simbole (sekcije)
#define LOCAL_SYMBOL 0 //Lokalni simbol u bazenu literala
#define GLOBAL_SYMBOL 1 //Globalni simbol u bazenu literala
#define LITERAL_VALUE 2 //Literalna vrednost u bazenu literala

extern int line_num;

// ELF Header
struct ELF32Header {
    unsigned char e_ident[16]; // Identification bytes (ELF magija, klasa, endianost itd.)
    uint16_t e_type;                  // Tip objektnog fajla (npr. izvršni, relocatable)
    uint16_t e_machine;               // Arhitektura (npr. EM_386 za x86)
    uint32_t e_version;               // Verzija ELF formata
    uint32_t e_entry;                 // Entry point virtuelna adresa (početna adresa instrukcija)
    uint32_t e_shoff;                 // Offset do section header tabele
    uint16_t e_ehsize;                // Veličina ELF zaglavlja
    uint16_t e_shentsize;             // Veličina jednog ulaza u section header tabeli
    uint16_t e_shnum;                 // Broj ulaza u section header tabeli
    uint16_t e_shstrndx;              // Indeks u section header tabeli koji sadrži imena sekcija
};

struct ELF32_Shdr {
    uint32_t sh_name;       // Pomeraj u string tabeli sa imenima sekcija
    uint32_t sh_type;       // Tip sekcije (SHT_PROGBITS, SHT_SYMTAB, SHT_RELA itd.)
    uint32_t sh_flags;      // Zastavice sekcije (npr. SHF_WRITE, SHF_ALLOC itd.)
    uint32_t sh_addr;       // Virtuelna adresa sekcije u memoriji
    uint32_t sh_offset;     // Offset sekcije u fajlu, popunjavamo posle kada smestamo sekcije u objektni fajl
    uint32_t sh_size;       // Veličina sekcije u bajtovima = location counter
    uint32_t sh_link;       // Indeks relokacione sekcije za tu sekciju
    uint32_t sh_info;       // Indeks ka bazenu literala
    uint32_t sh_addralign;  // Poravnanje sekcije
    uint32_t sh_entsize;    // Veličina jednog unosa (ako sekcija sadrži tabelu, npr. za simboličku tabelu)
    
};

struct SectionContent {
    uint32_t headerIndex;         // Indeks zaglavlja u vector<ELF32_Shdr>
    vector<uint8_t> content; // Sadržaj sekcije (podaci/instrukcije)
};

struct ELF32_Sym {
    uint32_t st_name;       // Pomeraj u string tabeli imena simbola
    uint32_t st_value;      // Vrednost simbola = location counter
    uint32_t st_size;       // Veličina simbola
    unsigned char st_info;  // Tip i vezivanje simbola
    unsigned char st_other; // Vidljivost simbola
    uint16_t st_shndx;      // Indeks sekcije kojoj simbol pripada
};

struct ELF32_Rela {
    uint32_t r_offset;      // Offset u sekciji gde je potrebna relokacija
    uint32_t r_info;        // Indeks simbola tj. indeks iz bazena literala i tip relokacije
    int32_t r_addend;       // Dodatna vrednost za relokaciju
};

// Struktura za sekciju koja sadrži relokacione zapise
struct SectionRelocation {
    uint32_t headerIndex;              // Indeks zaglavlja u vector<ELF32_Shdr>
    vector<ELF32_Rela> relocationTable; // Tabela relokacija za ovu sekciju
};

struct ForwardReference {
    uint32_t unresolvedSymbolIndex; // Indeks simbola u symbolTable koji nije definisan
    uint32_t sectionIndex;          // Indeks sekcije u kojoj se nalazi referenca
    uint32_t address;               // Adresa unutar sekcije gde se simbol koristi
    uint32_t instructionOffset;     // Pomeraj unutar instrukcije gde se nalazi simbol (ako je primenljivo)
    uint32_t bitLength;             // Broj bitova koje treba popraviti (npr. 4 bita, 12 bitova)
    uint32_t relocationType;        // Tip relokacije (npr. R_X86_64_32 ili R_X86_64_PC32)
    int32_t addend;                 // Vrednost addenda (ako postoji)
};

struct LiteralEntry {
    uint8_t type;     // Tip unosa
    uint32_t value;         // Ako je literal: sama vrednost; ako je globalni ili lokalni simbol: ofset u sekciji
    int32_t index;    // Indeks simbola u tabeli simbola (negativan za literal), indeks sekcije za lokalni simbol
};

struct Initializer {
    int literal;      // Za literalnu vrednost
    char* symbol;     // Za simboličke vrednosti

    // Konstruktor za literal
    Initializer(int lit) : literal(lit), symbol(nullptr) {}

    // Konstruktor za simbol
    Initializer(const char* sym) : literal(0), symbol(strdup(sym)) {}

    Initializer() = default;

   
};

void initialize();
uint32_t findSectionIndex(const char* sectionName);
uint32_t findSymbolIndex(const char* symbolName);
uint32_t addStringName(const char* name);
int findOrAddLiteral(uint8_t type, int32_t index, uint32_t value);
void handleSectionDirective(const char* sectionName);
void handleGlobalDirective(const char* symbol);
void handleExternDirective(const char* symbol);
void handleWordDirectiveWithLiteral(int literal);
void handleWordDirectiveWithSymbol(const char* symbolName);
void handleSkipDirective(int numBytes);
void addDataToSection(const std::vector<unsigned char>& data);
void addSymbol(const char* symbolName, bool isDefined, uint8_t type);
void addRelocationEntry(uint32_t currentSection, uint32_t lc, const ELF32_Sym& symbol, uint32_t index, uint32_t relocationType);
void addRelocationEntryForLiteral(uint32_t currentSection, uint32_t lc, uint32_t index, uint32_t relocationType);
void addForwardReference(uint32_t symbolIndex, uint32_t currentAddress, uint32_t currentInstructionOffset,  uint32_t bitLength, uint32_t relocationType);
void handleHaltInstruction();
void handleIntInstruction();
void handleIretInstruction();
void handleCallInstruction(const char* operand);
void handleRetInstruction();
int32_t getRegisterIndex(const std::string& reg);
int getCSRIndex(const std::string& csrName);
void handleJumpInstruction(const char* operand);
void handleBranchInstruction(const char* gpr1, const char* gpr2, const char* operand, const std::string& instructionType);
void addArithmeticInstruction(const std::string& operation, const char* gprS, const char* gprD);
void addLogicalInstruction(const std::string& operation, const char* gprS, const char* gprD);
void addShiftInstruction(const std::string& operation, const char* gprS, const char* gprD);
void handleXchgInstruction(const char* gprS, const char* gprD);
void handlePushInstruction(const char* gpr);
void handlePopInstruction(const char* gpr);
void handleCsrrdInstruction(const char* csr, const char* gpr);
void handleCsrwrInstruction(const char* gpr, const char* csr);
void handleLoadInstruction(const char* operand, const char* gpr);
void handleStoreInstruction(const char* gpr, const char* operand);
void backpatching();
void cleanupResources();
void generateObjectFile(const char* fileName);
void handleEndDirective();

void yyerror(const char *s);

#endif // ASSEMBLER_H