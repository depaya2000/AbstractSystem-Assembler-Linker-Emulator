# Abstract System – Assembler, Linker, and Emulator

This project implements a toolchain for an abstract computer system, including:

- An **assembler** that translates source assembly code into machine code.
- A **linker** that combines multiple object files into a final executable, independently of the target architecture.
- An **emulator** that executes machine code for the defined abstract system.

## 📌 Project Objective

The goal is to develop a translation toolchain and a virtual machine (emulator) for a fictional architecture. The abstract system specification (ISA) is provided separately and defines instruction formats, memory layout, and system behavior.

## ⚙️ Components

### 1. Assembler
- Converts textual assembly code into object files.
- Supports symbol resolution, directives, and instruction encoding.
- Generates relocation tables and symbol tables.

### 2. Linker
- Merges multiple object files into a single executable.
- Resolves external symbols.
- Produces a binary ready for the emulator.

### 3. Emulator
- Loads and runs the linked executable.
- Simulates the instruction set and system behavior.
- Provides debugging and state inspection tools (registers, memory, flags).

## 💻 Usage

All tools are CLI-based and compatible with **Linux** environments. 

##Author
Student of Electrical Engineering at the University of Belgrade, Department of Computer Engineering and Informatics.

##License
This project is for educational purposes.
