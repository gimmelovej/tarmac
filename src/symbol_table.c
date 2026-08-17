// ================================================================================================
// File: symbol_table.c
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
#include "symbol_table.h"
#include "errors.h"

#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
// Internos
// ------------------------------------------------------------------------------------------------

// Compara uma fatia (sem '\0') com outra. O teste de comprimento vem primeiro: é ele que impede
// tanto ler além da fatia quanto casar "contador" com "conta".
static bool slice_eq(const char *a, uint32_t a_len, const char *b, uint32_t b_len) {
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

// Busca linear. Com a dezena de variáveis de uma função é mais rápido que uma tabela hash, e sem
// o custo de manter uma; se um dia o perfil acusar, é aqui que entra o índice.
static Symbol *find_mut(SymbolTable *st, const char *name, uint32_t name_len) {
    for (size_t i = 0; i < st->count; i++)
        if (slice_eq(st->data[i].name, st->data[i].name_len, name, name_len))
            return &st->data[i];
    return NULL;
}

// Cresce por dobra de capacidade, como a TokenList. Devolve o slot novo, já contabilizado.
static Symbol *entry_push(SymbolTable *st) {
    if (st->count == st->capacity) {
        size_t new_capacity = (st->capacity == 0) ? 8 : st->capacity * 2;
        Symbol *buffer = realloc(st->data, new_capacity * sizeof *st->data);

        if (buffer == NULL) {
            tarm_system_error("Não foi possível realocar a tabela de símbolos");
            return NULL;
        }
        st->data = buffer;
        st->capacity = new_capacity;
    }
    return &st->data[st->count++];
}

// Arredonda para cima até o próximo múltiplo de `align`, que precisa ser potência de 2.
static size_t align_up(size_t value, size_t align) {
    return (value + (align - 1)) & ~(align - 1);
}

// ------------------------------------------------------------------------------------------------
// Ciclo de vida
// ------------------------------------------------------------------------------------------------

void tarm_symbol_table_init(SymbolTable *st) {
    st->data     = NULL;
    st->count    = 0;
    st->capacity = 0;

    st->initial_stack_offset = TARM_INITIAL_STACK_OFFSET;
    st->current_stack_offset = TARM_INITIAL_STACK_OFFSET;
    st->next_label_id        = 0;
}

void tarm_symbol_table_free(SymbolTable *st) {
    free(st->data);
    st->data     = NULL;
    st->count    = 0;
    st->capacity = 0;
}

// ------------------------------------------------------------------------------------------------
// Consultas
// ------------------------------------------------------------------------------------------------

size_t tarm_symbol_table_data_size(DataType type) {
    switch (type) {
        case Char:   return 1;
        case Int:    return 4;
        case Float:  return 4;
        case Bool:   return 1;
        case Int64:  return 8;
        case String: return 16;
        case Void:   return 0;
    }
    // Inalcançável para um DataType válido; o `-Wswitch` avisa se um caso novo ficar de fora.
    return 0;
}

const Symbol *tarm_symbol_table_find(const SymbolTable *st,
                                     const char *name, uint32_t name_len) {
    for (size_t i = 0; i < st->count; i++)
        if (slice_eq(st->data[i].name, st->data[i].name_len, name, name_len))
            return &st->data[i];
    return NULL;
}

bool tarm_symbol_table_exists(const SymbolTable *st,
                              const char *name, uint32_t name_len) {
    return tarm_symbol_table_find(st, name, name_len) != NULL;
}

int tarm_symbol_table_lookup(const SymbolTable *st,
                             const char *name, uint32_t name_len) {
    const Symbol *sym = tarm_symbol_table_find(st, name, name_len);
    return sym ? sym->offset : -1;
}

DataType tarm_symbol_table_type(const SymbolTable *st,
                                const char *name, uint32_t name_len) {
    const Symbol *sym = tarm_symbol_table_find(st, name, name_len);
    return sym ? sym->type : Int64;
}

int tarm_symbol_table_string_length(const SymbolTable *st,
                                    const char *name, uint32_t name_len) {
    const Symbol *sym = tarm_symbol_table_find(st, name, name_len);
    return sym ? (int)sym->size : -1;
}

bool tarm_symbol_table_label_id(const SymbolTable *st, const char *name, uint32_t name_len,
                                size_t *out_id) {
    const Symbol *sym = tarm_symbol_table_find(st, name, name_len);
    if (!sym || !sym->is_global) return false;
    if (out_id) *out_id = sym->label_id;
    return true;
}

// ------------------------------------------------------------------------------------------------
// Declaração
// ------------------------------------------------------------------------------------------------

bool tarm_symbol_table_declare(SymbolTable *st, const char *name, uint32_t name_len,
                               DataType type, size_t size, int *out_offset) {
    if (find_mut(st, name, name_len) != NULL)
        return false;   // redeclaração — quem chama reporta, com a posição do nó

    size_t bytes = (size > 0) ? size : tarm_symbol_table_data_size(type);

    // Cada slot ocupa um múltiplo de 8: mantém os acessos alinhados e simplifica o prólogo.
    st->current_stack_offset -= (int)align_up(bytes, 8);

    Symbol *sym = entry_push(st);
    if (!sym) {
        st->current_stack_offset += (int)align_up(bytes, 8);   // desfaz a reserva
        return false;
    }

    sym->name      = name;
    sym->name_len  = name_len;
    sym->offset    = st->current_stack_offset;
    sym->type      = type;
    sym->size      = bytes;
    sym->label_id  = 0;
    sym->is_global = false;

    if (out_offset) *out_offset = sym->offset;
    return true;
}

bool tarm_symbol_table_declare_global(SymbolTable *st, const char *name, uint32_t name_len,
                                      DataType type, size_t size, size_t *out_label_id) {
    if (find_mut(st, name, name_len) != NULL)
        return false;

    Symbol *sym = entry_push(st);
    if (!sym) return false;

    sym->name      = name;
    sym->name_len  = name_len;
    sym->offset    = 0;   // globais não usam offset de frame
    sym->type      = type;
    sym->size      = (size > 0) ? size : tarm_symbol_table_data_size(type);
    sym->label_id  = st->next_label_id++;
    sym->is_global = true;

    if (out_label_id) *out_label_id = sym->label_id;
    return true;
}

// ------------------------------------------------------------------------------------------------
// Frame
// ------------------------------------------------------------------------------------------------

int tarm_symbol_table_total_bytes(const SymbolTable *st) {
    int used = st->initial_stack_offset - st->current_stack_offset;
    if (used < 0) used = 0;
    return (int)align_up((size_t)used, 16);
}

// Escopo por marca de pilha: entrar numa função guarda o topo da tabela, sair descarta tudo que
// veio depois. É o que faz dois `int i` em funções diferentes conviverem — sem isso a tabela é
// plana e a segunda declaração seria recusada como redeclaração.
//
// Os globais são declarados antes da primeira função, então ficam abaixo de qualquer marca e
// sobrevivem a todos os escopos.
size_t tarm_symbol_table_scope_begin(SymbolTable *st) {
    st->current_stack_offset = st->initial_stack_offset;
    return st->count;
}

void tarm_symbol_table_scope_end(SymbolTable *st, size_t mark) {
    if (mark <= st->count)
        st->count = mark;
    st->current_stack_offset = st->initial_stack_offset;
}