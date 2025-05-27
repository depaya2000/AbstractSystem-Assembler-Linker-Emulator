.section .text
start:
    mov %r1, %r2
    add %r3, %r4
    push %r1
    jmp end
end:
    st %r1, term_out
    iret
.end
