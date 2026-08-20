// ================================================================================================
// File: parser.c
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
#include "parser.h"
#include "symbol_table.h"
#include <string.h>
#include <stdio.h>

// Declarações adiantadas: a cadeia de precedência é mutuamente recursiva (`primary` volta a
// `expression` pelos parênteses), então não há ordem de definição que dispense os protótipos.
static Expr *parse_top_level(Parser *ps);
static Expr *parse_function_declaration(Parser *ps);
static Expr *parse_global_declaration(Parser *ps);
static bool  parse_body_block(Parser *ps, Expr ***out_items, size_t *out_count);
static Expr *parse_declaration(Parser *ps);
static Expr *parse_statement(Parser *ps);
static Expr *parse_array_literal(Parser *ps);
static Expr *parse_expression(Parser *ps);
static Expr *parse_assignment(Parser *ps);
static Expr *parse_equality(Parser *ps);
static Expr *parse_relational(Parser *ps);
static Expr *parse_additive(Parser *ps);
static Expr *parse_multiplicative(Parser *ps);
static Expr *parse_postfix(Parser *ps);
static Expr *parse_primary(Parser *ps);
static bool parse_type(Parser *ps, DataType *out);


void tarm_parser_init(Parser *ps, const TokenList *toks, Diagnostics *diag, Arena *arena) {
    ps->tokens = *toks;
    ps->pos    = 0;
    ps->diag   = diag;
    ps->arena  = arena;
}

// ------------------------------------------------------------------------------------------------
// Primitivos de cursor. Nenhuma produção acessa `tokens.data` direto — é o que garante que
// ninguém leia além do fim.
// ------------------------------------------------------------------------------------------------

static Token peek(const Parser *ps) {
    if (ps->pos >= ps->tokens.count) {
        Token eof = {0};
        eof.kind = EndOfFile;
        return eof;
    }
    return ps->tokens.data[ps->pos];
}

static bool isAtEnd(const Parser *ps) {
    return peek(ps).kind == EndOfFile;
}

// Só é válido depois de ao menos um `advance`; o token zerado é rede contra bug de produção.
static Token previous(const Parser *ps) {
    if (ps->pos == 0) { Token none = {0}; return none; }
    return ps->tokens.data[ps->pos - 1];
}

static Token advance(Parser *ps) {
    if (!isAtEnd(ps)) ps->pos++;
    return previous(ps);
}

static bool check(const Parser *ps, TokenKind kind) {
    return peek(ps).kind == kind;
}

static bool match(Parser *ps, TokenKind kind) {
    if (!check(ps, kind)) return false;
    advance(ps);
    return true;
}

static bool expect(Parser *ps, TokenKind kind, const char *what) {
    if (check(ps, kind)) { advance(ps); return true; }
    Token t = peek(ps);
    tarm_error_at(ps->diag, t.line, t.col, "esperado %s", what);
    return false;
}

// ------------------------------------------------------------------------------------------------
// Auxiliares
// ------------------------------------------------------------------------------------------------


static bool match_type_kw(Parser *ps) {
    return match(ps, KwInt)  || match(ps, KwInt64) || match(ps, KwFloat)
        || match(ps, KwChar) || match(ps, KwString) || match(ps, KwBool);
}


static BaseType datatype_from_token(TokenKind k) {
    switch (k) {
        case KwChar:   return Char;
        case KwInt:    return Int;
        case KwInt64:  return Int64;
        case KwFloat:  return Float;
        case KwBool:   return Bool;
        case KwString: return String;
        default:       return Void;
    }
}

// Sem verificação de estouro: `99999999999999999999` dá a volta em silêncio. Pendência para a
// análise semântica, que é onde o intervalo do tipo declarado é conhecido.
static int64_t parse_int_slice(const char *s, uint32_t len) {
    int64_t v = 0;
    for (uint32_t i = 0; i < len; i++)
        v = v * 10 + (s[i] - '0');
    return v;
}

