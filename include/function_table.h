// ================================================================================================
// File: function_table.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Assinaturas das funções conhecidas: as nativas da linguagem e as declaradas pelo usuário.
/// @details Preenchida em duas frentes — `tarm_function_table_register_natives` antes de percorrer
/// a árvore, e `tarm_function_table_declare` numa passagem prévia sobre as declarações de função.
/// É o que permite validar aridade e tipos de uma chamada, saber o `BaseType` que ela devolve e,
/// na geração de código, descobrir qual rótulo do runtime chamar.
/// @note Separada da `SymbolTable` de propósito: variáveis e funções vivem em espaços de nome
/// distintos, e a tabela de símbolos carrega offset de stack, que não faz sentido para uma função.
/// @see docs/architecture.md#duas-tabelas-e-por-que-são-separadas

#ifndef TARM_FUNCTION_TABLE_H
#define TARM_FUNCTION_TABLE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "types.h"

/// @brief Máximo de parâmetros por função.
/// @note Limite fixo para manter a assinatura numa struct de tamanho conhecido; passar disso é
/// recusado por `tarm_function_table_declare`, com erro para o usuário.
#define TARM_MAX_PARAMS 16

/// @brief Bit de um `BaseType` dentro de uma máscara de tipos aceitos.
#define TARM_TYPE_BIT(t) (1u << (unsigned)(t))

/// @brief Assinatura de uma função ou método.
/// @details Para um **método**, `receiver` é o tipo do valor à esquerda do `.` e `params` cobre
/// apenas os argumentos explícitos — o receptor entra como `args[0]` no nó `ExprMethod`, mas não
/// conta em `param_count`.
typedef struct {
    const char *name; ///< Fatia do buffer de origem (nativas apontam para literais estáticos).
    uint32_t    name_len;
    BaseType    ret_type;
    BaseType    params[TARM_MAX_PARAMS];

    /// Conjunto de tipos aceitos em cada posição, como máscara de `TARM_TYPE_BIT`. Zero significa
    /// "só o tipo exato de `params[i]`"; é o caso de toda função do usuário. Uma nativa como
    /// `print` aceita várias, e é aqui que essa lista mora.
    uint32_t accepted[TARM_MAX_PARAMS];
    size_t   param_count;

    bool is_native;     ///< Implementada pelo runtime, não pelo usuário.
    bool is_variadic;   ///< Aceita qualquer aridade (ex.: `print`); todo argumento é conferido
                        ///< contra `accepted[0]`.
    bool     is_method; ///< Chamada com `.`; ver `receiver`.
    BaseType receiver;  ///< Tipo do receptor (só relevante quando `is_method`).

    /// Rótulo do runtime a chamar. Vazio para função do usuário (o rótulo sai do nome, com
    /// *mangling*) e para nativa com despacho por tipo (ver `dispatch_param`).
    const char *symbol;

    /// Índice do argumento cujo tipo escolhe o rótulo, ou -1 quando não há despacho. É o mecanismo
    /// de `print`: um nome na linguagem, cinco rotinas diferentes no runtime.
    int dispatch_param;

    /// Rótulo por tipo do argumento de despacho, indexado por `BaseType`. NULL onde o tipo não é
    /// suportado — o que não deve acontecer, porque `accepted` já barra esses casos na semântica.
    const char *symbol_by_type[Void + 1];
} FunctionSignature;

/// @brief Conjunto de assinaturas conhecidas.
/// @note Inicializar com `tarm_function_table_init`; liberar com `tarm_function_table_free`.
typedef struct {
    FunctionSignature *data;
    size_t             count;
    size_t             capacity;
} FunctionTable;

/// @brief Prepara uma tabela vazia.
void tarm_function_table_init(FunctionTable *ft);

/// @brief Libera o vetor de assinaturas. Seguro chamar mais de uma vez.
void tarm_function_table_free(FunctionTable *ft);

/// @brief Registra as funções e métodos embutidos na linguagem.
/// @return `false` se a alocação falhar.
/// @note Chamar uma única vez, antes de percorrer a AST.
bool tarm_function_table_register_natives(FunctionTable *ft);

/// @brief Registra uma função declarada pelo usuário.
/// @param params Tipos dos parâmetros, na ordem; pode ser `NULL` se `param_count` for 0.
/// @param out_reason Recebe o motivo da recusa, para a mensagem de erro. Pode ser `NULL`.
/// @return `false` se o nome já existir, se `param_count` passar de `TARM_MAX_PARAMS`, ou se a
/// realocação falhar. Sem erro registrado aqui — é quem chama que tem a posição do nó.
bool tarm_function_table_declare(FunctionTable *ft, const char *name, uint32_t name_len,
                                 BaseType ret_type, const BaseType *params, size_t param_count,
                                 const char **out_reason);

/// @brief Busca uma função comum (não método) pelo nome.
/// @return Ponteiro para a assinatura, ou `NULL` se não existir.
/// @warning Invalidado por qualquer `declare` posterior, que pode realocar o vetor. Use antes da
/// próxima declaração, ou copie a struct.
const FunctionSignature *tarm_function_table_find(const FunctionTable *ft, const char *name,
                                                  uint32_t name_len);

/// @brief Busca um método pelo nome **e** pelo tipo do receptor.
/// @details Métodos de mesmo nome podem existir para receptores diferentes, então o nome sozinho
/// não identifica a assinatura.
/// @return Ponteiro para a assinatura, ou `NULL` se não houver método com esse nome para o tipo.
const FunctionSignature *tarm_function_table_find_method(const FunctionTable *ft, const char *name,
                                                         uint32_t name_len, BaseType receiver);

/// @brief Indica se já existe uma função com o nome dado.
bool tarm_function_table_exists(const FunctionTable *ft, const char *name, uint32_t name_len);

/// @brief Confere a aridade de uma chamada contra a assinatura.
/// @param arg_count Argumentos explícitos (sem o receptor, no caso de método).
/// @return `true` se a contagem for aceitável — sempre verdadeiro para uma função variádica.
bool tarm_function_table_check_arity(const FunctionSignature *sig, size_t arg_count);

/// @brief Conjunto de tipos aceitos na posição `index` de uma chamada, como máscara.
/// @details Resolve as três formas de escrever a mesma coisa: variádica (todo argumento é conferido
/// contra `accepted[0]`), posição com lista explícita, e posição com um tipo exato em `params`.
/// @return Máscara de `TARM_TYPE_BIT`; 0 se a posição não existir na assinatura.
uint32_t tarm_function_table_accepted_mask(const FunctionSignature *sig, size_t index);

/// @brief Rótulo do runtime a chamar, dado o tipo do argumento de despacho.
/// @param dispatch_type Tipo do argumento em `sig->dispatch_param`; ignorado quando não há despacho.
/// @return O rótulo, ou `NULL` se a assinatura for de função do usuário (o rótulo sai do nome) ou
/// se o tipo não tiver implementação no runtime.
const char *tarm_function_table_symbol(const FunctionSignature *sig, BaseType dispatch_type);

#endif
