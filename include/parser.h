// ================================================================================================
// File: parser.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Segunda etapa do pipeline: consome a sequência de `Token` e constrói a AST.
/// @details *Parser descendente recursivo*, como o do Tarmac em C++, com duas cadeias de produções:
/// uma de instrução (nível superior → declaração → instrução), que decide o que uma linha do
/// programa **é**, e uma de precedência de expressão (atribuição → igualdade → relacional →
/// aditiva → multiplicativa → posfixa → primária), que decide como uma expressão é **agrupada**.
/// Os nós saem em `ast.h` e são alocados na arena recebida em `tarm_parser_init`.
/// @see docs/parser.md
/// @see docs/architecture.md#pipeline

#ifndef TARM_PARSER_H
#define TARM_PARSER_H

#include "errors.h"
#include "types.h"
#include "arena.h"
#include "ast.h"
#include <stddef.h>

/// @brief Estado da análise sintática sobre uma lista de tokens já produzida pelo Lexer.
/// @note O Parser **não é dono** de nada do que guarda: `tokens` é uma cópia do descritor da lista
/// (o vetor continua a ser liberado por quem o criou — hoje, o Driver), e `diag`/`arena` são
/// emprestados pelo Driver, que os cria e destrói em volta da análise.
typedef struct {
    TokenList    tokens;
    size_t       pos;    ///< Índice do próximo token a consumir.
    Diagnostics *diag;
    Arena       *arena;  ///< Onde os nós da AST são alocados (ver ast.h).
} Parser;


/// @brief Prepara a análise sobre `toks`, ligando o Parser ao diagnóstico e à arena da compilação.
/// @param toks Lista de tokens produzida por `tarm_lexer_tokenize`, que deve continuar viva —
/// junto com o buffer de código-fonte para o qual os tokens apontam — durante toda a análise.
/// @param arena Região onde os nós da AST são alocados; precisa sobreviver à árvore inteira,
/// porque liberá-la invalida todos os nós de uma vez.
/// @note `toks` é lida e copiada para dentro do `Parser` (só o descritor: ponteiro, contagem e
/// capacidade), então a lista original não é modificada.
void tarm_parser_init(Parser *ps, const TokenList *toks, Diagnostics *diag, Arena *arena);

/// @brief Ponto de entrada da gramática: reconhece o programa inteiro.
/// @return `true` se o programa foi reconhecido sem nenhum erro registrado no `Diagnostics` — o
/// mesmo contrato de `tarm_lexer_tokenize`.
/// @param out_items Recebe o vetor de instruções de nível superior, alocado na arena.
/// @param out_count Recebe a quantidade de instruções em `out_items`.
/// @note A árvore sai por parâmetro de saída, e não pelo retorno, que é reservado ao sucesso/falha.
/// @note Sem recuperação de erro ainda: a primeira produção que falha encerra a análise, então uma
/// rodada relata um erro de sintaxe por vez.
/// @see docs/parser.md#erros-e-recuperação
bool tarm_parser_program(Parser *ps, Expr ***out_items, size_t *out_count);

#endif