// Tipo de uma declaração: a palavra-chave e, opcionalmente, o `[N]` que a torna um array.
//
// O tamanho vem **antes** do nome (`int[3] v`), e não depois como em C. É o que deixa o tipo ser
// lido de uma vez só: quando `parse_declaration` chega ao identificador, já sabe tudo sobre a
// forma do valor.
//
// Ver docs/parser.md#arrays-novo-e-em-desenvolvimento.
static bool parse_type(Parser *ps, DataType *out){
    Token    kw = peek(ps);

    if(!match_type_kw(ps)) return false;

    *out = tarm_datatype_of(datatype_from_token(kw.kind));
    if(match(ps, LBracket)){
        if (!expect(ps, LiteralInteger, "o tamanho do array")) return false;
        out->is_array = true;
        out->array_len = (size_t)parse_int_slice(previous(ps).start, previous(ps).len);
        if (!expect(ps, RBracket, "']' ao fechar o tamanho do array")) return false;
    }

    return true;
}

// Resolve o conteúdo de um literal de caractere, que o Lexer entrega cru. As sequências aceitas são
// as que `is_valid_escape` (lexer.c) já validou, então aqui não há erro a reportar — só tradução.
static char decode_char_literal(const char *text, uint32_t len) {
    if (len == 0) return '\0';

    if (text[0] != '\\' || len < 2) return text[0];

    switch (text[1]) {
        case 'n':  return '\n';
        case 't':  return '\t';
        case 'r':  return '\r';
        case '0':  return '\0';
        case '\\': return '\\';
        case '"':  return '"';
        case '\'': return '\'';
        default:   return text[1];
    }
}

// Lista de argumentos entre parênteses, já consumido o '(' de abertura.
static bool parse_arg_list(Parser *ps, ExprList *args) {
    if (!check(ps, RParen)) {
        do {
            Expr *a = parse_expression(ps);
            if (!a || !ast_list_push(args, a)) return false;
        } while (match(ps, Comma));
    }
    return expect(ps, RParen, "')' para fechar a lista de argumentos");
}

// ------------------------------------------------------------------------------------------------
// Instruções — a cadeia que decide o que uma linha do programa *é*.
// Ver docs/parser.md#nível-superior.
// ------------------------------------------------------------------------------------------------

// Todo item de nível superior começa por um tipo: ou é declaração de função (tipo de retorno
// seguido de `function`), ou é variável global.
static Expr *parse_top_level(Parser *ps) {
    if (match_type_kw(ps)) {
        if (check(ps, KwFunction))
            return parse_function_declaration(ps);
        return parse_global_declaration(ps);
    }

    Token t = peek(ps);
    tarm_error_at(ps->diag, t.line, t.col,
                  "instrução de nível superior deve começar com um tipo "
                  "(declaração de função ou variável global); recebeu '%.*s'",
                  (int)t.len, t.start);
    return NULL;
}

// O tipo de retorno já foi consumido por `parse_top_level` e é lido de volta via `previous`.
static Expr *parse_function_declaration(Parser *ps) {
    Token    type_tok = previous(ps);
    DataType ret_type = tarm_datatype_of(datatype_from_token(type_tok.kind));

    advance(ps);   // consome KwFunction

    if (!expect(ps, Identifier, "o nome da função após 'function'")) return NULL;
    Token name = previous(ps);

    if (!expect(ps, LParen, "'(' após o nome da função")) return NULL;

    ExprList params = {0};
    if (!check(ps, RParen)) {
        do {
            Expr *p = parse_declaration(ps);
            if (!p || !ast_list_push(&params, p)) { ast_list_free(&params); return NULL; }
        } while (match(ps, Comma));
    }
    if (!expect(ps, RParen, "')' após os parâmetros")) { ast_list_free(&params); return NULL; }

    Expr *e = ast_expr_new(ps->arena, ExprFuncDecl, type_tok.line, type_tok.col);
    if (!e) { ast_list_free(&params); return NULL; }

    e->as.func_decl.name     = name.start;
    e->as.func_decl.name_len = name.len;
    e->as.func_decl.ret_type = ret_type;
    e->as.func_decl.params   = ast_list_commit(ps->arena, &params,
                                               &e->as.func_decl.param_count);

    if (!parse_body_block(ps, &e->as.func_decl.body, &e->as.func_decl.body_count))
        return NULL;

    return e;
}

