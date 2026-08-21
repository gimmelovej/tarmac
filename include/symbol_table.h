// ================================================================================================
// File: symbol_table.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Tabela de símbolos: registra as variáveis declaradas e onde cada uma vive.
/// @details Uma instância por unidade de compilação, criada pelo Driver e emprestada à análise
/// semântica e à Codegen. Variáveis locais recebem um offset relativo a `%rbp`; globais viram
/// dado estático em `.data`, identificado por `label_id`.
/// @note Nomes são **fatias** do buffer de código-fonte (`name`/`name_len`), como nos tokens e nos
/// nós da AST — a tabela só é válida enquanto esse buffer viver, e as fatias não terminam em '\0'.

#ifndef TARM_SYMBOL_TABLE_H
#define TARM_SYMBOL_TABLE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "types.h"

/// @brief Offset inicial do frame, em bytes relativos a `%rbp`.
/// @details Os primeiros 32 bytes ficam reservados (área de salvamento de registradores); a
/// primeira variável local começa abaixo disso.
#define TARM_INITIAL_STACK_OFFSET (-32)

/// @brief Entrada da tabela para uma variável.
/// @details `offset` é o deslocamento relativo a `%rbp` no frame; `type` é o `DataType` declarado;
/// `size` é o tamanho em bytes do valor (para String, o comprimento — ver
/// `tarm_symbol_table_string_length`). Para uma variável **global**, `is_global` é `true` e
/// `label_id` identifica o rótulo `globobj_<label_id>` do dado estático em `.data`; nesse caso
/// `offset` não é usado.
typedef struct {
    const char *name; ///< Fatia do buffer de origem; não termina em '\0'.
    uint32_t    name_len;
    int         offset; ///< Relativo a `%rbp` (só para locais).
    DataType    type;
    size_t      size;
    size_t      label_id; ///< Id do rótulo `globobj_N` (só relevante quando `is_global`).
    bool        is_global;
} Symbol;

/// @brief Conjunto de símbolos declarados, com o controle de offset do frame corrente.
/// @note Inicializar sempre com `tarm_symbol_table_init`; liberar com `tarm_symbol_table_free`.
typedef struct {
    Symbol *data;
    size_t  count;
    size_t  capacity;

    int    initial_stack_offset;
    int    current_stack_offset;
    size_t next_label_id; ///< Contador dos rótulos `globobj_N`.
} SymbolTable;

/// @brief Prepara uma tabela vazia com o frame no offset inicial.
void tarm_symbol_table_init(SymbolTable *st);

/// @brief Libera o vetor de símbolos. Seguro chamar mais de uma vez.
void tarm_symbol_table_free(SymbolTable *st);

/// @brief Tamanho em bytes de um elemento de `BaseType` (offsets na stack e alinhamento).
/// @note Tamanho de **um** elemento — para um array, quem chama multiplica por `array_len`.
size_t tarm_symbol_table_data_size(BaseType type);

/// @brief Constrói um `DataType` escalar (não-array) para `base`, com `size_of` já resolvido via
/// `tarm_symbol_table_data_size`.
/// @note Fica aqui, e não em `types.h`, porque depende de `tarm_symbol_table_data_size` — `types.h`
/// é só vocabulário, sem lógica.
static inline DataType tarm_datatype_of(BaseType base) {
    DataType t = {.type      = base,
                  .size_of   = tarm_symbol_table_data_size(base),
                  .is_array  = false,
                  .array_len = 0};
    return t;
}

/// @brief Busca uma variável pelo nome.
/// @return Ponteiro para o `Symbol`, ou `NULL` se não existir.
/// @note Preferível a `exists` + `lookup`/`type` quando a variável pode não existir: resolve a
/// busca uma vez só e devolve tudo de uma vez.
/// @warning O ponteiro é invalidado por qualquer `declare` posterior, que pode realocar o vetor.
/// Use-o antes da próxima declaração, ou copie o `Symbol`.
const Symbol *tarm_symbol_table_find(const SymbolTable *st, const char *name, uint32_t name_len);

