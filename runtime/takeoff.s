# ================================================================================================
# File: takeoff.s — Ponto de entrada (_start) da runtime tarm. Convenção: System V AMD64 ABI.
# Reference: docs/runtime.md
#
# Substitui o crt0/libc: o binário é montado por `as` e linkado por `ld` (sem gcc/libc), então
# `_start` é a primeira instrução executada. Ele monta os argumentos que a ABI espera em %rdi/%rsi/
# %rdx (argc/argv/envp) a partir da pilha inicial do kernel, alinha %rsp, chama `main` e, ao voltar,
# encerra o processo via syscall exit (#60) usando o valor de retorno de `main` (%eax) como código
# de saída.
# ================================================================================================

.text
.global _start

_start:
    xorq    %rbp, %rbp              # ABI: marca o fim da cadeia de frames
    movq    (%rsp), %rdi            # argc
    leaq    8(%rsp), %rsi           # argv
    leaq    16(%rsp,%rdi,8), %rdx   # envp (pula argv e o NULL terminador)
    andq    $-16, %rsp              # alinha a pilha em 16 bytes antes do call

    call    main

.landing:
    movl    %eax, %edi             
    movq    $60, %rax           
    syscall

    hlt                             # inalcancavel; rede de seguranca

.section .note.GNU-stack, "", @progbits