// Gêmea de `parse_declaration`, e de propósito: a forma da declaração é a mesma, o que muda é o
// `frame` gravado no nó — `Global` vira dado estático, `Local` vira slot na stack. É esse campo
// que a análise semântica e a codegen vão consultar, não a produção que criou o nó.
static Expr *parse_global_declaration(Parser *ps) {
    Token    type_tok = previous(ps);
    DataType var_type = tarm_datatype_of(datatype_from_token(type_tok.kind));

    Expr *identifier_expr = parse_postfix(ps);
    if (!identifier_expr) return NULL;

    Expr *init = NULL;
    if (match(ps, Equal)) {
        init = parse_statement(ps);
        if (!init) return NULL;
    }

    Expr *e = ast_expr_new(ps->arena, ExprVarDecl, type_tok.line, type_tok.col);
    if (!e) return NULL;

    e->as.var_decl.obj         = identifier_expr;
    e->as.var_decl.type        = var_type;
    e->as.var_decl.initializer = init;
    e->as.var_decl.frame       = Global;
    return e;
}

// Bloco `{ ... }`. Devolve a lista pelos parâmetros de saída porque um bloco não é um nó — é o
// conteúdo de um (corpo de função, ramo de `if`, corpo de `while`).
static bool parse_body_block(Parser *ps, Expr ***out_items, size_t *out_count) {
    if (!expect(ps, LBrace, "'{' para abrir um bloco")) return false;

    ExprList list = {0};
    while (!check(ps, RBrace) && !isAtEnd(ps)) {
        if (match(ps, Semicolon)) continue;

        Expr *e = parse_declaration(ps);
        if (!e || !ast_list_push(&list, e)) { ast_list_free(&list); return false; }

        // Instrução terminada em '}' (if/while aninhado) dispensa o ';'.
        if (previous(ps).kind != RBrace)
            match(ps, Semicolon);
    }

    if (!expect(ps, RBrace, "'}' para fechar um bloco")) {
        ast_list_free(&list);
        return false;
    }

    *out_items = ast_list_commit(ps->arena, &list, out_count);
    return true;
}

// Declaração local, ou uma instrução comum se não começar por tipo.
static Expr *parse_declaration(Parser *ps) {

    DataType var_type;
    Token type_tok = peek(ps);
    if(!parse_type(ps, &var_type)) return parse_statement(ps);

    Expr *identifier_expr = parse_primary(ps);
    if (!identifier_expr) return NULL;

    Expr *init = NULL;
    if (match(ps, Equal)) {
        init = check(ps, LBrace) ? parse_array_literal(ps) : parse_expression(ps);        
        if (!init) return NULL;
    }

    Expr *e = ast_expr_new(ps->arena, ExprVarDecl, type_tok.line, type_tok.col);
    if (!e) return NULL;

    e->as.var_decl.obj         = identifier_expr;
    e->as.var_decl.type        = var_type;
    e->as.var_decl.initializer = init;
    e->as.var_decl.frame       = Local;
    return e;
}

// Instruções de controle de fluxo, com a expressão comum como caso padrão.
//
// Os parênteses da condição são opcionais sem nenhum tratamento especial: a cadeia de expressão
// para sozinha no `{`, porque nenhuma produção casa com `LBrace`. Quando eles aparecem, entram por
// `parse_primary`, como agrupamento comum.
static Expr *parse_statement(Parser *ps) {
    Token t = peek(ps);

    if (match(ps, KwIf)) {
        Expr *cond = parse_expression(ps);
        if (!cond) return NULL;

        Expr *e = ast_expr_new(ps->arena, ExprConditional, t.line, t.col);
        if (!e) return NULL;
        e->as.conditional.cond       = cond;
        e->as.conditional.else_body  = NULL;
        e->as.conditional.else_count = 0;

        if (!parse_body_block(ps, &e->as.conditional.then_body,
                                  &e->as.conditional.then_count))
            return NULL;

        if (match(ps, KwElse)) {
            if (!parse_body_block(ps, &e->as.conditional.else_body,
                                      &e->as.conditional.else_count))
                return NULL;
        }
        return e;
    }

    if (match(ps, KwWhile)) {
        Expr *cond = parse_expression(ps);
        if (!cond) return NULL;

        Expr *e = ast_expr_new(ps->arena, ExprWhile, t.line, t.col);
        if (!e) return NULL;
        e->as.while_loop.cond = cond;

        if (!parse_body_block(ps, &e->as.while_loop.body,
                                  &e->as.while_loop.body_count))
            return NULL;
        return e;
    }

    if (match(ps, KwReturn)) {
        Expr *value = NULL;
        if (!check(ps, Semicolon)) {
            value = parse_expression(ps);
            if (!value) return NULL;
        }

        Expr *e = ast_expr_new(ps->arena, ExprReturn, t.line, t.col);
        if (!e) return NULL;
        e->as.ret.value = value;
        return e;
    }

    return parse_expression(ps);
}

