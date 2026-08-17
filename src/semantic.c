// ================================================================================================
// File: semantic.c
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
// Validação de tipos sobre a AST. O contrato da etapa está em semantic.h; aqui ficam só as
// escolhas de implementação.
//
// Duas invariantes atravessam o arquivo:
//
//   - `Void` como retorno de `check_expr` significa "erro já registrado nesta subárvore". Quem
//     chama não reporta de novo, e é isso que impede um erro virar uma cascata de mensagens.
//   - toda travessia recebe `Expr **`, e não `Expr *`, porque `coerce_to` **substitui** o nó pelo
//     `ExprCast` que o envolve — sem o endereço do campo do pai não haveria onde gravar a troca.

#include "semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
// Declarações adiantadas
// ------------------------------------------------------------------------------------------------
static DataType check_expr(SemanticAnalyzer *an, Expr **slot);
static void check_block(SemanticAnalyzer *an, Expr **items, size_t count);

// ------------------------------------------------------------------------------------------------
// Auxiliares de tipo
// ------------------------------------------------------------------------------------------------

const char *tarm_semantic_type_name(DataType type)
{
    switch (type)
    {
    case Char:
        return "char";
    case Int:
        return "int";
    case Int64:
        return "int64";
    case Float:
        return "float";
    case Bool:
        return "bool";
    case String:
        return "string";
    case Void:
        return "void";
    }
    return "?";
}

// Só operações de comparação produzem Bool; as aritméticas devolvem o tipo dos operandos.
static bool is_comparison(BinaryOp op)
{
    switch (op)
    {
    case OpEq:
    case OpGt:
    case OpLt:
    case OpGtEq:
    case OpLtEq:
        return true;
    case OpAdd:
    case OpSub:
    case OpMul:
    case OpDiv:
        return false;
    }
    return false;
}

static const char *binop_name(BinaryOp op)
{
    switch (op)
    {
    case OpAdd:
        return "+";
    case OpSub:
        return "-";
    case OpMul:
        return "*";
    case OpDiv:
        return "/";
    case OpEq:
        return "==";
    case OpGt:
        return ">";
    case OpLt:
        return "<";
    case OpGtEq:
        return ">=";
    case OpLtEq:
        return "<=";
    }
    return "?";
}

// Conversões implícitas permitidas. Só a partir de `Int`: é o tipo dos literais inteiros, e alargar
// a partir dele é o único caso em que a conversão não perde informação de forma surpreendente.
static bool find_implicit_cast(DataType from, DataType to, CastKind *out)
{
    if (from != Int)
        return false;

    switch (to)
    {
    case Bool:
        *out = IntToBool;
        return true;
    case Float:
        *out = IntToFloat;
        return true;
    case Int64:
        *out = IntToInt64;
        return true;
    case Char:
        *out = IntToChar;
        return true;
    default:
        return false;
    }
}

// `Char` é o único tipo em que um literal inteiro pode estourar a faixa em silêncio; os demais
// alvos de conversão implícita são mais largos que `Int`.
static bool validate_integer_range(DataType type, int64_t value)
{
    if (type != Char)
        return true;
    return value >= TARM_CHAR_MIN_VAL && value <= TARM_CHAR_MAX_VAL;
}

// ------------------------------------------------------------------------------------------------
// Inserção de cast
// ------------------------------------------------------------------------------------------------

