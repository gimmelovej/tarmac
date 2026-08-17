# ================================================================================================
# File: object.s — Objetos com header (buffers) da runtime tarm. Convenção: System V AMD64 ABI.
# Reference: docs/runtime.md
#
# Um "objeto" é um bloco no heap (alocado por tarm_brk_alloc) com um header fixo de 24 bytes
# seguido do payload (os dados propriamente ditos). O ponteiro que circula pela linguagem aponta
# sempre para o header; o payload começa em OBJ_SIZE bytes depois, e OBJ_DATA guarda esse endereço.
#
#   offset 0  (OBJ_DATA) : ponteiro para o payload (header + 24)
#   offset 8  (OBJ_LEN)  : bytes atualmente usados no payload
#   offset 16 (OBJ_CAP)  : capacidade do payload em bytes
#   offset 24 (OBJ_SIZE) : início do payload
# ================================================================================================
.include "object.inc"
.global tarm_obj_new
.global tarm_obj_len
.global tarm_buf_str


.text

# ------------------------------------------------------------------------------------------------
# tarm_obj_new — Aloca um objeto (header de 24 bytes + payload) via tarm_brk_alloc e inicializa o
# header: LEN e CAP recebem o tamanho pedido e DATA aponta para o início do payload.
#
# In:       %rdi = tamanho do payload em bytes
# Out:      %rax = ponteiro para o header, ou 0 se a alocação falhar
# Clobbers: %rax, %rdi, %rcx (%r12 salvo/restaurado via pilha)
# ------------------------------------------------------------------------------------------------
tarm_obj_new:
    pushq   %rbp
    movq    %rsp, %rbp 
    pushq %r12

    movq    %rdi, %r12

    leaq    24(%rdi), %rdi 
    call    tarm_brk_alloc   

    testq %rax, %rax
    jz .obj_done
    
    movq %r12, OBJ_LEN(%rax)
    movq %r12, OBJ_CAP(%rax)

    leaq    OBJ_SIZE(%rax), %rcx
    movq    %rcx, OBJ_DATA(%rax)

    .obj_done:
        popq    %r12
        movq    %rbp, %rsp
        popq    %rbp
        ret

# ------------------------------------------------------------------------------------------------
# tarm_obj_len — Comprimento (OBJ_LEN) de um objeto. Implementa o método `.len()` de Buffer/String.
#
# In:       %rdi = ponteiro para o header do objeto
# Out:      %rax = bytes usados (OBJ_LEN)
# Clobbers: %rax
# ------------------------------------------------------------------------------------------------
tarm_obj_len:
    movq    OBJ_LEN(%rdi), %rax
    ret

# ------------------------------------------------------------------------------------------------
# tarm_buf_str — Vê um Buffer como String. Implementa o método `.str()` de Buffer. É a identidade:
# a String da linguagem também é um ponteiro-para-header, então basta devolver o mesmo ponteiro.
#
# In:       %rdi = ponteiro para o header do objeto (Buffer)
# Out:      %rax = o mesmo ponteiro (agora tratado como String)
# Clobbers: %rax
# ------------------------------------------------------------------------------------------------
tarm_buf_str:
    movq    %rdi, %rax
    ret

