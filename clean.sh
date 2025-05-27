#!/bin/bash

# Lista fajlova i ekstenzija koje treba obrisati
FILES_TO_CLEAN=(
    "assembler"
    "linker"
    "emulator"
    "*.o"
    "*.debug.txt"
    "*.hex"
    "parser.cpp"
    "parser.hpp"
    "lex.yy.cpp"
)

echo "Cleaning generated files..."

# Iteracija kroz sve navedene fajlove i brisanje
for pattern in "${FILES_TO_CLEAN[@]}"; do
    find . -name "$pattern" -exec rm -f {} +
done

echo "Cleanup complete!"