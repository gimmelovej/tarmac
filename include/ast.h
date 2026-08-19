// ================================================================================================
// File: ast.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Nós da árvore sintática (AST) e a lista temporária usada para montá-los.
/// @details Um nó é uma struct única com uma *tag* (`ExprKind`) e uma `union` — a forma em C do
/// que, no Tarmac em C++, era uma hierarquia de classes. Sem herança, o nó vira dado puro: quem
/// percorre a árvore despacha com `switch (e->kind)` e lê o membro da `union` correspondente à tag.
/// @note Os nós vivem na `Arena` da compilação e não são liberados um a um: a árvore inteira morre
/// junto com a arena.
/// @see docs/architecture.md#a-ast-como-união-etiquetada

#ifndef TARM_AST_H
#define TARM_AST_H

#include "types.h"
#include "arena.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/// @brief Tag do nó: diz qual membro da `union` de `Expr` é o válido.
/// @warning Ler um membro que não corresponde à tag é comportamento indefinido — a `union` não
/// guarda qual campo foi escrito, é a tag que faz esse papel.
/// @note Um `ExprKind` novo precisa ser tratado em **todos** os `switch` que percorrem a árvore
/// (`check_expr`, na análise semântica, e `gen_expr`, na geração de código); `-Wswitch` avisa, mas
/// não impede a build.
/// @note `ExprCast` é o único que o Parser nunca produz: quem o insere é a análise semântica, ao
/// encontrar uma conversão implícita permitida.
/// @note `ExprIndex` e `ExprArrayLit` são o suporte a array, **novo e em desenvolvimento** — ver
/// docs/parser.md#arrays-novo-e-em-desenvolvimento.
typedef enum {
    ExprInteger, ExprFloat, ExprBool, ExprString, ExprChar,
    ExprIdentifier, ExprBinary, ExprAssign,
    ExprVarDecl, ExprFuncDecl, ExprCall, ExprMethod,
    ExprConditional, ExprWhile, ExprReturn, ExprCast,
    ExprIndex, ExprArrayLit
} ExprKind;

/// @brief Conversão implícita representada por um nó `ExprCast`.
/// @details Todas partem de `Int`, que é o tipo dos literais inteiros — alargar a partir dele é o
/// único caso em que a conversão não perde informação de forma surpreendente. `IntToChar` é a
/// exceção que estreita, e por isso a faixa do literal é conferida antes, na análise semântica.
typedef enum{
    IntToBool,
    IntToFloat,
    IntToInt64,
    IntToChar
} CastKind;

/// @brief Operador de um nó `ExprBinary`.
/// @note A precedência **não** está aqui: ela é decidida pela cadeia de produções do Parser
/// (`parse_equality` → `parse_relational` → ...), que já entrega a árvore com o agrupamento certo.
typedef enum {
    OpAdd, OpSub, OpMul, OpDiv,
    OpEq, OpGt, OpLt, OpGtEq, OpLtEq,
} BinaryOp;

typedef struct Expr Expr;

/// @brief Um nó da árvore: a tag, a origem no código-fonte e a `union` com os dados da construção.
/// @details `line`/`col` vêm do token que abriu a construção e viajam com o nó — é o que permite a
/// uma fase posterior apontar a posição de um erro sem voltar à lista de tokens. (No Tarmac em C++
/// a AST não carregava origem, e só léxico e sintaxe conseguiam citar linha/coluna.)
/// @note Todo nome ou texto guardado aqui é uma **fatia** do buffer de código-fonte (`ponteiro` +
/// `len`), como nos tokens: o buffer precisa viver enquanto a árvore viver.
struct Expr {
    ExprKind kind;
    DataType type;
    uint32_t line, col;

    union {
        struct { int64_t value; }                    integer;
        /// Texto original do literal, não um `double`: a conversão fica para a geração de código,
        /// que é quem decide o formato da constante emitida.
        struct { const char *text; uint32_t len; }   float_lit;
        struct { bool value; }                       boolean;
        struct { const char *text; uint32_t len; }   string_lit;
        struct { char value; }                       char_lit;
        struct { const char *name; uint32_t len; }   identifier;

        /// Inicializador de array (`{ 1, 2, 3 }`). Só aparece à direita de uma declaração — não é
        /// uma expressão de primeira classe, e a análise semântica confere a contagem e o tipo de
        /// cada elemento contra o tipo declarado.
        struct { Expr **elements; size_t count; }    array_lit;

        struct { BinaryOp op; Expr *left, *right; }  binary;

