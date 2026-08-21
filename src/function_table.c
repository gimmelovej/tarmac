// ================================================================================================
// File: function_table.c
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
#include "function_table.h"
#include "errors.h"

#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
// Internos
// ------------------------------------------------------------------------------------------------

// O teste de comprimento vem primeiro: é ele que impede tanto ler além da fatia quanto casar
// "print" com "printf".
static bool slice_eq(const char *a, uint32_t a_len, const char *b, uint32_t b_len) {
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

// Cresce por dobra de capacidade, como a TokenList e a SymbolTable. Devolve o slot novo, zerado e
// já contabilizado.
static FunctionSignature *entry_push(FunctionTable *ft) {
    if (ft->count == ft->capacity) {
        size_t             new_capacity = (ft->capacity == 0) ? 8 : ft->capacity * 2;
        FunctionSignature *buffer       = realloc(ft->data, new_capacity * sizeof *ft->data);

        if (buffer == NULL) {
            tarm_system_error("Não foi possível realocar a tabela de funções");
            return NULL;
        }
        ft->data     = buffer;
        ft->capacity = new_capacity;
    }

    FunctionSignature *sig = &ft->data[ft->count++];
    memset(sig, 0, sizeof *sig);
    sig->dispatch_param = -1;
    return sig;
}

// Registro de uma nativa com rótulo único no runtime. O nome é um literal estático, então `strlen`
// aqui é seguro — ao contrário das fatias do buffer de origem, que não terminam em '\0'.
static FunctionSignature *register_native(FunctionTable *ft, const char *name, BaseType ret_type,
                                          const char *symbol) {
    FunctionSignature *sig = entry_push(ft);
    if (!sig) return NULL;

    sig->name      = name;
    sig->name_len  = (uint32_t)strlen(name);
    sig->ret_type  = ret_type;
    sig->is_native = true;
    sig->symbol    = symbol;
    sig->receiver  = Void;
    return sig;
}

// Um parâmetro de tipo exato, na próxima posição livre.
static void add_param(FunctionSignature *sig, BaseType type) {
    if (sig->param_count >= TARM_MAX_PARAMS) return;
    sig->params[sig->param_count++] = type;
}

// ------------------------------------------------------------------------------------------------
// Ciclo de vida
// ------------------------------------------------------------------------------------------------

void tarm_function_table_init(FunctionTable *ft) {
    ft->data     = NULL;
    ft->count    = 0;
    ft->capacity = 0;
}

void tarm_function_table_free(FunctionTable *ft) {
    free(ft->data);
    ft->data     = NULL;
    ft->count    = 0;
    ft->capacity = 0;
}

// ------------------------------------------------------------------------------------------------
// Registro
// ------------------------------------------------------------------------------------------------

// As embutidas da linguagem. Cada uma aponta para um rótulo real de `runtime/*.s`: é essa ligação
// que a Codegen segue ao emitir a chamada, em vez de conhecer os nomes por conta própria.
//
// Acrescentar uma nativa é acrescentar um bloco aqui **e** a rotina correspondente na runtime.
bool tarm_function_table_register_natives(FunctionTable *ft) {
    // `print` é o único caso de despacho por tipo: um nome na linguagem, cinco rotinas no runtime.
    // A lista de tipos aceitos precisa cobrir todas as chaves do mapa abaixo — a semântica valida o
    // argumento contra ela, e só então a Codegen escolhe o rótulo.
    FunctionSignature *print = register_native(ft, "print", Void, NULL);
    if (!print) return false;
    print->is_variadic         = true;
    print->dispatch_param      = 0;
    print->param_count         = 1;
    print->accepted[0]         = TARM_TYPE_BIT(Int) | TARM_TYPE_BIT(Int64) | TARM_TYPE_BIT(Float) |
                                 TARM_TYPE_BIT(Bool) | TARM_TYPE_BIT(Char) | TARM_TYPE_BIT(String);
    print->symbol_by_type[Int] = "tarm_print_int";
    print->symbol_by_type[Int64]  = "tarm_print_int";
    print->symbol_by_type[Float]  = "tarm_print_float";
    print->symbol_by_type[Bool]   = "tarm_print_bool";
    print->symbol_by_type[Char]   = "tarm_print_char";
    print->symbol_by_type[String] = "tarm_print_str";

    // Converte texto decimal em inteiro; para no primeiro byte que não é dígito.
    FunctionSignature *sig = register_native(ft, "atoi", Int, "tarm_atoi");
    if (!sig) return false;
    add_param(sig, String);

    // Alocação. `brk` mantém um heap linear (liberar um endereço libera tudo acima dele); `mmap`
    // mapeia regiões independentes. Ver docs/runtime.md#heap-brk-linear.
    sig = register_native(ft, "mmap_alloc", Int64, "tarm_mmap_alloc");
    if (!sig) return false;
    add_param(sig, Int);

    sig = register_native(ft, "mmap_free", Void, "tarm_mmap_free");
    if (!sig) return false;
    add_param(sig, Int64);
    add_param(sig, Int);

    sig = register_native(ft, "brk_alloc", Int64, "tarm_brk_alloc");
    if (!sig) return false;
    add_param(sig, Int);

    sig = register_native(ft, "brk_free", Void, "tarm_brk_free");
    if (!sig) return false;
    add_param(sig, Int64);

    // Onda quadrada PCM em stdout. Ver docs/runtime.md#audio-emit_note.
    sig = register_native(ft, "emit_note", Void, "tarm_emit_note");
    if (!sig) return false;
    add_param(sig, Int);
    add_param(sig, Int);
    add_param(sig, Int);

    // Método: o receptor não conta em `param_count` — ele entra como `args[0]` no nó de chamada,
    // por convenção do Parser (ver `parse_postfix`).
    sig = register_native(ft, "len", Int64, "tarm_obj_len");
    if (!sig) return false;
    sig->is_method = true;
    sig->receiver  = String;

    return true;
}

bool tarm_function_table_declare(FunctionTable *ft, const char *name, uint32_t name_len,
                                 BaseType ret_type, const BaseType *params, size_t param_count,
                                 const char **out_reason) {
    if (param_count > TARM_MAX_PARAMS) {
        if (out_reason) *out_reason = "parâmetros demais";
        return false;
    }

    if (tarm_function_table_find(ft, name, name_len) != NULL) {
        if (out_reason) *out_reason = "função já declarada";
        return false;
    }

    FunctionSignature *sig = entry_push(ft);
    if (!sig) {
        if (out_reason) *out_reason = "memória insuficiente";
        return false;
    }

    sig->name        = name;
    sig->name_len    = name_len;
    sig->ret_type    = ret_type;
    sig->param_count = param_count;
    sig->receiver    = Void;

    for (size_t i = 0; i < param_count; i++)
        sig->params[i] = params[i];

    return true;
}

// ------------------------------------------------------------------------------------------------
// Consultas
// ------------------------------------------------------------------------------------------------

// Busca linear: são poucas dezenas de funções num programa típico, e o custo de manter um índice
// não se paga. Se o perfil acusar, é aqui que ele entra, sem mexer na API.
const FunctionSignature *tarm_function_table_find(const FunctionTable *ft, const char *name,
                                                  uint32_t name_len) {
    for (size_t i = 0; i < ft->count; i++) {
        const FunctionSignature *sig = &ft->data[i];
        if (!sig->is_method && slice_eq(sig->name, sig->name_len, name, name_len)) return sig;
    }
    return NULL;
}

const FunctionSignature *tarm_function_table_find_method(const FunctionTable *ft, const char *name,
                                                         uint32_t name_len, BaseType receiver) {
    for (size_t i = 0; i < ft->count; i++) {
        const FunctionSignature *sig = &ft->data[i];
        if (sig->is_method && sig->receiver == receiver &&
            slice_eq(sig->name, sig->name_len, name, name_len))
            return sig;
    }
    return NULL;
}

bool tarm_function_table_exists(const FunctionTable *ft, const char *name, uint32_t name_len) {
    return tarm_function_table_find(ft, name, name_len) != NULL;
}

bool tarm_function_table_check_arity(const FunctionSignature *sig, size_t arg_count) {
    if (sig->is_variadic) return true;
    return arg_count == sig->param_count;
}

// Três formas de escrever a mesma coisa, resolvidas num lugar só para que a semântica não repita a
// distinção em cada caso de chamada.
uint32_t tarm_function_table_accepted_mask(const FunctionSignature *sig, size_t index) {
    if (sig->is_variadic) return sig->accepted[0];

    if (index >= sig->param_count) return 0;

    if (sig->accepted[index] != 0) return sig->accepted[index];

    return TARM_TYPE_BIT(sig->params[index]);
}

const char *tarm_function_table_symbol(const FunctionSignature *sig, BaseType dispatch_type) {
    if (!sig->is_native) return NULL; // função do usuário: o rótulo sai do nome, na Codegen

    if (sig->dispatch_param < 0) return sig->symbol;

    return sig->symbol_by_type[dispatch_type];
}
