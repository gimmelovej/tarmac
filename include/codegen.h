// ================================================================================================
// File: codegen.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Quarta e última etapa do pipeline: emite assembly x86-64 a partir da AST.
/// @details Percorre a árvore já validada pela análise semântica e escreve assembly GNU (sintaxe
/// AT&T) num `FILE *`. Não valida nada: um programa que chegou aqui já passou pelas barreiras do
/// Driver, então qualquer inconsistência é bug do compilador, não erro do usuário.
/// @note Convenção interna: **toda expressão deixa seu resultado em `%rax`**. É isso que permite
/// compor as produções sem combinar registradores caso a caso.
/// @note Os rótulos a chamar não são conhecidos aqui: vêm da `FunctionTable`, que sabe qual rotina
/// de `runtime/*.s` corresponde a cada nativa (e, no caso de `print`, a cada tipo de argumento).
/// @see docs/architecture.md#pipeline
/// @see docs/runtime.md

#ifndef TARM_CODEGEN_H
#define TARM_CODEGEN_H

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

#include "errors.h"
#include "ast.h"
#include "symbol_table.h"
#include "function_table.h"

/// @brief Estado da geração de código para uma unidade de compilação.
/// @note O gerador é dono da própria `SymbolTable`: os offsets de frame são atribuídos aqui,
/// função a função, em vez de herdados da análise semântica — que abre e fecha os escopos dela por
/// conta própria e não deixa nada para trás.
typedef struct {
    FILE          *out;
    Diagnostics   *diag;
    FunctionTable *functions;     ///< Emprestada pelo Driver; é dela que sai o rótulo de cada chamada.

    SymbolTable    symbols;       ///< Própria; ver nota acima.
    size_t         label_counter; ///< Gera `.L<n>` únicos para desvios.
    size_t         string_counter;///< Gera os `strobj_<n>`/`strbytes_<n>` dos literais em `.rodata`.
    size_t         return_label;  ///< Rótulo do epílogo da função corrente.
    int            stack_depth;   ///< Pushes pendentes; ver `call_align` em codegen.c.
} Codegen;

/// @brief Prepara o gerador para escrever em `out`.
void tarm_codegen_init(Codegen *cg, FILE *out, Diagnostics *diag, FunctionTable *functions);

/// @brief Libera os recursos próprios do gerador (a tabela de símbolos interna).
void tarm_codegen_free(Codegen *cg);

/// @brief Emite o programa inteiro: seção de dados, globais e o corpo de cada função.
/// @param program Vetor de instruções de nível superior, já validado.
/// @return `false` se algo não suportado apareceu — o motivo é registrado no `Diagnostics`.
bool tarm_codegen_generate(Codegen *cg, Expr **program, size_t count);

#endif