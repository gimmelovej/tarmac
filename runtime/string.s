# ================================================================================================
# File: string.s — Funções de string da runtime tarm. Convenção: System V AMD64 ABI.
# Reference: docs/runtime.md
# ================================================================================================
.include "object.inc"

.text
.global tarm_atoi
.global tarm_strlen

# ------------------------------------------------------------------------------------------------
# tarm_atoi — Converte string decimal (terminada em NUL, só dígitos '0'-'9') para inteiro. Para no
# primeiro byte inválido/terminador. Multiplica por 10 via "rax*5, depois *2" (evita imul).
#
# In:       %rdi = ponteiro para o header do objeto String (o texto vem de OBJ_DATA)
# Out:      %rax = valor convertido (Int64); 0 se nenhum dígito válido
# Clobbers: %rax, %rcx, %rdi
# ------------------------------------------------------------------------------------------------
tarm_atoi:
    pushq   %rbp
    movq    %rsp, %rbp
    xorq %rax, %rax
    movq    OBJ_DATA(%rdi), %rdi   # troca o header pelo ponteiro do texto, uma vez só: o laço
                                   # abaixo avança %rdi byte a byte e não pode voltar ao header
    .loop:
        movzbl (%rdi), %ecx
        testb %cl, %cl
        je .done

        subb $'0', %cl
        js .done
        cmpb $9, %cl
        ja .done

        leaq (%rax,%rax,4), %rax
        shlq $1, %rax

        movzbq %cl, %rcx
        addq %rcx, %rax

        incq %rdi
        jmp .loop

    .done:
        movq    %rbp, %rsp
        popq    %rbp
        ret

# ------------------------------------------------------------------------------------------------
# tarm_strlen — Comprimento de uma string terminada em NUL (conta bytes até o terminador).
#
# In:       %rdi = ponteiro para os bytes do texto (terminado em NUL), não para o header
# Out:      %rax = número de bytes antes do NUL
# Clobbers: %rax, %rbx
# ------------------------------------------------------------------------------------------------
tarm_strlen:
    pushq   %rbp
    movq    %rsp, %rbp

    xorq %rax, %rax

    .L_loop:
        movb (%rdi, %rax, 1), %bl
        testb %bl, %bl
        jz .strlen_done

        incq %rax
        jmp .L_loop

    .strlen_done:
        movq    %rbp, %rsp
        popq    %rbp
        ret