// Inicializador de array: `{ 1, 2, 3 }`. Só é chamado por `parse_declaration`, quando o `=` é
// seguido de `{` — um literal de array não é expressão de primeira classe, então não entra na
// cadeia de precedência.
//
static Expr *parse_array_literal(Parser *ps){
    if (!match(ps, LBrace)) return NULL;
    Token open = previous(ps);
    ExprList items = {0};
    if(!check(ps, RBrace)){
        do {
            Expr *a = parse_expression(ps);
            if (!a || !ast_list_push(&items, a)){
                ast_list_free(&items); return NULL;;
            }
        } while (match(ps, Comma));
    }
    
    if (!expect(ps, RBrace, "'}' ao fechar o inicializador")) {
        ast_list_free(&items); return NULL;
    }

    Expr *e = ast_expr_new(ps->arena, ExprArrayLit, open.line, open.col);
    if (!e) { ast_list_free(&items); return NULL; }
    e->as.array_lit.elements = ast_list_commit(ps->arena, &items, &e->as.array_lit.count);
    return e;
}

// ------------------------------------------------------------------------------------------------
// Expressões, da menor para a maior precedência. A precedência não está guardada em lugar nenhum:
// ela *é* a ordem em que as produções se chamam, e cada nível só agrupa o que o de cima deixou.
// Ver docs/parser.md#expressões-e-precedência.
// ------------------------------------------------------------------------------------------------

static Expr *parse_expression(Parser *ps) {
    return parse_assignment(ps);
}

// Associativa à direita: `a = b = c` vira `a = (b = c)`.
static Expr *parse_assignment(Parser *ps) {
    Expr *expr = parse_equality(ps);
    if (!expr) return NULL;

    if (match(ps, Equal)) {
        Expr *value = parse_assignment(ps);
        if (!value) return NULL;
        if (expr->kind != ExprIdentifier && expr->kind != ExprIndex) {
            tarm_error_at(ps->diag, expr->line, expr->col,
                        "alvo de atribuição inválido "
                        "(esperava um identificador à esquerda de '=')");
            return NULL;
        }

        Expr *a = ast_expr_new(ps->arena, ExprAssign, expr->line, expr->col);
        if (!a) return NULL;
        a->as.assign.target = expr;
        a->as.assign.value  = value;
        return a;
    }
    return expr;
}

// Fabrica o nó binário e reaproveita `left` como acumulador — é o que dá associatividade à
// esquerda a toda a cadeia abaixo.
static Expr *make_binary(Parser *ps, BinaryOp op, Expr *left, Expr *right, Token t) {
    Expr *e = ast_expr_new(ps->arena, ExprBinary, t.line, t.col);
    if (!e) return NULL;
    e->as.binary.op    = op;
    e->as.binary.left  = left;
    e->as.binary.right = right;
    return e;
}

static Expr *parse_equality(Parser *ps) {
    Expr *left = parse_relational(ps);
    if (!left) return NULL;

    while (check(ps, EqualEqual)) {
        Token t = advance(ps);
        Expr *right = parse_relational(ps);
        if (!right) return NULL;
        left = make_binary(ps, OpEq, left, right, t);
        if (!left) return NULL;
    }
    return left;
}

