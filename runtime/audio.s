# ================================================================================================
# File: audio.s — Funções de áudio da runtime tarm. Convenção: System V AMD64 ABI.
# Inteiro: %rdi = valor | Float: %xmm0 = valor | String: %rdi = ponteiro, %rsi = tamanho | Bool: %rdi = 0/1
# Reference: docs/runtime.md
# ================================================================================================

.set TAXA,      44100               # amostras por segundo | padrão de CD anos 80
.set BLOCO,     2048                # amostras por bloco | buffer de 4096 bytes

.bss
buffer:
    .skip BLOCO * 2                 # 2 bytes por amostra

.text
.global tarm_emit_note

# ------------------------------------------------------------------------------------------------
# tarm_emit_note — Gera uma onda quadrada PCM (16 bits, mono, TAXA Hz) e escreve as amostras em
# stdout (fd 1), em blocos de BLOCO amostras. Pensada para ser redirecionada a um player de áudio
# cru (ex.: `./programa | aplay -f S16_LE -r 44100 -c 1`). freq = 0 gera silêncio (pausa).
# Reference: docs/runtime.md#audio-emit_note
#
# In:       %rdi = frequência (Hz), %rsi = amplitude, %rdx = duração (ms)
# Out:      nenhum (void); side-effect: amostras escritas em stdout
# Clobbers: %rax, %rdi, %rsi, %rdx, %rcx (%rbx/%r12-%r15/%rbp salvos/restaurados via pilha)
#
# Registradores de trabalho: meio = %r14 (meio-período) | restantes = %r15 (amostras a gerar) |
# fase = %rbx | onda = %r12 (amostra atual, com sinal) | bloco atual = %r13
# ------------------------------------------------------------------------------------------------
tarm_emit_note:
    pushq %rbp
    movq  %rsp, %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq  $8, %rsp                  # alinha %rsp em 16 para o call

    movq %rdi, %r12                 # freq (temporario)
    movq %rsi, %r13                 # amp  (temporario)

    movq  %rdx, %rax                # restantes = TAXA * ms / 1000
    imulq $TAXA, %rax
    xorq  %rdx, %rdx
    movq  $1000, %rcx
    divq  %rcx
    movq  %rax, %r15

    testq %r12, %r12                # freq = 0 -> pausa: silencio, sem divisao
    jnz  .Lcalc_meio
    movq $-1, %r14                  # meio infinito: a onda nunca inverte
    xorq %r13, %r13                 # amplitude 0
    jmp  .Lmeio_ok

.Lcalc_meio:
    movq $TAXA, %rax                # meio = TAXA / (2 * freq)
    xorq %rdx, %rdx
    leaq (%r12,%r12), %rcx          # denominador = freq * 2
    divq %rcx
    movq %rax, %r14
    testq %r14, %r14
    jnz  .Lmeio_ok
    movq $1, %r14                   # freq alta demais: satura em 1

.Lmeio_ok:
    xorq %rbx, %rbx                 # fase = 0        | nao reiniciar por bloco
    movq %r13, %r12                 # onda = amp      | nao reiniciar por bloco

    call .full_fill_note

    addq  $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    movq %rbp, %rsp
    popq %rbp
    ret

# .full_fill_note (local) — Gera as %r15 amostras restantes, em blocos de BLOCO: preenche o buffer
# com a onda quadrada (inverte o sinal a cada meio-período %r14) e escreve cada bloco em stdout.
.full_fill_note:
    pushq %rbp
    movq  %rsp, %rbp

.Lbloco:
    testq %r15, %r15                # acabaram as amostras?
    jz    .Lfim

    movq  $BLOCO, %r13              # r13 = min(BLOCO, restantes)
    cmpq  %r15, %r13
    jbe   .Lquantas
    movq  %r15, %r13
.Lquantas:
    subq  %r13, %r15

    leaq  buffer(%rip), %rdi        # rdi = onde escrever a proxima amostra
    movq  %r13, %rcx                # rcx = quantas amostras faltam NESTE bloco

    .preencher:
    movw    %r12w, (%rdi)           # grava a amostra (2 bytes)
    addq    $2, %rdi                # avanca o ponteiro
    incq    %rbx                    # avancou uma amostra no meio-periodo

    cmpq    %r14, %rbx
    jb      .proxima                # ainda dentro da metade: nao inverte
    xorq    %rbx, %rbx              # Se fase >= MEIO: zera a fase
    negw    %r12w                   # e inverte o sinal da onda

    .proxima:
    decq    %rcx
    jnz     .preencher

    movq    $1, %rax                # syscall 1 = write
    movq    $1, %rdi                # fd 1 = stdout
    leaq    buffer(%rip), %rsi      # endereco do buffer
    leaq    (%r13,%r13), %rdx       # quantos bytes = amostras * 2
    syscall

    jmp   .Lbloco                   # proximo bloco

.Lfim:
    xorl %eax, %eax
    movq %rbp, %rsp
    popq %rbp
    ret

.section .note.GNU-stack, "", @progbits