// Envolve o nó em `slot` num `ExprCast`, deixando o pai apontar para o cast e o nó antigo virar
// seu operando. É por isso que o parâmetro é `Expr **` e não `Expr *`: sem o endereço do campo do
// pai não há como substituir o nó no lugar onde ele está guardado.
//
// `reported` (opcional) recebe `true` quando a falha já produziu uma mensagem própria — a de faixa
// estourada, que é a única que conhece o valor do literal. Sem isso, quem chama emitiria em cima
// dela um "espera X, recebeu Y" genérico, e o usuário veria dois erros para o mesmo problema.
static bool coerce_to(SemanticAnalyzer *an, Expr **slot, DataType from, DataType expected,
                      bool *reported)
{
    if (reported)
        *reported = false;

    if (from == expected)
        return true;

    CastKind ck;
    if (!find_implicit_cast(from, expected, &ck))
        return false;

    Expr *original = *slot;

    // Literal inteiro convertido para Char: a faixa é conferida aqui, onde ainda se sabe o valor.
    if (original->kind == ExprInteger &&
        !validate_integer_range(expected, original->as.integer.value))
    {
        tarm_error_at(an->diag, original->line, original->col,
                      "literal %lld fora da faixa de '%s'",
                      (long long)original->as.integer.value,
                      tarm_semantic_type_name(expected));
        if (reported)
            *reported = true;
        return false;
    }

    Expr *cast = ast_expr_new(an->arena, ExprCast, original->line, original->col);
    if (!cast)
        return false;

    cast->type = expected;
    cast->as.cast.castKind = ck;
    cast->as.cast.operand = original;

    *slot = cast;
    return true;
}

// Exige que a expressão em `slot` seja de `expected`, convertendo se possível e reportando se não.
static void expect_type(SemanticAnalyzer *an, Expr **slot, DataType expected, const char *context)
{
    DataType actual = check_expr(an, slot);
    if (actual == expected)
        return;

    // `Void` já indica erro registrado mais fundo; não vale reportar em cascata.
    if (actual == Void)
        return;

    bool reported = false;
    if (!coerce_to(an, slot, actual, expected, &reported) && !reported)
        tarm_error_at(an->diag, (*slot)->line, (*slot)->col,
                      "%s espera '%s', recebeu '%s'",
                      context, tarm_semantic_type_name(expected),
                      tarm_semantic_type_name(actual));
}

// Um inicializador de global precisa ser conhecido em tempo de montagem: ele vira `.quad` em
// `.data`, e não há onde executar código antes de o programa começar. A coerção implícita pode ter
// envolvido o literal num `ExprCast`, então a checagem atravessa essa camada.
static bool is_constant_literal(const Expr *e)
{
    while (e && e->kind == ExprCast)
        e = e->as.cast.operand;

    if (!e)
        return false;

    switch (e->kind)
    {
    case ExprInteger:
    case ExprFloat:
    case ExprBool:
    case ExprString:
    case ExprChar:
        return true;
    default:
        return false;
    }
}

// ------------------------------------------------------------------------------------------------
// Operação binária
// ------------------------------------------------------------------------------------------------

// Resolve o tipo comum dos dois lados, inserindo o cast no lado que precisar. Devolve `Void` se
// os tipos forem irreconciliáveis — o erro já foi registrado por quem chama.
static DataType check_binary_op(SemanticAnalyzer *an, Expr *e)
{
    DataType lt = check_expr(an, &e->as.binary.left);
    DataType rt = check_expr(an, &e->as.binary.right);

    if (lt == Void || rt == Void)
        return Void;

    DataType common = Void;

    if (lt == rt)
    {
        common = lt;
    }
    else if (coerce_to(an, &e->as.binary.left, lt, rt, NULL))
    {
        common = rt;
    }
    else if (coerce_to(an, &e->as.binary.right, rt, lt, NULL))
    {
        common = lt;
    }
    else
    {
        tarm_error_at(an->diag, e->line, e->col,
                      "operação '%s' entre tipos incompatíveis: '%s' e '%s'",
                      binop_name(e->as.binary.op),
                      tarm_semantic_type_name(lt), tarm_semantic_type_name(rt));
        return Void;
    }

    if (common == String && e->as.binary.op != OpEq)
    {
        tarm_error_at(an->diag, e->line, e->col,
                      "operação '%s' não se aplica a 'string'", binop_name(e->as.binary.op));
        return Void;
    }

    return is_comparison(e->as.binary.op) ? Bool : common;
}

// ------------------------------------------------------------------------------------------------
// Chamadas
// ------------------------------------------------------------------------------------------------

