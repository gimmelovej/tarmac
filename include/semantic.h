// ================================================================================================
// File: semantic.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Terceira etapa do pipeline: valida tipos na AST e insere conversões implícitas.
/// @details Percorre a árvore produzida pelo Parser resolvendo o `DataType` de cada nó, registrando
/// variáveis na `SymbolTable`, conferindo cada chamada contra a `FunctionTable` e envolvendo
/// operandos em `ExprCast` quando a conversão é permitida. A árvore sai **anotada**: `Expr::type`
/// guarda o tipo resolvido, de modo que a Codegen não precise refazer inferência nenhuma.
/// @see docs/architecture.md#a-ast-como-união-etiquetada

#ifndef TARM_SEMANTIC_H
#define TARM_SEMANTIC_H

#include "errors.h"
#include "arena.h"
#include "ast.h"
#include "symbol_table.h"
#include "function_table.h"

/// @brief Faixa representável por `Char`, usada por `validate_integer_range`.
/// @note São macros, e não `const int64_t` no escopo de arquivo: em C um `const` no header é uma
/// **definição**, replicada em toda unidade de tradução que o incluir, e o link falha com
/// `multiple definition`.
#define TARM_CHAR_MIN_VAL (-128)
#define TARM_CHAR_MAX_VAL (127)

/// @brief Estado da análise semântica sobre uma AST já construída.
/// @note O analisador **não é dono** de nada: `diag`, `arena`, `symbols` e `functions` são
/// emprestados pelo Driver. A arena precisa ser a mesma usada pelo Parser — é nela que os
/// `ExprCast` inseridos serão alocados, e liberá-la invalida a árvore inteira.
typedef struct {
    Diagnostics   *diag;
    Arena         *arena;
    SymbolTable   *symbols;
    FunctionTable *functions;

    DataType current_return_type; ///< Tipo de retorno da função sendo validada.
    bool     inside_function;     ///< Falso no nível superior: `return` solto vira erro.
} SemanticAnalyzer;

/// @brief Prepara a análise, ligando o analisador aos recursos da compilação.
/// @param functions Tabela já preenchida com as nativas; a análise acrescenta as funções do
/// usuário numa passagem prévia, antes de validar qualquer corpo.
void tarm_semantic_init(SemanticAnalyzer *an, Diagnostics *diag, Arena *arena,
                        SymbolTable *symbols, FunctionTable *functions);

/// @brief Valida, em ordem, cada instrução do programa.
/// @param program Vetor de instruções de nível superior, como devolvido por `tarm_parser_program`.
/// @param count Quantidade de instruções em `program`.
/// @return `true` se nenhum erro semântico foi registrado.
/// @details Roda em duas passagens. A primeira só registra as assinaturas das funções declaradas,
/// de modo que uma função possa chamar outra definida **depois** dela no arquivo; a segunda valida
/// os corpos.
/// @note A árvore é **mutada**: nós ganham `type` preenchido e podem ser envolvidos em `ExprCast`.
/// Diferente da versão em C++, um erro não interrompe a análise — ele é acumulado no `Diagnostics`
/// e a validação segue, para que o usuário veja todos os problemas de uma vez (ver errors.h).
bool tarm_semantic_analyse(SemanticAnalyzer *an, Expr **program, size_t count);

/// @brief Nome legível de um `BaseType`, para mensagens de erro.
const char *tarm_semantic_type_name(BaseType type);

#endif
