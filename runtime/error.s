# ================================================================================================
# File: error.s — Aborto de execução da runtime tarm. Convenção: System V AMD64 ABI.
# Reference: docs/runtime.md#aborto-de-execucao
#
# Tratamento genérico, de propósito: uma mensagem só, sem contexto do que falhou. É o suficiente
# para o programa parar em vez de continuar sobre memória inválida — que era o que acontecia antes
# de a verificação de faixa existir. Uma versão com causa e posição virá depois.
# ================================================================================================

.section .rodata
    fatal_error_msg:
    .ascii "erro de execução: valor inválido\n"
    .set msg_len, . - fatal_error_msg

.text
.global fatal_error_

# ------------------------------------------------------------------------------------------------
# fatal_error_ — Escreve a mensagem em stderr e encerra o processo com código 1.
# Reference: docs/runtime.md#aborto-de-execucao
#
# **Não retorna.** Quem desvia para cá (hoje, a verificação de faixa emitida pela Codegen) não
# precisa preservar registrador nenhum nem alinhar a pilha: o processo acaba aqui.
#
# `msg_len` é calculado pelo montador (`. - fatal_error_msg`), então mudar o texto da mensagem não
# exige acertar o tamanho à mão.
#
# In:       nenhum
# Out:      não retorna (syscall exit, #60, com status 1)
# Clobbers: irrelevante — o processo encerra
# ------------------------------------------------------------------------------------------------
fatal_error_:
    movq $1, %rax        # syscall write (#1)
    movq $2, %rdi        # fd 2 = stderr
    leaq fatal_error_msg(%rip), %rsi
    movq $msg_len, %rdx
    syscall

    movq $60, %rax       # syscall exit (#60)
    movq $1, %rdi        # exit(1)
    syscall