// Valida uma chamada contra a assinatura registrada e devolve o tipo de retorno dela.
//
// `ExprCall` e `ExprMethod` compartilham a mesma variante da união e são resolvidos juntos: a
// diferença é que, num método, `args[0]` é o receptor e é o **tipo dele** que identifica a
// assinatura — o nome sozinho não basta, porque um mesmo `len` pode existir para receptores
// diferentes (ver function_table.h).
static DataType check_call(SemanticAnalyzer *an, Expr *e)
{
    const bool   is_method = (e->kind == ExprMethod);
    const size_t first     = is_method ? 1 : 0;

    DataType receiver = Void;
    if (is_method)
    {
        if (e->as.call.arg_count == 0)
            return Void; // o Parser não produz método sem receptor
        receiver = check_expr(an, &e->as.call.args[0]);
    }

    const FunctionSignature *sig =
        is_method ? tarm_function_table_find_method(an->functions, e->as.call.name,
                                                    e->as.call.name_len, receiver)
                  : tarm_function_table_find(an->functions, e->as.call.name,
                                             e->as.call.name_len);

    if (!sig)
    {
        // Os argumentos são validados mesmo assim: um nome errado não deve esconder os erros que
        // estejam dentro deles.
        for (size_t i = first; i < e->as.call.arg_count; i++)
            check_expr(an, &e->as.call.args[i]);

        if (is_method && receiver == Void)
            return Void; // o erro já saiu na validação do receptor

        if (is_method)
            tarm_error_at(an->diag, e->line, e->col,
                          "'%s' não tem método '%.*s'",
                          tarm_semantic_type_name(receiver),
                          (int)e->as.call.name_len, e->as.call.name);
        else
            tarm_error_at(an->diag, e->line, e->col,
                          "função desconhecida: '%.*s'",
                          (int)e->as.call.name_len, e->as.call.name);
        return Void;
    }

    const size_t given = e->as.call.arg_count - first;

    if (!tarm_function_table_check_arity(sig, given))
        tarm_error_at(an->diag, e->line, e->col,
                      "'%.*s' espera %zu argumento(s), recebeu %zu",
                      (int)e->as.call.name_len, e->as.call.name, sig->param_count, given);

    // Cada posição é conferida contra o conjunto de tipos que a assinatura aceita ali, e o erro
    // cita a posição — numa chamada com vários argumentos, é o que diz qual deles está errado.
    for (size_t i = first; i < e->as.call.arg_count; i++)
    {
        const size_t pos    = i - first;
        DataType     actual = check_expr(an, &e->as.call.args[i]);

        if (actual == Void)
            continue; // erro já registrado mais fundo

        uint32_t mask = tarm_function_table_accepted_mask(sig, pos);
        if (mask == 0)
            continue; // argumento a mais: a aridade acima já reportou

        if (mask & TARM_TYPE_BIT(actual))
            continue;

        // Fora do conjunto, o argumento ainda pode chegar lá por conversão implícita — a mesma que
        // vale numa atribuição.
        bool coerced  = false;
        bool reported = false;
        for (int t = 0; t <= (int)Void && !coerced && !reported; t++)
            if (mask & TARM_TYPE_BIT((DataType)t))
                coerced = coerce_to(an, &e->as.call.args[i], actual, (DataType)t, &reported);

        if (coerced || reported)
            continue;

        tarm_error_at(an->diag, e->as.call.args[i]->line, e->as.call.args[i]->col,
                      "parâmetro %zu de '%.*s' não aceita '%s'",
                      pos + 1, (int)e->as.call.name_len, e->as.call.name,
                      tarm_semantic_type_name(actual));
    }

    return sig->ret_type;
}

// ------------------------------------------------------------------------------------------------
// Percurso da árvore
// ------------------------------------------------------------------------------------------------

static void check_block(SemanticAnalyzer *an, Expr **items, size_t count)
{
    for (size_t i = 0; i < count; i++)
        check_expr(an, &items[i]);
}