static Expr *parse_relational(Parser *ps) {
    Expr *left = parse_additive(ps);
    if (!left) return NULL;

    for (;;) {
        BinaryOp op;
        Token    t = peek(ps);

        if      (check(ps, Greater))      op = OpGt;
        else if (check(ps, GreaterEqual)) op = OpGtEq;
        else if (check(ps, Less))      op = OpLt;
        else if (check(ps, LessEqual)) op = OpLtEq;
        else break;

        advance(ps);
        Expr *right = parse_additive(ps);
        if (!right) return NULL;
        left = make_binary(ps, op, left, right, t);
        if (!left) return NULL;
    }
    return left;
}

static Expr *parse_additive(Parser *ps) {
    Expr *left = parse_multiplicative(ps);
    if (!left) return NULL;

    for (;;) {
        BinaryOp op;
        Token    t = peek(ps);

        if      (check(ps, Plus))  op = OpAdd;
        else if (check(ps, Minus)) op = OpSub;
        else break;

        advance(ps);
        Expr *right = parse_multiplicative(ps);
        if (!right) return NULL;
        left = make_binary(ps, op, left, right, t);
        if (!left) return NULL;
    }
    return left;
}

static Expr *parse_multiplicative(Parser *ps) {
    Expr *left = parse_postfix(ps);
    if (!left) return NULL;

    for (;;) {
        BinaryOp op;
        Token    t = peek(ps);

        if      (check(ps, Star))  op = OpMul;
        else if (check(ps, Slash)) op = OpDiv;
        else break;

        advance(ps);
        Expr *right = parse_postfix(ps);
        if (!right) return NULL;
        left = make_binary(ps, op, left, right, t);
        if (!left) return NULL;
    }
    return left;
}

// Sufixos que se aplicam a um valor já reconhecido: indexação (`v[i]`) e chamada de método
// (`x.len()`). No método, o receptor entra como `args[0]`, seguido dos argumentos explícitos — é
// essa convenção que a análise semântica e a codegen esperam.
//
// Os dois são laços, então encadeiam (`a[i][j]`, `a.b().c()`), mas não se misturam: o `if/else`
// escolhe um dos dois caminhos, e `v[0].len()` não é reconhecido.
static Expr *parse_postfix(Parser *ps) {
    Expr *receiver = parse_primary(ps);
    if (!receiver) return NULL;
    if(check(ps, LBracket)){
        while (match(ps, LBracket))
        {
            Token open = previous(ps);

            Expr *index  = parse_expression(ps);
            if(!index) return NULL;

            if (!expect(ps, RBracket, "']' ao fechar uma indexação")) return NULL;
            
            Expr *e = ast_expr_new(ps->arena, ExprIndex, open.line, open.col);
            if (!e) return NULL;

            e->as.index.base = receiver;
            e->as.index.index = index;
            receiver = e;
        }   
    } else if(check(ps, Dot)){
        while (match(ps, Dot)) {
            if (!expect(ps, Identifier, "o nome do método após '.'")) return NULL;
            Token name = previous(ps);

            if (!expect(ps, LParen, "'(' ao abrir um método")) return NULL;

            ExprList args = {0};
            if (!ast_list_push(&args, receiver)) { ast_list_free(&args); return NULL; }
            if (!parse_arg_list(ps, &args))      { ast_list_free(&args); return NULL; }

            Expr *e = ast_expr_new(ps->arena, ExprMethod, name.line, name.col);
            if (!e) { ast_list_free(&args); return NULL; }

            e->as.call.name     = name.start;
            e->as.call.name_len = name.len;
            e->as.call.args     = ast_list_commit(ps->arena, &args, &e->as.call.arg_count);
            receiver = e;
        }
    }
    return receiver;
}

