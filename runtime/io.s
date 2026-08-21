# ================================================================================================
# File: io.s — Funções de I/O da runtime tarm. Convenção: System V AMD64 ABI.
# Inteiro: %rdi = valor | Float: %xmm0 = valor | String: %rdi = ponteiro, %rsi = tamanho | Bool: %rdi = 0/1
# Reference: docs/runtime.md
# ================================================================================================
.include "object.inc"

.text

.global tarm_read_buf

.global tarm_print_int
.global tarm_print_float
.global tarm_print_str
.global tarm_print_bool
.global tarm_print_char

# ------------------------------------------------------------------------------------------------
# tarm_read_buf — Recebe uma String (header de objeto), extrai o nome do arquivo de OBJ_DATA, abre
# o arquivo, lê até 32 bytes para um buffer temporário na stack, fecha o descritor e devolve um
# objeto (ver object.s) com os bytes lidos copiados para o payload. É a implementação da função
# nativa `read_buf`, que produz um Buffer.
# Reference: docs/runtime.md#convencoes-especificas-de-io
#
# In:       %rdi = ponteiro para o header do objeto String (o nome do arquivo está em OBJ_DATA)
# Out:      %rax = ponteiro para o header do objeto (Buffer) com o conteúdo lido
# Clobbers: %rax, %rdi, %rsi, %rdx, %rcx
# ------------------------------------------------------------------------------------------------
tarm_read_buf:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $80, %rsp
    movq    OBJ_DATA(%rdi), %rdi
    call    tarm_open_file
    movq    %rax, -8(%rbp) 

    movq    -8(%rbp), %rdi
    leaq    -64(%rbp), %rsi   
    movq    $32, %rdx
    movq    $0, %rax
    syscall
    movq    %rax, -16(%rbp)      

    movq    $3, %rax
    movq    -8(%rbp), %rdi
    syscall

    movq    -16(%rbp), %rdi        # Passa os bytes lidos para criar o objeto
    call    tarm_obj_new
    movq    %rax, -24(%rbp)        # -24(%rbp) = Ponteiro do HEADER do Objeto
    
    movq    -24(%rbp), %rax
    movq    OBJ_DATA(%rax), %rdi   # Destino: Endereço do Payload no Heap
    leaq    -64(%rbp), %rsi        # Origem: Buffer temporário na Stack
    movq    -16(%rbp), %rcx        # Contagem: bytes lidos
    rep movsb

    movq    -24(%rbp), %rax        # Retorna o ponteiro do HEADER em %rax

    addq    $80, %rsp
    movq    %rbp, %rsp
    popq    %rbp
    ret

# ------------------------------------------------------------------------------------------------
# tarm_open_file (local) — Abre um arquivo via open(2) (syscall #2) com flags O_RDWR e devolve o
# descritor. O nome do arquivo já vem em %rdi do chamador (tarm_read_buf), que não é alterado aqui.
#
# In:       %rdi = ponteiro para o nome do arquivo (String terminada em NUL)
# Out:      %rax = descritor de arquivo (ou negativo em erro, repassado sem tratamento)
# Clobbers: %rax, %rsi
# ------------------------------------------------------------------------------------------------
tarm_open_file:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $16, %rsp

    movq $2, %rax
    movq $2, %rsi
    syscall
    
    addq $16, %rsp
    movq    %rbp, %rsp
    pop %rbp
    ret

# ------------------------------------------------------------------------------------------------
# _format_int (local) — Formata um int64 COM SINAL em ASCII base 10, do fim do buffer para o início.
#
# Guarda o sinal em %r8, formata o valor absoluto com _format_uint e, se era negativo, recua um byte
# e escreve o '-' na frente dos dígitos. Escrever da direita para a esquerda é o que torna isso
# barato: o sinal entra por último, sem deslocar nada.
#
# %r8 sobrevive à chamada porque _format_uint não o toca (ver a lista de clobbers dela).
#
# INT64_MIN cai de pé por acidente feliz: `negq` sobre ele devolve ele mesmo, e _format_uint, que lê
# sem sinal, imprime 9223372036854775808 — exatamente o módulo que se quer.
#
# In:       %rax = valor com sinal, %rdi = fim (exclusivo) do buffer
# Out:      %rsi = ponteiro pro primeiro dígito (ou pro '-'), %rcx = quantidade de bytes
# Clobbers: %rax, %rdx, %rsi, %rcx, %r8 (%rbx salvo/restaurado por _format_uint)
# ------------------------------------------------------------------------------------------------
_format_int:
    xorl    %r8d, %r8d
    testq   %rax, %rax
    jns     .L_int_abs
    movl    $1, %r8d
    negq    %rax
