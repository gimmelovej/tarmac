// ================================================================================================
// File: types.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Vocabulário compartilhado pelas etapas do pipeline: `TokenKind`, `Token`, `TokenList`,
/// `BaseType`, `DataType` e `FrameType`.
/// @details Vive separado de `lexer.h` e de `ast.h` para que cada etapa — Parser, análise semântica
/// e geração de código — enxergue esses tipos sem arrastar junto o estado de quem os produz.
/// @see docs/architecture.md#tokens-como-fatias-do-buffer-de-origem

#ifndef TARM_TYPES_H
#define TARM_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/// @brief Classificação de um lexema reconhecido pelo Lexer.
/// @note A ordem dos membros não tem significado — nada indexa este `enum`. O que importa é que
/// todo caso novo seja tratado nos `switch` que despacham sobre ele (`-Wswitch` avisa, mas não
/// impede a build).
typedef enum
{
    // Literais e nomes
    LiteralInteger,
    LiteralFloat,
    LiteralString,
    LiteralChar,
    Identifier,

    // Palavras-chave
    KwReturn,
    KwBuffer,
    KwChar,
    KwInt,
    KwInt64,
    KwFloat,
    KwBool,
    KwTrue,
    KwFalse,
    KwString,
    KwIf,
    KwElse,
    KwWhile,
    KwFunction,
    KwDeclaration,

    // Pontuação e operadores
    Comma,
    Plus,
    PlusEqual,  ///< `+=` — os compostos não têm nó próprio: o Parser os desfaz em `a = a + b`
    Minus,
    MinusEqual, ///< `-=`
    Star,
    StarEqual,  ///< `*=`
    Slash,
    SlashEqual, ///< `/=`
    Equal,
    EqualEqual,
    LParen,
    RParen,
    LBrace,
    RBrace,
    Semicolon,

    EndOfFile, ///< Sempre o último token de uma varredura bem-sucedida.
    Invalid,   ///< Lexema não reconhecido; o erro correspondente já foi registrado no `Diagnostics`.

    LBracket,     ///< `[` — abre o tamanho de um array ou uma indexação
    RBracket,     ///< `]`
    Greater,      ///< `>`
    Less,         ///< `<`
    GreaterEqual, ///< `>=`
    LessEqual,    ///< `<=`
    Dot           ///< `.` — chamada de método

    // As aspas não têm membro próprio de propósito: `scan_string`/`scan_char` as consomem como
    // delimitador e o token guarda apenas o conteúdo.
} TokenKind;

/// @brief Unidade léxica reconhecida no código-fonte.
/// @note `start`/`len` são uma **fatia** do buffer do arquivo, não uma cópia: o token só é válido
/// enquanto esse buffer viver (é o equivalente em C do `std::string_view` usado no Tarmac em C++).
/// Quem lê o texto precisa respeitar `len` — a fatia não termina em `'\0'`.
/// @note `line`/`col` marcam o **início** do lexema, e é dali que saem as posições nas mensagens de
/// erro do Lexer e do Parser.
typedef struct
{
    TokenKind kind;
    const char *start; ///< Primeiro byte do lexema, dentro do buffer de origem.
    uint32_t len;      ///< Comprimento do lexema em bytes.
    uint32_t line, col;
} Token;

/// @brief Vetor dinâmico de tokens, crescido por dobra de capacidade.
/// @details Inicializar sempre com `{0}`: o crescimento parte de `data == NULL`, e é `capacity`
/// que distingue "vazio" de "cheio". Liberar com `tarm_lexer_tokens_free`.
typedef struct
{
    Token *data;
    size_t count;    ///< Tokens efetivamente armazenados.
    size_t capacity; ///< Slots alocados em `data`.
} TokenList;

/// @brief Categoria base de um valor na linguagem Tarmac.
/// @details É o que responde "de que espécie é este valor", sem dizer nada sobre a forma. A forma
/// — escalar ou array — vive em `DataType`, que envolve este `enum`.
/// @note `Void` representa ausência de valor (ex.: retorno de `print`) e não é declarável.
/// @note Um array **não** tem categoria própria: `int[3]` guarda `Int` aqui, e o que o distingue de
/// um `int` é o `is_array` do `DataType`. Isso mantém as máscaras de tipo aceito da `FunctionTable`
/// indexadas por uma dimensão só.
typedef enum
{
    Char,
    Int,
    Int64,
    Float,
    Bool,
    String,
    Void
} BaseType;

/// @brief Tipo completo de um valor: a categoria base do elemento mais a forma, quando é um array.
/// @details Separar `BaseType` de `DataType` é o que evita duplicar cada tipo da linguagem numa
/// versão "array de": a categoria fica numa dimensão e a forma na outra, e quem só se importa com
/// a categoria (o despacho de `print`, por exemplo) lê apenas `type`.
/// @note `type` é sempre o tipo do **elemento** (`Int` em `int[3] y`), nunca uma categoria "array".
/// @note `size_of` é o tamanho de **um** elemento; o espaço total de um array é
/// `size_of * array_len`, calculado por quem reserva o slot.
/// @note Construir com `tarm_datatype_of` (symbol_table.h) para um escalar — ele já resolve
/// `size_of` e zera a parte de array.
/// @warning Suporte a array é **novo e em desenvolvimento**; ver as limitações conhecidas em
/// docs/parser.md#arrays-novo-e-em-desenvolvimento.
typedef struct
{
    BaseType type;
    size_t   size_of;
    bool     is_array;
    size_t  array_len;
} DataType;

/// @brief Escopo em que uma variável é declarada, definido pelo Parser e carregado no nó
/// `ExprVarDecl` (`as.var_decl.frame`): `Global` (nível superior, vira dado estático em `.data`),
/// `Local` (dentro de uma função, vira slot na stack) ou `External` (reservado, ainda não usado).
/// @see docs/parser.md#declarações-de-variável
typedef enum
{
    External,
    Global,
    Local
} FrameType;

#endif