// Folhas da árvore: literais, identificador, chamada e o agrupamento por parênteses.
static Expr *parse_primary(Parser *ps) {
    Token t = peek(ps);

    if (match(ps, Identifier)) {
        // Identificador seguido de '(' é chamada de função; caso contrário, referência.
        if (match(ps, LParen)) {
            ExprList args = {0};
            if (!parse_arg_list(ps, &args)) { ast_list_free(&args); return NULL; }

            Expr *e = ast_expr_new(ps->arena, ExprCall, t.line, t.col);
            if (!e) { ast_list_free(&args); return NULL; }

            e->as.call.name     = t.start;
            e->as.call.name_len = t.len;
            e->as.call.args     = ast_list_commit(ps->arena, &args, &e->as.call.arg_count);
            return e;
        }

        Expr *e = ast_expr_new(ps->arena, ExprIdentifier, t.line, t.col);
        if (!e) return NULL;
        e->as.identifier.name = t.start;
        e->as.identifier.len  = t.len;
        return e;
    }

    if (match(ps, LiteralInteger)) {
        Expr *e = ast_expr_new(ps->arena, ExprInteger, t.line, t.col);
        if (!e) return NULL;
        e->as.integer.value = parse_int_slice(t.start, t.len);
        return e;
    }

    // O texto original é preservado até a codegen, onde vira constante em `.rodata`.
    if (match(ps, LiteralFloat)) {
        Expr *e = ast_expr_new(ps->arena, ExprFloat, t.line, t.col);
        if (!e) return NULL;
        e->as.float_lit.text = t.start;
        e->as.float_lit.len  = t.len;
        return e;
    }

    if (match(ps, LiteralString)) {
        Expr *e = ast_expr_new(ps->arena, ExprString, t.line, t.col);
        if (!e) return NULL;
        e->as.string_lit.text = t.start;
        e->as.string_lit.len  = t.len;
        return e;
    }

    // O Lexer valida a sequência de escape mas guarda a fatia crua (`'\n'` chega como `\` + `n`),
    // para não precisar de armazenamento próprio. Um caractere cabe num byte do nó, então a
    // decodificação acontece aqui; a de string fica para a Codegen, que a repassa ao `as`.
    if (match(ps, LiteralChar)) {
        Expr *e = ast_expr_new(ps->arena, ExprChar, t.line, t.col);
        if (!e) return NULL;
        e->as.char_lit.value = decode_char_literal(t.start, t.len);
        return e;
    }

    if (match(ps, KwTrue) || match(ps, KwFalse)) {
        Expr *e = ast_expr_new(ps->arena, ExprBool, t.line, t.col);
        if (!e) return NULL;
        e->as.boolean.value = (t.kind == KwTrue);
        return e;
    }

    if (match(ps, LParen)) {
        Expr *inner = parse_expression(ps);
        if (!inner) return NULL;
        if (!expect(ps, RParen, "')' após a expressão")) return NULL;
        return inner;
    }

    tarm_error_at(ps->diag, t.line, t.col,
                  "token inesperado: '%.*s'", (int)t.len, t.start);
    return NULL;
}

// A árvore sai por parâmetro de saída e o retorno fica reservado ao sucesso/falha, exatamente como
// em `tarm_lexer_tokenize` — e, como lá, o veredito vem do `Diagnostics`, não do caminho
// percorrido.
//
// Sem recuperação de erro: a primeira produção que devolve NULL encerra a análise, então uma
// rodada relata um erro de sintaxe por vez. Ver docs/parser.md#erros-e-recuperação.
bool tarm_parser_program(Parser *ps, Expr ***out_items, size_t *out_count) {
    ExprList program = {0};

    while (!isAtEnd(ps)) {
        if (match(ps, Semicolon)) continue;

        Expr *e = parse_top_level(ps);
        if (!e) { ast_list_free(&program); return false; }

        // Instrução terminada em '}' (corpo de função) dispensa o ';'.
        if (previous(ps).kind != RBrace) {
            if (!expect(ps, Semicolon, "';' ao final da instrução")) {
                ast_list_free(&program);
                return false;
            }
        } else {
            match(ps, Semicolon);
        }

        if (!ast_list_push(&program, e)) { ast_list_free(&program); return false; }
    }

    *out_items = ast_list_commit(ps->arena, &program, out_count);
    return !tarm_diag_has_errors(ps->diag);
}