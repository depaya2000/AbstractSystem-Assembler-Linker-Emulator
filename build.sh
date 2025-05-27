#!/bin/bash

# Pokreni Bison za generisanje parsera
bison -d -o parser.cpp assembler.y
if [ $? -ne 0 ]; then
    echo "Bison failed"
    exit 1
fi

# Pokreni Flex za generisanje leksičkog analizatora
flex -o lex.yy.cpp assembler.l
if [ $? -ne 0 ]; then
    echo "Flex failed"
    exit 1
fi

# Kompajliraj parser.cpp
g++ -Wall -std=c++17 -g -c parser.cpp -o parser.o
if [ $? -ne 0 ]; then
    echo "Failed to compile parser.cpp"
    exit 1
fi

# Kompajliraj lex.yy.cpp
g++ -Wall -std=c++17 -g -c lex.yy.cpp -o lex.yy.o
if [ $? -ne 0 ]; then
    echo "Failed to compile lex.yy.cpp"
    exit 1
fi

# Kompajliraj assembler.cpp
g++ -Wall -std=c++17 -g -c assembler.cpp -o assembler.o
if [ $? -ne 0 ]; then
    echo "Failed to compile assembler.cpp"
    exit 1
fi

# Linkuj sve zajedno u izvršnu datoteku 'assembler'
g++ -Wall -std=c++17 -g -o assembler assembler.o parser.o lex.yy.o
if [ $? -ne 0 ]; then
    echo "Failed to link files"
    exit 1
fi

# Generiši .o fajlove za svaki .s fajl u trenutnom direktorijumu
for file in *.s; do
    ./assembler "$file" -o "${file%.s}" -d "${file%.s}.debug.txt"
    if [ $? -ne 0 ]; then
        echo "Failed to assemble $file"
        exit 1
    fi
done

#Kompajliraj linker.cpp
g++ -Wall -std=c++17 -g -o linker linker.cpp
if [ $? -ne 0 ]; then
    echo "Failed to compile linker.cpp"
    exit 1
fi

#Pokreni linker sa odgovarajucim argumentima
./linker -o program.hex -hex test1.o test2.o
if [ $? -ne 0 ]; then
    echo "Failed to run linker"
    exit 1
fi

#Kompajliraj emulator.cpp
g++ -Wall -std=c++17 -g -o emulator emulator.cpp
if [ $? -ne 0 ]; then
    echo "Failed to compile emulator.cpp"
    exit 1
fi

echo "Build and assembly successful!"