#ifndef LINKER_H
#define LINKER_H

#include <vector>
#include <string>
#include <map>
#include <cstdint>

using namespace std;

// Struktura koja čuva opcije komandne linije
struct CommandLineOptions {
    string outputFileName; // Naziv izlazne datoteke
    map<string, uint32_t> sectionAddresses; // Adrese sekcija
    bool generateHex = false; // Da li treba generisati hex zapis
    vector<string> inputFiles; // Lista ulaznih datoteka
};

int parseCommandLine(int argc, char* argv[], CommandLineOptions& options);// Parsiranje komandne linije
void loadObjectFile(const std::string& filename); // Učitavanje objekata
void mergeStringNameTables();
void updateSymbolTable();
void updateSectionLinks();
void mergeSectionHeaders(const CommandLineOptions& options);
void mergeSections();
void mergeAndMapRelocations();
void mergeLiteralPools();
void resolveRelocations(); // Razrešavanje relokacija
void generateHexOutput(const std::string& filename); // Generisanje HEX izlaza

// Generisanje relocabilnog izlaza
//void generateRelocatableOutput(const std::string& filename);

#endif // LINKER_H