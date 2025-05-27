.section .text
.global main, helper
.extern input_data, output_data

main:
    ld input_data, %r1   // Učitaj ulazne podatke u registar r1
    add %r2, %r1         // Saberi vrednost iz r2 sa r1
    st %r1, output_data  // Sačuvaj rezultat u izlazne podatke
    call helper          // Pozovi potprogram helper
    halt                 // Zaustavi izvršavanje

helper:
    push %r3             // Sačuvaj r3 na stek
    not %r3              // Inverzija vrednosti r3
    pop %r3              // Vrati r3 sa steka
    ret                  // Povratak iz potprograma
.end

.section .data
.global input_data, output_data
input_data: .word 0x1234ABCD
output_data: .skip 4

.section .rodata
msg: .word 0xDEADBEEF