.L_int_abs:
    call    _format_uint        # %rsi = 1º dígito, %rcx = nº de dígitos

    testl   %r8d, %r8d
    jz      .L_int_done
    decq    %rsi
    movb    $45, (%rsi)
    incq    %rcx
.L_int_done:
    ret

# ------------------------------------------------------------------------------------------------
# _format_uint (local) — Formata um uint64 em ASCII base 10, do fim do buffer para o início.
#
# In:       %rax = valor sem sinal, %rdi = fim (exclusivo) do buffer
# Out:      %rsi = ponteiro pro primeiro dígito, %rcx = quantidade de dígitos
# Clobbers: %rax, %rdx, %rsi, %rcx (%rbx salvo/restaurado via pilha)
# ------------------------------------------------------------------------------------------------
_format_uint:
    pushq   %rbx
    movq    %rdi, %rsi
    xorq    %rcx, %rcx
.L_fmt_loop:
    xorl    %edx, %edx
    movq    $10, %rbx
    divq    %rbx
    addl    $48, %edx
    decq    %rsi
    movb    %dl, (%rsi)
    incq    %rcx
    cmpq    $0, %rax
    jne     .L_fmt_loop
    popq    %rbx
    ret

# ------------------------------------------------------------------------------------------------
# _format_uint_padded4 (local) — Igual a _format_uint, mas sempre 4 dígitos (zero à esquerda).
# Usada por print_float para a parte fracionária (já escalada por 10000).
#
# In:       %rax = valor (0..9999), %rdi = fim (exclusivo) do buffer
# Out:      %rsi = ponteiro pro início dos 4 dígitos, %rcx = 4 (fixo)
# Clobbers: %rax, %rdx, %rsi, %rcx (%rbx salvo/restaurado via pilha)
# ------------------------------------------------------------------------------------------------
_format_uint_padded4:
    pushq   %rbx
    movq    %rdi, %rsi
    movq    $4, %rcx
.L_fmtpad_loop:
    xorl    %edx, %edx
    movl    $10, %ebx
    divl    %ebx
    addl    $48, %edx
    decq    %rsi
    movb    %dl, (%rsi)
    loop    .L_fmtpad_loop
    movq    $4, %rcx
    popq    %rbx
    ret

# ------------------------------------------------------------------------------------------------
# tarm_print_int — Imprime um inteiro não-negativo (write, #1). Não insere '\n'.
# Reference: docs/runtime.md#convencoes-especificas-de-io
#
# In:       %rdi = valor (Int/Int64, não-negativo)
# Out:      nenhum (void); side-effect em stdout
# Clobbers: %rax, %rdi, %rsi, %rdx, %rcx
# ------------------------------------------------------------------------------------------------
tarm_print_int:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $32, %rsp

    movq    %rdi, %rax
    leaq    (%rbp), %rdi          # fim do buffer local (32 bytes reservados abaixo)
    call    _format_int

    movq    $1, %rax
    movq    $1, %rdi
    movq    %rcx, %rdx
    syscall

    movq    %rbp, %rsp
    popq    %rbp
    ret

