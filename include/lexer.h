// ================================================================================================
// File: lexer.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Primeira etapa do pipeline: transforma código-fonte bruto numa sequência de `Token`.
/// @see docs/architecture.md#pipeline

#ifndef TARM_LEXER_H
#define TARM_LEXER_H

#include "types.h"
#include "errors.h"

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/// @brief Estado da varredura léxica sobre um buffer de código-fonte.
/// @details Dois pares de posição coexistem de propósito: `line`/`col` acompanham o **cursor**
/// (avançam a cada `consume`), enquanto `tok_line`/`tok_col` congelam onde o token atual
/// **começou** — é esse par que vai para o token e para a mensagem de erro, de modo que ambos
/// apontem o início do lexema e não o ponto onde a varredura parou.
typedef struct {
    const char  *src;   ///< Buffer do arquivo-fonte; o Lexer não é dono dele (ver file.h).
    size_t       len;   ///< Bytes válidos em `src`.
    size_t       start; ///< Início do lexema em construção; `[start, pos)` é o token atual.
    size_t       pos;   ///< Cursor de leitura.
    uint32_t     line, col;
    uint32_t     tok_line, tok_col;
    Diagnostics *diag;
} Lexer;

/// @brief Prepara a varredura de `source`, ligando o Lexer ao diagnóstico do compilador.
/// @param source Buffer do arquivo-fonte, que deve sobreviver a todo uso dos tokens produzidos.
/// @param len Bytes válidos em `source`.
void tarm_lexer_init(Lexer *lx, const char *source, size_t len, Diagnostics *diag);

/// @brief Percorre todo o código-fonte e preenche `tokens` com a sequência reconhecida.
/// @param tokens Lista de saída; recebe uma lista nova, sempre terminada por `EndOfFile`.
/// @return `true` se a varredura terminou sem nenhum erro registrado no `Diagnostics`.
/// @note Um lexema não reconhecido não interrompe a varredura: vira um token `Invalid`, o erro é
/// acumulado e a leitura segue — assim um arquivo com vários problemas os relata de uma vez.
/// @note Literais de string e de caractere guardam apenas o **conteúdo**, sem as aspas. As
/// sequências de escape são **validadas** mas não decodificadas: `\n` chega como dois caracteres,
/// e a conversão fica para quem materializar o valor — o Parser, num literal de caractere, e a
/// Codegen, num de string, que repassa a fatia ao `as`. É o que mantém o Lexer sem alocação
/// nenhuma.
bool tarm_lexer_tokenize(Lexer *lx, TokenList *tokens);

/// @brief Libera o vetor de tokens e devolve a lista ao estado vazio.
/// @note Não toca no buffer de código-fonte: os tokens apenas apontam para dentro dele.
void tarm_lexer_tokens_free(TokenList *list);

/// @brief Nome legível de um `TokenKind`, para depuração e mensagens.
const char *tarm_token_kind_name(TokenKind kind);

/// @brief Imprime a lista de tokens com posição e lexema, uma linha por token.
void tarm_lexer_dump_tokens(FILE *out, const TokenList *list);

#endif
