// ================================================================================================
// File: ast.c
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
// Montagem dos nós declarados em include/ast.h. Duas memórias convivem aqui: a `ExprList`, que é
// um vetor comum de heap e existe só enquanto uma produção do Parser está aberta, e a arena, onde
// os nós e os vetores definitivos ficam até o fim da compilação.
#include "ast.h"
#include <stdlib.h>
#include <string.h>

// Mesmo crescimento por dobra da `TokenList`, e pelo mesmo motivo: o total só se conhece no fim.
// A capacidade inicial é maior (8) porque uma lista de nós costuma ser um corpo de função, não um
// par de itens.
//
// `realloc` devolvendo NULL não libera o bloco antigo, então `l->data` só é sobrescrito depois do
// teste — a lista continua íntegra para quem for liberá-la.
bool ast_list_push(ExprList *l, Expr *e) {
    if (l->count == l->capacity) {
        size_t cap = l->capacity ? l->capacity * 2 : 8;
        Expr **buf = realloc(l->data, cap * sizeof *buf);
        if (!buf) return false;
        l->data = buf;
        l->capacity = cap;
    }
    l->data[l->count++] = e;
    return true;
}

// Libera só o vetor: os nós apontados são da arena e morrem com ela.
void ast_list_free(ExprList *l) {
    free(l->data);
    l->data = NULL;
    l->count = l->capacity = 0;
}

// Passagem do temporário para o definitivo: o vetor é copiado para a arena, onde vai viver junto
// dos nós, e o heap é devolvido na mesma chamada — assim nenhuma produção precisa lembrar de
// liberar a lista no caminho de sucesso.
Expr **ast_list_commit(Arena *a, ExprList *l, size_t *out_count) {
    *out_count = l->count;
    Expr **items = NULL;
    if (l->count) {
        items = arena_alloc(a, l->count * sizeof *items);
        if (items) memcpy(items, l->data, l->count * sizeof *items);
    }
    ast_list_free(l);
    return items;
}

// Preenche só o que todo nó tem em comum. O resto da `union` fica por conta de quem chama — e não
// vem zerado, porque a arena entrega a memória como está (ver o aviso em ast.h).
Expr *ast_expr_new(Arena *a, ExprKind kind, uint32_t line, uint32_t col) {
    Expr *e = arena_alloc(a, sizeof *e);
    if (!e) return NULL;
    e->kind = kind;
    e->type = (DataType){ .type = Void, .size_of = 0, .is_array = false, .array_len = 0 };
    e->line = line;
    e->col  = col;
    return e;
}