        /// `obj` é um nó `ExprIdentifier` — e não um par nome/comprimento — para que a declaração
        /// carregue posição própria e possa, no futuro, receber alvos mais complexos sem mudar a
        /// forma do nó.
        struct {
            Expr       *obj;
            DataType    type;
            Expr       *initializer;   ///< NULL se sem valor inicial.
            FrameType   frame;         ///< `Global` fora de função, `Local` dentro dela.
        } var_decl;

        /// Indexação `base[index]`. Encadeia à esquerda (`a[i][j]` é `(a[i])[j]`), embora array de
        /// array ainda não exista.
        struct {
            Expr *base;
            Expr *index;
        } index;

        /// `target` é um `ExprIdentifier` ou um `ExprIndex` — a mesma razão de `var_decl.obj`: o
        /// alvo é um nó, não um nome, e é o Parser que restringe quais formas são atribuíveis.
        struct {
            Expr       *target;
            Expr       *value;
        } assign;

        /// Parâmetros são nós `ExprVarDecl` (mesma produção de uma declaração local); `body` é o
        /// bloco da função. Os dois vetores vivem na arena, montados por `ast_list_commit`.
        struct {
            const char *name; uint32_t name_len;
            DataType    ret_type;
            Expr      **params; size_t param_count;
            Expr      **body;   size_t body_count;
        } func_decl;

        /// Compartilhada por `ExprCall` e `ExprMethod`. No método, o receptor é `args[0]` e os
        /// argumentos explícitos vêm depois.
        struct {
            const char *name; uint32_t name_len;
            Expr      **args; size_t arg_count;
        } call;

        /// `else_body` é NULL e `else_count` é 0 quando não há `else`.
        struct {
            Expr  *cond;
            Expr **then_body; size_t then_count;
            Expr **else_body; size_t else_count;
        } conditional;

        struct {
            Expr  *cond;
            Expr **body; size_t body_count;
        } while_loop;

        /// Inserida pela análise semântica em volta do nó original, que vira `operand` — é por isso
        /// que a travessia recebe `Expr **`: sem o endereço do campo do pai, não haveria como
        /// substituir o nó no lugar em que ele está guardado.
        struct {
            CastKind castKind;
            Expr *operand;
        } cast;

        struct { Expr *value; } ret;   ///< `value` é NULL num `return;` sem expressão.
    } as;
};

/// @brief Vetor dinâmico de nós, usado **durante** a análise sintática.
/// @details Uma produção não sabe de antemão quantos filhos vai reconhecer, e a arena não permite
/// realocar o que já entregou. Então a lista cresce no heap comum (`realloc`, dobrando a
/// capacidade) e só no fim é copiada para a arena por `ast_list_commit`, que já devolve o
/// temporário ao sistema. Inicializar sempre com `{0}`.
/// @see docs/architecture.md#a-ast-como-união-etiquetada
typedef struct {
    Expr **data;
    size_t count, capacity;
} ExprList;

/// @brief Acrescenta um nó ao fim da lista, crescendo-a se necessário.
/// @return `false` se a realocação falhar; nesse caso a lista continua íntegra e liberável.
bool ast_list_push(ExprList *l, Expr *e);

/// @brief Libera o vetor temporário e devolve a lista ao estado vazio.
/// @note Não toca nos nós apontados: eles são da arena.
void ast_list_free(ExprList *l);

/// @brief Copia a lista para a arena e libera o vetor temporário.
/// @param out_count Recebe o número de itens copiados.
/// @return Vetor de nós alocado na arena, ou NULL se a lista estava vazia.
/// @note Consome `l`: ao voltar, a lista está vazia — chamar `ast_list_free` de novo é inofensivo,
/// mas desnecessário.
/// @warning Uma falha de alocação na arena também devolve NULL, e `*out_count` já foi preenchido
/// com a contagem original — o chamador não distingue "lista vazia" de "faltou memória".
Expr **ast_list_commit(Arena *a, ExprList *l, size_t *out_count);

/// @brief Aloca um nó na arena e preenche a tag e a posição de origem.
/// @param line Linha do token que abriu a construção.
/// @param col Coluna do token que abriu a construção.
/// @return Nó recém-criado, ou NULL se a arena não tiver memória.
/// @warning O membro `as` **não** vem zerado (a arena não limpa o que entrega): quem cria o nó
/// precisa escrever todos os campos da variante que serão lidos depois — inclusive os "vazios",
/// como `else_body`/`else_count` de um `if` sem `else`.
Expr *ast_expr_new(Arena *a, ExprKind kind, uint32_t line, uint32_t col);

#endif