// Resolve o tipo do nó em `slot`, anota-o em `Expr::type` e devolve-o. `Void` sinaliza que um erro
// já foi registrado nesta subárvore — quem chama não deve reportar de novo em cascata.
static DataType check_expr(SemanticAnalyzer *an, Expr **slot)
{
    Expr *e = *slot;
    if (!e)
        return Void;

    DataType result = Void;

    switch (e->kind)
    {

    // --- literais ---------------------------------------------------------------------------
    case ExprInteger:
        result = Int;
        break;
    case ExprFloat:
        result = Float;
        break;
    case ExprBool:
        result = Bool;
        break;
    case ExprString:
        result = String;
        break;
    case ExprChar:
        result = Char;
        break;

    // --- referência a variável --------------------------------------------------------------
    case ExprIdentifier:
    {
        const Symbol *sym = tarm_symbol_table_find(an->symbols,
                                                   e->as.identifier.name,
                                                   e->as.identifier.len);
        if (!sym)
        {
            tarm_error_at(an->diag, e->line, e->col,
                          "variável não declarada: '%.*s'",
                          (int)e->as.identifier.len, e->as.identifier.name);
            result = Void;
        }
        else
        {
            result = sym->type;
        }
        break;
    }

    case ExprBinary:
        result = check_binary_op(an, e);
        break;

    case ExprCast:
        // Inserido pela própria análise, que já gravou o tipo de destino em `coerce_to`.
        result = e->type;
        break;

    // --- atribuição -------------------------------------------------------------------------
    case ExprAssign:
    {
        const Symbol *sym = tarm_symbol_table_find(an->symbols,
                                                   e->as.assign.name,
                                                   e->as.assign.name_len);
        if (!sym)
        {
            tarm_error_at(an->diag, e->line, e->col,
                          "atribuição a variável não declarada: '%.*s'",
                          (int)e->as.assign.name_len, e->as.assign.name);
            check_expr(an, &e->as.assign.value); // segue validando o lado direito
            result = Void;
            break;
        }

        DataType target = sym->type;
        expect_type(an, &e->as.assign.value, target, "atribuição");
        result = target;
        break;
    }

    // --- declaração de variável -------------------------------------------------------------
    case ExprVarDecl:
    {
        DataType declared = e->as.var_decl.type;

        if (e->as.var_decl.initializer)
        {
            expect_type(an, &e->as.var_decl.initializer, declared, "inicializador");

            if (e->as.var_decl.frame == Global &&
                !is_constant_literal(e->as.var_decl.initializer))
                tarm_error_at(an->diag, e->line, e->col,
                              "variável global '%.*s': o inicializador precisa ser um literal "
                              "constante",
                              (int)e->as.var_decl.name_len, e->as.var_decl.name);
        }

        bool ok;
        if (e->as.var_decl.frame == Global)
            ok = tarm_symbol_table_declare_global(an->symbols,
                                                  e->as.var_decl.name, e->as.var_decl.name_len,
                                                  declared, 0, NULL);
        else
            ok = tarm_symbol_table_declare(an->symbols,
                                           e->as.var_decl.name, e->as.var_decl.name_len,
                                           declared, 0, NULL);

        if (!ok)
            tarm_error_at(an->diag, e->line, e->col,
                          "variável já declarada neste escopo: '%.*s'",
                          (int)e->as.var_decl.name_len, e->as.var_decl.name);

        result = declared;
        break;
    }

    // --- declaração de função ---------------------------------------------------------------
    case ExprFuncDecl:
    {
        // Escopo próprio: parâmetros e locais desta função somem ao sair, e o offset de frame
        // recomeça do topo. É o que deixa duas funções usarem os mesmos nomes de variável.
        //
        // A assinatura já foi registrada na passagem prévia (`declare_functions`), então uma
        // chamada dentro do corpo enxerga inclusive funções declaradas depois desta.
        size_t scope = tarm_symbol_table_scope_begin(an->symbols);

        DataType saved_return = an->current_return_type;
        bool saved_inside = an->inside_function;

        an->current_return_type = e->as.func_decl.ret_type;
        an->inside_function = true;

        // Parâmetros são `VarDecl`: validá-los já os registra na tabela com o offset do frame.
        check_block(an, e->as.func_decl.params, e->as.func_decl.param_count);
        check_block(an, e->as.func_decl.body, e->as.func_decl.body_count);

        an->current_return_type = saved_return;
        an->inside_function = saved_inside;

        tarm_symbol_table_scope_end(an->symbols, scope);

        result = e->as.func_decl.ret_type;
        break;
    }

    // --- chamada ----------------------------------------------------------------------------
    case ExprCall:
    case ExprMethod:
        result = check_call(an, e);
        break;

    // --- condicional ------------------------------------------------------------------------
    case ExprConditional:
        expect_type(an, &e->as.conditional.cond, Bool, "condição de 'if'");
        check_block(an, e->as.conditional.then_body, e->as.conditional.then_count);
        check_block(an, e->as.conditional.else_body, e->as.conditional.else_count);
        result = Void;
        break;

    case ExprWhile:
        expect_type(an, &e->as.while_loop.cond, Bool, "condição de 'while'");
        check_block(an, e->as.while_loop.body, e->as.while_loop.body_count);
        result = Void;
        break;

    // --- retorno ----------------------------------------------------------------------------
    case ExprReturn:
        if (!an->inside_function)
        {
            tarm_error_at(an->diag, e->line, e->col,
                          "'return' fora do corpo de uma função");
            result = Void;
            break;
        }

        if (e->as.ret.value)
            expect_type(an, &e->as.ret.value, an->current_return_type, "'return'");
        else if (an->current_return_type != Void)
            tarm_error_at(an->diag, e->line, e->col,
                          "'return' sem valor em função que devolve '%s'",
                          tarm_semantic_type_name(an->current_return_type));

        result = Void;
        break;
    }

    e->type = result;
    return result;
}

