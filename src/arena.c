// ================================================================================================
// File: arena.c
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
// Implementação do alocador de arena declarado em include/arena.h.
//
// A arena é um *bump allocator* sobre uma lista de blocos: cada `arena_alloc` empurra o cursor
// `used` do bloco da frente e devolve o pedaço; quando não cabe mais, um bloco novo entra na
// frente da lista. Nada é devolvido individualmente — a memória só volta ao sistema em
// `arena_free`. Ver docs/architecture.md#arena-no-lugar-de-unique_ptr.

#include "arena.h"
#include <stdlib.h>

// 64 KiB por bloco: grande o bastante para diluir o custo do `malloc` entre milhares de nós de
// AST, pequeno o bastante para não desperdiçar num arquivo-fonte curto.
#define ARENA_BLOCK_SIZE (64 * 1024)
#define ARENA_ALIGN 16

// `char data[]` (membro de array flexível) põe o payload logo depois do cabeçalho, na mesma
// alocação: um `malloc` por bloco, e não um para o cabeçalho e outro para os bytes.
struct ArenaBlock {
    ArenaBlock *next;
    size_t used;
    size_t capacity;
    char data[];
};

// Não aloca nada: o primeiro bloco só nasce no primeiro `arena_alloc`, então uma arena que nunca
// foi usada não custa nada. Em compensação, `arena_free` depende desta chamada ter acontecido —
// sem ela, `head` é lixo da pilha.
void arena_init(Arena *a) {
    a->head = NULL;
}

static ArenaBlock *new_block(size_t capacity) {
    ArenaBlock *b = malloc(sizeof *b + capacity);
    if (!b) return NULL;
    b->next     = NULL;
    b->used     = 0;
    b->capacity = capacity;
    return b;
}

// O bloco novo entra na **frente** da lista: a alocação seguinte só olha `head`, sem percorrer
// nada. O espaço que sobrou nos blocos antigos é abandonado de propósito — procurar buraco custaria
// mais do que o desperdício que evita.
//
// Um pedido maior que o bloco padrão ganha um bloco do tamanho exato, em vez de falhar: assim
// nenhum tamanho é grande demais para a arena.
//
// NOTA: o arredondamento mantém cada pedaço múltiplo de 16, mas o cabeçalho tem 24 bytes, então
// `data` começa 8 bytes fora do alinhamento que o `malloc` garante — na prática os ponteiros saem
// alinhados a 8. Basta para ponteiros, `size_t` e `double`; um tipo que exija 16 pediria um
// cabeçalho preenchido até 32.
void *arena_alloc(Arena *a, size_t size) {
    // arredonda para múltiplo do alinhamento
    size = (size + (ARENA_ALIGN - 1)) & ~(size_t)(ARENA_ALIGN - 1);

    if (!a->head || a->head->used + size > a->head->capacity) {
        size_t cap = (size > ARENA_BLOCK_SIZE) ? size : ARENA_BLOCK_SIZE;
        ArenaBlock *b = new_block(cap);
        if (!b) return NULL;
        b->next = a->head;
        a->head = b;
    }

    void *p = a->head->data + a->head->used;
    a->head->used += size;
    return p;
}

// Guarda `next` antes do `free`: depois de liberar o bloco, ler qualquer campo dele é
// use-after-free — o erro clássico de percorrer lista encadeada liberando pelo caminho.
//
// Zerar `head` no fim deixa a arena reutilizável e torna um `arena_free` repetido inofensivo.
void arena_free(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}
