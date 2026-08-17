// ================================================================================================
// File: arena.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Alocador de arena: memória por região, liberada de uma vez só.
/// @details Substitui o que, no Tarmac em C++, era feito por `unique_ptr` — os nós da AST nascem
/// e morrem juntos, então não vale carregar um `free` por nó. A arena entrega blocos sequenciais e
/// devolve tudo ao sistema numa chamada, no fim da compilação.
/// @see docs/architecture.md#arena-no-lugar-de-unique_ptr

#ifndef TARM_ARENA_H
#define TARM_ARENA_H

#include <stddef.h>

/// @brief Bloco de memória da arena; o layout é privado a `arena.c`.
typedef struct ArenaBlock ArenaBlock;

/// @brief Região de alocação: uma lista de blocos, do mais recente para o mais antigo.
/// @note Declarada pelo chamador (normalmente na pilha) e sempre inicializada por `arena_init`
/// antes de qualquer outra chamada — inclusive antes de um `arena_free` em caminho de erro.
typedef struct {
    ArenaBlock *head;
} Arena;

/// @brief Deixa a arena vazia e pronta para a primeira alocação.
/// @note Não aloca nada: o primeiro bloco só nasce no primeiro `arena_alloc`.
void  arena_init(Arena *a);

/// @brief Reserva `size` bytes na arena.
/// @return Ponteiro para a região reservada, ou NULL se não houver memória.
/// @note A memória volta ao sistema só em `arena_free` — não existe liberação individual.
/// @note O tamanho é arredondado para múltiplo de 16; um pedido maior que o bloco padrão
/// (64 KiB) ganha um bloco próprio, do tamanho exato, em vez de falhar.
/// @warning O conteúdo devolvido **não** vem zerado.
void *arena_alloc(Arena *a, size_t size);

/// @brief Libera todos os blocos da arena de uma vez.
/// @warning Invalida **todo** ponteiro devolvido por `arena_alloc` nesta arena.
/// @note Deixa a arena vazia e reutilizável, de modo que chamá-la duas vezes é inofensivo.
void  arena_free(Arena *a);

#endif