// ------------------------------------------------------------------------------------------------
// Ponto de entrada
// ------------------------------------------------------------------------------------------------

void tarm_semantic_init(SemanticAnalyzer *an, Diagnostics *diag, Arena *arena,
                        SymbolTable *symbols, FunctionTable *functions)
{
    an->diag = diag;
    an->arena = arena;
    an->symbols = symbols;
    an->functions = functions;
    an->current_return_type = Void;
    an->inside_function = false;
}

// Passagem prévia: registra só as assinaturas, sem olhar corpo nenhum. É o que permite uma função
// chamar outra definida **depois** dela no arquivo — sem isso, a ordem em que as funções aparecem
// no `.tm` viraria regra da linguagem.
static void declare_functions(SemanticAnalyzer *an, Expr **program, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        Expr *e = program[i];
        if (!e || e->kind != ExprFuncDecl)
            continue;

        // Os parâmetros são nós `ExprVarDecl`; a assinatura guarda só os tipos, na ordem.
        DataType params[TARM_MAX_PARAMS];
        size_t   n = e->as.func_decl.param_count;
        if (n > TARM_MAX_PARAMS)
            n = TARM_MAX_PARAMS;

        for (size_t p = 0; p < n; p++)
        {
            const Expr *decl = e->as.func_decl.params[p];
            params[p] = decl ? decl->as.var_decl.type : Void;
        }

        const char *reason = NULL;
        if (!tarm_function_table_declare(an->functions,
                                         e->as.func_decl.name, e->as.func_decl.name_len,
                                         e->as.func_decl.ret_type,
                                         params, e->as.func_decl.param_count, &reason))
            tarm_error_at(an->diag, e->line, e->col, "função '%.*s': %s",
                          (int)e->as.func_decl.name_len, e->as.func_decl.name,
                          reason ? reason : "não foi possível registrar");
    }
}

bool tarm_semantic_analyse(SemanticAnalyzer *an, Expr **program, size_t count)
{
    declare_functions(an, program, count);

    for (size_t i = 0; i < count; i++)
        check_expr(an, &program[i]);

    return !tarm_diag_has_errors(an->diag);
}