# ------------------------------------------------------------------------------------------------
# tarm_print_float — Imprime um float não-negativo com 4 casas decimais fixas (write, #1 x3). Não
# insere '\n'.
# Reference: docs/runtime.md#convencoes-especificas-de-io
#
# In:       %xmm0 = valor (Float, não-negativo)
# Out:      nenhum (void); side-effect em stdout
# Clobbers: %rax, %rdi, %rsi, %rdx, %rcx, %xmm0, %xmm1, %xmm2
# ------------------------------------------------------------------------------------------------
tarm_print_float:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $32, %rsp

    movss   %xmm0, %xmm2              # xmm2 = cópia do valor original

    # ---- parte inteira ----
    cvttss2si %xmm0, %eax
    leaq    (%rbp), %rdi
    call    _format_uint

    movq    $1, %rax
    movq    $1, %rdi
    movq    %rcx, %rdx
    syscall

    # ---- ponto decimal ----
    movq    $1, %rax
    movq    $1, %rdi
    leaq    .Lio_dot(%rip), %rsi
    movq    $1, %rdx
    syscall

    # ---- parte fracionária (escalada por 10000, 4 casas fixas) ----
    cvttss2si %xmm2, %eax
    cvtsi2ss  %eax, %xmm1
    subss   %xmm1, %xmm2
    movss   .Lio_scale(%rip), %xmm1
    mulss   %xmm1, %xmm2
    cvttss2si %xmm2, %eax

    leaq    (%rbp), %rdi
    call    _format_uint_padded4

    movq    $1, %rax
    movq    $1, %rdi
    movq    %rcx, %rdx
    syscall

    movq    %rbp, %rsp
    popq    %rbp
    ret

# ------------------------------------------------------------------------------------------------
# tarm_print_str — Imprime uma String (write, #1); lê o ponteiro e o tamanho do header do objeto
# (OBJ_DATA/OBJ_LEN — ver object.s), não depende de terminador nulo. Não insere '\n'.
# Reference: docs/runtime.md#convencoes-especificas-de-io
#
# In:       %rdi = ponteiro para o header do objeto (String)
# Out:      nenhum (void); side-effect em stdout
# Clobbers: %rax, %rdi, %rsi, %rdx
# ------------------------------------------------------------------------------------------------
tarm_print_str:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %rbx

    movq    OBJ_LEN(%rdi), %rdx    # arg3 do syscall: tamanho
    movq    OBJ_DATA(%rdi), %rsi   # arg2 do syscall: ponteiro do texto
    movq    $1, %rax
    movq    $1, %rdi
    syscall

    popq    %rbx

    movq    %rbp, %rsp
    popq    %rbp
    ret

# ------------------------------------------------------------------------------------------------
# tarm_print_bool — Imprime "true" ou "false" (write, #1) conforme %rdi. Não insere '\n'.
# Reference: docs/runtime.md#convencoes-especificas-de-io
#
# In:       %rdi = 0 (false) ou 1 (true)
# Out:      nenhum (void); side-effect em stdout
# Clobbers: %rax, %rdi, %rsi, %rdx
# ------------------------------------------------------------------------------------------------
tarm_print_bool:
    pushq   %rbp
    movq    %rsp, %rbp

    cmpq    $0, %rdi
    je      .Lio_print_false

    .Lio_print_true:
        movq    $1, %rax
        movq    $1, %rdi
        leaq    .Lio_true(%rip), %rsi
        movq    $4, %rdx
        syscall
        jmp .Lio_done

    .Lio_print_false:
        movq    $1, %rax
        movq    $1, %rdi
        leaq    .Lio_false(%rip), %rsi
        movq    $5, %rdx
        syscall

    .Lio_done:
    movq    %rbp, %rsp
    popq    %rbp
    ret


# ------------------------------------------------------------------------------------------------
# tarm_print_char — Imprime um único caractere (write, #1), via slot temporário na stack. Não insere '\n'.
# Reference: docs/runtime.md#convencoes-especificas-de-io
#
# In:       %dil = caractere (Char, byte baixo de %rdi)
# Out:      nenhum (void); side-effect em stdout
# Clobbers: %rax, %rdi, %rsi, %rdx
# ------------------------------------------------------------------------------------------------

tarm_print_char:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $16, %rsp

    movb    %dil, -1(%rbp)      # guarda o byte num slot temporário na stack
    movq    $1, %rax
    movq    $1, %rdi
    leaq    -1(%rbp), %rsi      # ponteiro pro byte
    movq    $1, %rdx            # tamanho: 1 byte
    syscall

    movq    %rbp, %rsp
    popq    %rbp
    ret

.section .rodata
    .align 4
.Lio_newline:
    .string "\n"
.Lio_dot:
    .string "."
.Lio_scale:
    .float 10000.0
.Lio_true:
    .ascii "true"
.Lio_false:
    .ascii "false"

