# ================================================================================================
# File: alloc.s — Alocação de memória da runtime tarm. Convenção: System V AMD64 ABI.
# Reference: docs/runtime.md#heap-brk-linear
# ================================================================================================

.text
.global tarm_mmap_alloc
.global tarm_brk_alloc
.global tarm_mmap_free
.global tarm_brk_free

# ------------------------------------------------------------------------------------------------
# tarm_mmap_alloc — Aloca memória via mmap(2) (syscall #9), mapeamento anônimo independente.
# Reference: docs/runtime.md#tabela-de-syscalls-linux-x86-64-usadas
#
# In:       %rdi = tamanho em bytes
# Out:      %rax = ponteiro para o bloco alocado, ou 0 em caso de falha
# Clobbers: %rax, %rdi, %rsi, %rdx, %r10, %r8, %r9
# ------------------------------------------------------------------------------------------------
tarm_mmap_alloc:
    pushq   %rbp
    movq    %rsp, %rbp

    # Organiza os 6 argumentos esperados pela System Call de mmap.
    movq    %rdi, %rsi  # Argumento 2 (Lenght): Tamanho do bloco de memória a ser alocado.
    xorq    %rdi, %rdi  # Argumento 1 (Addr): Endereço à ser usado.
    movq    $3, %rdx    # Argumento 3 (Permissão de memória): PROT_READ (1) | PROT_WRITE (2) = 3 (Permissão de leitura e escrita).
    movq    $34, %r10   # Argumento 4 (Flags): Opções de mapeamento.
    movq    $-1, %r8    # Argumento 5 (Fd): File descriptor. Como usamos MAP_ANONYMOUS, não há arquivo envolvido, então passa-se -1.
    xorq    %r9, %r9    # Argumento 6 (Offset): Deslocamento no arquivo. Como não há arquivo, é 0.

    # Chamando syscall
    movq    $9, %rax    # Número identificado de mmap na Linux System Call Table (x86 64)
    syscall

    # Verifica resultado da execução
    cmpq    $-1, %rax   # Compara retorno em %rax com -1 (MAP_FAILED).
    jne     .mmap_done  # Com sucesso (não é igual a -1), pula para a finalização.
    xorq    %rax, %rax  # Se falhar, padroniza o retorno de erro da função para ponteiro nulo (NULL), zerando o %rax.

    .mmap_done:
        movq    %rbp, %rsp
        popq    %rbp
        ret

# ------------------------------------------------------------------------------------------------
# tarm_brk_alloc — Aloca memória movendo o program break adiante (syscall brk, #12). Heap linear.
# Reference: docs/runtime.md#heap-brk-linear
#
# In:       %rdi = tamanho em bytes
# Out:      %rax = ponteiro para o bloco alocado (break antigo), ou 0 em caso de falha
# Clobbers: %rax, %rdi, %rdx (%r12/%rbp salvos/restaurados via pilha)
# ------------------------------------------------------------------------------------------------

tarm_brk_alloc:
    pushq   %rbp
    movq    %rsp, %rbp

    pushq   %r12
    movq    %rdi, %r12

    xorq    %rdi, %rdi
    movq    $12, %rax
    syscall

    movq    %rax, %rdx
    addq    %r12, %rax
    movq    %rax, %rdi
    movq    $12, %rax
    syscall

    cmpq    %rdi, %rax
    jne     .brk_failed
    movq    %rdx, %rax
    jmp     .brk_done
    .brk_failed:
        xorq    %rax, %rax
    .brk_done:
        popq    %r12
        movq    %rbp, %rsp
        popq    %rbp
        ret

# ------------------------------------------------------------------------------------------------
# tarm_brk_free (local) — Libera memória movendo o program break de volta para %rdi (syscall brk, #12).
# Reference: docs/runtime.md#heap-brk-linear
# @warning Libera também tudo alocado acima de %rdi — heap linear, sem contabilidade de blocos.
#
# In:       %rdi = endereço do break desejado (tipicamente devolvido por brk_alloc)
# Out:      %rax = break antigo, ou 0 em caso de falha
# Clobbers: %rax, %rdi, %rdx (%r12/%rbp salvos/restaurados via pilha)
# ------------------------------------------------------------------------------------------------
tarm_brk_free:
    pushq   %rbp
    movq    %rsp, %rbp

    pushq   %r12
    movq    %rdi, %r12

    xorq    %rdi, %rdi
    movq    $12, %rax
    syscall

    movq    %rax, %rdx
    movq    %r12, %rax
    movq    %rax, %rdi
    movq    $12, %rax
    syscall

    cmpq    %rdi, %rax
    jne     .brk_free_failed
    movq    %rdx, %rax
    jmp     .brk_free_done
    .brk_free_failed:
        xorq    %rax, %rax
    .brk_free_done:
        popq    %r12
        movq    %rbp, %rsp
        popq    %rbp
        ret
# ------------------------------------------------------------------------------------------------
# tarm_mmap_free (local) — Libera mapeamento via munmap(2) (syscall #11). Argumentos já vêm prontos do
# chamador (%rdi/%rsi); região independente (não afeta outros mapeamentos, ao contrário de brk_free).
# Reference: docs/runtime.md#tabela-de-syscalls-linux-x86-64-usadas
#
# In:       %rdi = ponteiro, %rsi = tamanho em bytes
# Out:      %rax = 0 (sucesso ou falha com retorno exato -1); outro valor é repassado sem alteração
# Clobbers: %rax
# ------------------------------------------------------------------------------------------------
tarm_mmap_free:
    pushq   %rbp
    movq    %rsp, %rbp

    movq    $11, %rax
    syscall

    # Verifica resultado da execução
    cmpq    $-1, %rax   # Compara retorno em %rax com -1.
    jne     .free_done  # Com sucesso (não é igual a -1), pula para a finalização.
    xorq    %rax, %rax  # Se falhar, padroniza o retorno de erro da função para ponteiro nulo (NULL), zerando o %rax.

    .free_done:
        movq    %rbp, %rsp
        popq    %rbp
        ret