/// @brief Indica se já existe uma variável com o nome dado.
bool tarm_symbol_table_exists(const SymbolTable *st, const char *name, uint32_t name_len);

/// @brief Offset (relativo a `%rbp`) da variável.
/// @return O offset, ou -1 se a variável não existir na tabela.
int tarm_symbol_table_lookup(const SymbolTable *st, const char *name, uint32_t name_len);

/// @brief Registra uma variável local, reservando espaço na stack conforme `type`.
/// @param size Tamanho a reservar; passe 0 para usar o tamanho natural do `DataType`.
/// @param out_offset Recebe o offset atribuído. Pode ser `NULL`.
/// @return `false` se o nome já existir no escopo ou se a realocação falhar.
bool tarm_symbol_table_declare(SymbolTable *st, const char *name, uint32_t name_len, DataType type,
                               size_t size, int *out_offset);

/// @brief Registra uma variável global, atribuindo o próximo rótulo `globobj_N`.
/// @param out_label_id Recebe o id do rótulo. Pode ser `NULL`.
/// @return `false` se o nome já existir no escopo ou se a realocação falhar.
bool tarm_symbol_table_declare_global(SymbolTable *st, const char *name, uint32_t name_len,
                                      DataType type, size_t size, size_t *out_label_id);

/// @brief Tipo declarado da variável.
/// @return `Int64` se a variável não existir na tabela.
DataType tarm_symbol_table_type(const SymbolTable *st, const char *name, uint32_t name_len);

/// @brief Comprimento em bytes armazenado para a variável (relevante para String).
/// @return -1 se a variável não existir.
int tarm_symbol_table_string_length(const SymbolTable *st, const char *name, uint32_t name_len);

/// @brief Id do rótulo `globobj_N` de uma variável global.
/// @param out_id Recebe o id.
/// @return `false` se a variável não existir ou não for global — sem exceção, é quem chama que
/// decide o que fazer (ver errors.h).
bool tarm_symbol_table_label_id(const SymbolTable *st, const char *name, uint32_t name_len,
                                size_t *out_id);

/// @brief Total de bytes reservados na stack no frame corrente, arredondado para o próximo
/// múltiplo de 16 (alinhamento de stack da ABI).
/// @note Ainda sem chamador: o `subq` do prólogo precisa ser emitido **antes** do corpo, e é lá
/// que as variáveis locais são declaradas. A Codegen resolve isso contando os slots numa varredura
/// prévia da subárvore (`count_slots`); o Tarmac em C++ resolvia gerando o corpo para um buffer e
/// montando o cabeçalho depois. Esta função serve a essa segunda abordagem, se ela for adotada.
int tarm_symbol_table_total_bytes(const SymbolTable *st);

/// @brief Abre o escopo de uma função: zera o offset de frame e devolve a marca do topo da tabela.
/// @return Marca a ser passada a `tarm_symbol_table_scope_end`.
/// @details No Tarmac em C++ cada função ganhava uma `SymbolTable` própria (um *frame*). Aqui a
/// tabela é única e o escopo é uma **marca de pilha**: tudo que for declarado depois dela pertence
/// à função, e some quando ela termina. Os globais, declarados antes de qualquer função, ficam
/// abaixo da marca e continuam visíveis.
/// @see docs/architecture.md#duas-tabelas-e-por-que-são-separadas
size_t tarm_symbol_table_scope_begin(SymbolTable *st);

/// @brief Fecha o escopo aberto por `tarm_symbol_table_scope_begin`.
/// @param mark Valor devolvido pela chamada correspondente.
/// @details Descarta as entradas locais e devolve o offset de frame ao início, de modo que a
/// função seguinte comece do zero e possa reutilizar os mesmos nomes.
void tarm_symbol_table_scope_end(SymbolTable *st, size_t mark);

#endif
