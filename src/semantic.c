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
static void     check_block(SemanticAnalyzer *an, Expr **items, size_t count);

// ------------------------------------------------------------------------------------------------
// Auxiliares de tipo
// ------------------------------------------------------------------------------------------------

const char *tarm_semantic_type_name(BaseType type) {
    switch (type) {
        case Char:   return "char";
        case Int:    return "int";
        case Int64:  return "int64";
        case Float:  return "float";
        case Bool:   return "bool";
        case String: return "string";
        case Void:   return "void";
    }
    return "?";
}

// Só operações de comparação produzem Bool; as aritméticas devolvem o tipo dos operandos.
static bool is_comparison(BinaryOp op) {
    switch (op) {
        case OpEq:
        case OpGt:
        case OpLt:
        case OpGtEq:
        case OpLtEq: return true;
        case OpAdd:
        case OpSub:
        case OpMul:
        case OpDiv:  return false;
        case OpNone: return false;
    }
    return false;
}

static const char *binop_name(BinaryOp op) {
    switch (op) {
        case OpAdd:  return "+";
        case OpSub:  return "-";
        case OpMul:  return "*";
        case OpDiv:  return "/";
        case OpEq:   return "==";
        case OpGt:   return ">";
        case OpLt:   return "<";
        case OpGtEq: return ">=";
        case OpLtEq: return "<=";
        case OpNone: return "?";
    }
    return "?";
}

// Conversões implícitas permitidas. Só a partir de `Int`: é o tipo dos literais inteiros, e alargar
// a partir dele é o único caso em que a conversão não perde informação de forma surpreendente.
static bool find_implicit_cast(BaseType from, BaseType to, CastKind *out) {
    if (from != Int) return false;

    switch (to) {
        case Bool:  *out = IntToBool; return true;
        case Float: *out = IntToFloat; return true;
        case Int64: *out = IntToInt64; return true;
        case Char:  *out = IntToChar; return true;
        default:    return false;
    }
}

// `Char` é o único tipo em que um literal inteiro pode estourar a faixa em silêncio; os demais
// alvos de conversão implícita são mais largos que `Int`.
static bool validate_integer_range(BaseType type, int64_t value) {
    if (type != Char) return true;
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
                      bool *reported) {
    if (reported) *reported = false;

    if (from.type == expected.type) return true;

    CastKind ck;
    if (!find_implicit_cast(from.type, expected.type, &ck)) return false;

    Expr *original = *slot;

    // Literal inteiro convertido para Char: a faixa é conferida aqui, onde ainda se sabe o valor.
    if (original->kind == ExprInteger &&
        !validate_integer_range(expected.type, original->as.integer.value)) {
        tarm_error_at(an->diag, original->line, original->col, "literal %lld fora da faixa de '%s'",
                      (long long)original->as.integer.value,
                      tarm_semantic_type_name(expected.type));
        if (reported) *reported = true;
        return false;
    }

    Expr *cast = ast_expr_new(an->arena, ExprCast, original->line, original->col);
    if (!cast) return false;

    cast->type             = expected;
    cast->as.cast.castKind = ck;
    cast->as.cast.operand  = original;

    *slot = cast;
    return true;
}

// Exige que a expressão em `slot` seja de `expected`, convertendo se possível e reportando se não.
static void expect_type(SemanticAnalyzer *an, Expr **slot, DataType expected, const char *context) {
    DataType actual = check_expr(an, slot);
    if (actual.type == expected.type) return;

    // `Void` já indica erro registrado mais fundo; não vale reportar em cascata.
    if (actual.type == Void) return;

    bool reported = false;
    if (!coerce_to(an, slot, actual, expected, &reported) && !reported)
        tarm_error_at(an->diag, (*slot)->line, (*slot)->col, "%s espera '%s', recebeu '%s'",
                      context, tarm_semantic_type_name(expected.type),
                      tarm_semantic_type_name(actual.type));
}

// Um inicializador de global precisa ser conhecido em tempo de montagem: ele vira `.quad` em
// `.data`, e não há onde executar código antes de o programa começar. A coerção implícita pode ter
// envolvido o literal num `ExprCast`, então a checagem atravessa essa camada.
static bool is_constant_literal(const Expr *e) {
    while (e && e->kind == ExprCast)
        e = e->as.cast.operand;

    if (!e) return false;

    switch (e->kind) {
        case ExprInteger:
        case ExprFloat:
        case ExprBool:
        case ExprString:
        case ExprChar:    return true;
        default:          return false;
    }
}

// ------------------------------------------------------------------------------------------------
// Operação binária
// ------------------------------------------------------------------------------------------------

// Resolve o tipo comum dos dois lados, inserindo o cast no lado que precisar. Devolve `Void` se
// os tipos forem irreconciliáveis — o erro já foi registrado por quem chama.
static DataType check_binary_op(SemanticAnalyzer *an, Expr *e) {
    DataType lt = check_expr(an, &e->as.binary.left);
    DataType rt = check_expr(an, &e->as.binary.right);

    if (lt.type == Void || rt.type == Void) return tarm_datatype_of(Void);

    DataType common = tarm_datatype_of(Void);

    if (lt.type == rt.type) {
        common = lt;
    } else if (coerce_to(an, &e->as.binary.left, lt, rt, NULL)) {
        common = rt;
    } else if (coerce_to(an, &e->as.binary.right, rt, lt, NULL)) {
        common = lt;
    } else {
        tarm_error_at(an->diag, e->line, e->col,
                      "operação '%s' entre tipos incompatíveis: '%s' e '%s'",
                      binop_name(e->as.binary.op), tarm_semantic_type_name(lt.type),
                      tarm_semantic_type_name(rt.type));
        return tarm_datatype_of(Void);
    }

    if (common.type == String && e->as.binary.op != OpEq) {
        tarm_error_at(an->diag, e->line, e->col, "operação '%s' não se aplica a 'string'",
                      binop_name(e->as.binary.op));
        return tarm_datatype_of(Void);
    }

    return is_comparison(e->as.binary.op) ? tarm_datatype_of(Bool) : common;
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
static DataType check_call(SemanticAnalyzer *an, Expr *e) {
    const bool   is_method = (e->kind == ExprMethod);
    const size_t first     = is_method ? 1 : 0;

    DataType receiver = tarm_datatype_of(Void);
    if (is_method) {
        if (e->as.call.arg_count == 0)
            return tarm_datatype_of(Void); // o Parser não produz método sem receptor
        receiver = check_expr(an, &e->as.call.args[0]);
    }

    const FunctionSignature *sig =
        is_method ? tarm_function_table_find_method(an->functions, e->as.call.name,
                                                    e->as.call.name_len, receiver.type)
                  : tarm_function_table_find(an->functions, e->as.call.name, e->as.call.name_len);

    if (!sig) {
        // Os argumentos são validados mesmo assim: um nome errado não deve esconder os erros que
        // estejam dentro deles.
        for (size_t i = first; i < e->as.call.arg_count; i++)
            check_expr(an, &e->as.call.args[i]);

        if (is_method && receiver.type == Void)
            return tarm_datatype_of(Void); // o erro já saiu na validação do receptor

        if (is_method)
            tarm_error_at(an->diag, e->line, e->col, "'%s' não tem método '%.*s'",
                          tarm_semantic_type_name(receiver.type), (int)e->as.call.name_len,
                          e->as.call.name);
        else
            tarm_error_at(an->diag, e->line, e->col, "função desconhecida: '%.*s'",
                          (int)e->as.call.name_len, e->as.call.name);
        return tarm_datatype_of(Void);
    }

    const size_t given = e->as.call.arg_count - first;

    if (!tarm_function_table_check_arity(sig, given))
        tarm_error_at(an->diag, e->line, e->col, "'%.*s' espera %zu argumento(s), recebeu %zu",
                      (int)e->as.call.name_len, e->as.call.name, sig->param_count, given);

    // Cada posição é conferida contra o conjunto de tipos que a assinatura aceita ali, e o erro
    // cita a posição — numa chamada com vários argumentos, é o que diz qual deles está errado.
    for (size_t i = first; i < e->as.call.arg_count; i++) {
        const size_t pos    = i - first;
        DataType     actual = check_expr(an, &e->as.call.args[i]);

        if (actual.type == Void) continue; // erro já registrado mais fundo

        uint32_t mask = tarm_function_table_accepted_mask(sig, pos);
        if (mask == 0) continue; // argumento a mais: a aridade acima já reportou

        if (mask & TARM_TYPE_BIT(actual.type)) continue;

        // Fora do conjunto, o argumento ainda pode chegar lá por conversão implícita — a mesma que
        // vale numa atribuição.
        bool coerced  = false;
        bool reported = false;
        for (int t = 0; t <= (int)Void && !coerced && !reported; t++)
            if (mask & TARM_TYPE_BIT((BaseType)t))
                coerced = coerce_to(an, &e->as.call.args[i], actual, tarm_datatype_of((BaseType)t),
                                    &reported);

        if (coerced || reported) continue;

        tarm_error_at(an->diag, e->as.call.args[i]->line, e->as.call.args[i]->col,
                      "parâmetro %zu de '%.*s' não aceita '%s'", pos + 1, (int)e->as.call.name_len,
                      e->as.call.name, tarm_semantic_type_name(actual.type));
    }

    return tarm_datatype_of(sig->ret_type);
}

// ------------------------------------------------------------------------------------------------
// Percurso da árvore
// ------------------------------------------------------------------------------------------------

static void check_block(SemanticAnalyzer *an, Expr **items, size_t count) {
    for (size_t i = 0; i < count; i++)
        check_expr(an, &items[i]);
}

// Resolve o tipo do nó em `slot`, anota-o em `Expr::type` e devolve-o. `Void` sinaliza que um erro
// já foi registrado nesta subárvore — quem chama não deve reportar de novo em cascata.
static DataType check_expr(SemanticAnalyzer *an, Expr **slot) {
    Expr *e = *slot;
    if (!e) return tarm_datatype_of(Void);

    DataType result = tarm_datatype_of(Void);

    switch (e->kind) {

        // --- literais ---------------------------------------------------------------------------
        case ExprInteger: result = tarm_datatype_of(Int); break;
        case ExprFloat:   result = tarm_datatype_of(Float); break;
        case ExprBool:    result = tarm_datatype_of(Bool); break;
        case ExprString:  result = tarm_datatype_of(String); break;
        case ExprChar:    result = tarm_datatype_of(Char); break;
        // Um literal de array não tem tipo próprio: quem o dá é a declaração à esquerda, que conhece o
        // tipo e o tamanho reservados. Aqui os elementos só são percorridos para que erros dentro deles
        // apareçam mesmo quando o literal está solto; a checagem de verdade é a de `ExprVarDecl`.
        case ExprArrayLit:
            for (size_t i = 0; i < e->as.array_lit.count; i++)
                check_expr(an, &e->as.array_lit.elements[i]);
            result = tarm_datatype_of(Void);
            break;
        // --- referência a variável --------------------------------------------------------------
        case ExprIdentifier: {
            const Symbol *sym =
                tarm_symbol_table_find(an->symbols, e->as.identifier.name, e->as.identifier.len);
            if (!sym) {
                tarm_error_at(an->diag, e->line, e->col, "variável não declarada: '%.*s'",
                              (int)e->as.identifier.len, e->as.identifier.name);
                result = tarm_datatype_of(Void);
            } else {
                result = sym->type;
            }
            break;
        }
        // O resultado de `v[i]` é o **elemento**: o mesmo tipo base, com a forma de array desligada.
        //
        // A faixa é conferida aqui só quando o índice é literal — é o único caso em que o valor se
        // conhece antes de o programa rodar, e o único em que dá para recusar na compilação. Nos
        // demais, quem confere é o código emitido pela Codegen, antes de cada acesso.
        // Ver docs/parser.md#arrays-novo-e-em-desenvolvimento.
        case ExprIndex: {
            DataType base_t = check_expr(an, &e->as.index.base);
            expect_type(an, &e->as.index.index, tarm_datatype_of(Int), "índice");

            if (base_t.type == Void) {
                result = tarm_datatype_of(Void);
                break;
            }

            if (!base_t.is_array) {
                tarm_error_at(an->diag, e->line, e->col, "indexação de valor que não é array");
                result = tarm_datatype_of(Void);
                break;
            }
            Expr *index = e->as.index.index;
            if (index->kind == ExprInteger) {
                if (index->as.integer.value >= (int64_t)(base_t.array_len) ||
                    index->as.integer.value < 0) {
                    tarm_error_at(an->diag, e->line, e->col, "elemento fora de alcance");
                    result = tarm_datatype_of(Void);
                    break;
                }
            } else if (index->kind == ExprIdentifier || index->kind == ExprBinary) {
                DataType expr = check_expr(an, &index);
                if (expr.type != Int && expr.type != Int64) {
                    tarm_error_at(an->diag, e->line, e->col, "índice não-inteiro não permitido");
                    result = tarm_datatype_of(Void);
                    break;
                }
            } else {
                tarm_error_at(an->diag, e->line, e->col, "índice de tipo não permitido");
                result = tarm_datatype_of(Void);
                break;
            }

            result           = base_t;
            result.is_array  = false;
            result.array_len = 0;
            break;
        }
        case ExprBinary: result = check_binary_op(an, e); break;

        case ExprCast:
            // Inserido pela própria análise, que já gravou o tipo de destino em `coerce_to`.
            result = e->type;
            break;

        // --- atribuição -------------------------------------------------------------------------
        case ExprAssign: {
            Expr *target = e->as.assign.target;

            switch (e->as.assign.target->kind) {
                case ExprIdentifier: {
                    if (!target->as.identifier.name || !target->as.identifier.len) {
                        tarm_error_at(an->diag, target->line, target->col,
                                      "variável sem slot na geração de código: '%.*s'",
                                      (int)target->as.identifier.len, target->as.identifier.name);
                        return tarm_datatype_of(Void);
                    }

                    const Symbol *sym = tarm_symbol_table_find(
                        an->symbols, target->as.identifier.name, target->as.identifier.len);
                    if (!sym) {
                        tarm_error_at(an->diag, target->line, target->col,
                                      "atribuição a variável não declarada: '%.*s'",
                                      (int)target->as.identifier.len, target->as.identifier.name);
                        check_expr(an, &e->as.assign.value); // segue validando o lado direito
                        result = tarm_datatype_of(Void);
                        break;
                    }

                    DataType target_t = sym->type;
                    expect_type(an, &e->as.assign.value, target_t, "atribuição");
                    result = target_t;
                    break;
                }
                // Atribuir a um elemento (`v[0] = 9`). O tipo esperado do lado direito é o da variável
                // indexada, com a coerção implícita valendo como em qualquer atribuição.
                //
                // A base precisa ser um array — sem esse teste, `x[5] = 9` num escalar viraria uma escrita
                // 20 bytes além do slot. A faixa do índice, essa, continua conferida só na leitura.
                case ExprIndex: {

                    Expr *base = target->as.index.base;
                    Expr *idx  = target->as.index.index;

                    if (!base->as.identifier.name || !base->as.identifier.len) {
                        tarm_error_at(an->diag, base->line, base->col,
                                      "variável sem slot na geração de código: '%.*s'",
                                      (int)base->as.identifier.len, base->as.identifier.name);
                        return tarm_datatype_of(Void);
                    }

                    const Symbol *sym = tarm_symbol_table_find(
                        an->symbols, base->as.identifier.name, base->as.identifier.len);
                    if (!sym) {
                        tarm_error_at(an->diag, e->line, e->col,
                                      "atribuição a variável não declarada: '%.*s'",
                                      (int)base->as.identifier.len, base->as.identifier.name);
                        check_expr(an, &e->as.assign.value);
                        result = tarm_datatype_of(Void);
                        break;
                    }


                    if (!sym->type.is_array) {
                        tarm_error_at(an->diag, base->line, base->col,
                                      "base de atribuição não é um array");
                        return tarm_datatype_of(Void);
                    }

                    DataType target_t = sym->type;
                    expect_type(an, &e->as.assign.value, target_t, "atribuição");
                    result = target_t;

                    if (idx->kind == ExprInteger) {
                        if (idx->as.integer.value >= (int64_t)(target_t.array_len) ||
                            idx->as.integer.value < 0) {
                            tarm_error_at(an->diag, e->line, e->col, "elemento fora de alcance");
                            result = tarm_datatype_of(Void);
                            break;
                        }
                    } else if (idx->kind == ExprIdentifier || idx->kind == ExprBinary) {
                        DataType expr = check_expr(an, &idx);
                        if (expr.type != Int && expr.type != Int64) {
                            tarm_error_at(an->diag, e->line, e->col,
                                          "índice não-inteiro não permitido");
                            result = tarm_datatype_of(Void);
                            break;
                        }
                    } else {
                        tarm_error_at(an->diag, e->line, e->col, "índice de tipo não permitido");
                        result = tarm_datatype_of(Void);
                        break;
                    }


                    break;
                }
                default:
                    tarm_error_at(an->diag, e->line, e->col,
                                  "atribuição semanticamente inesperada");
            }
            break;
        }

        // --- declaração de variável -------------------------------------------------------------
        case ExprVarDecl: {
            DataType declared = e->as.var_decl.type;
            Expr    *target   = e->as.var_decl.obj;
            Expr    *init     = e->as.var_decl.initializer;

            // Array com inicializador: a contagem e o tipo de cada elemento são conferidos contra o
            // que foi declarado, e o literal herda o tipo completo — é dele que a Codegen tira o passo
            // entre os elementos.
            if (init && declared.is_array && init->kind == ExprArrayLit) {
                if (init->as.array_lit.count > declared.array_len)
                    tarm_error_at(an->diag, init->line, init->col,
                                  "quantidade de elementos acima do reservado em: '%.*s",
                                  (int)target->as.identifier.len, target->as.identifier.name);

                DataType el = declared;
                el.is_array = false;
                for (size_t i = 0; i < init->as.array_lit.count; i++)
                    expect_type(an, &init->as.array_lit.elements[i], el, "elemento do array");

                init->type = declared;
            } else if (init) {
                expect_type(an, &e->as.var_decl.initializer, declared, "inicializador");
                if (!target->as.identifier.name || !target->as.identifier.len) {
                    tarm_error_at(an->diag, target->line, target->col,
                                  "variável sem slot na geração de código: '%.*s'",
                                  (int)target->as.identifier.len, target->as.identifier.name);
                    return tarm_datatype_of(Void);
                }
                if (e->as.var_decl.frame == Global &&
                    !is_constant_literal(e->as.var_decl.initializer))
                    tarm_error_at(an->diag, e->line, e->col,
                                  "variável global '%.*s': o inicializador precisa ser um literal "
                                  "constante",
                                  (int)target->as.identifier.len, target->as.identifier.name);
            }

            bool ok;
            if (e->as.var_decl.frame == Global)
                ok = tarm_symbol_table_declare_global(an->symbols, target->as.identifier.name,
                                                      target->as.identifier.len, declared, 0, NULL);
            else
                ok = tarm_symbol_table_declare(an->symbols, target->as.identifier.name,
                                               target->as.identifier.len, declared, 0, NULL);

            if (!ok)
                tarm_error_at(an->diag, e->line, e->col,
                              "variável já declarada neste escopo: '%.*s'",
                              (int)target->as.identifier.len, target->as.identifier.name);

            result = declared;
            break;
        }

        // --- declaração de função ---------------------------------------------------------------
        case ExprFuncDecl: {
            // Escopo próprio: parâmetros e locais desta função somem ao sair, e o offset de frame
            // recomeça do topo. É o que deixa duas funções usarem os mesmos nomes de variável.
            //
            // A assinatura já foi registrada na passagem prévia (`declare_functions`), então uma
            // chamada dentro do corpo enxerga inclusive funções declaradas depois desta.
            size_t scope = tarm_symbol_table_scope_begin(an->symbols);

            DataType saved_return = an->current_return_type;
            bool     saved_inside = an->inside_function;

            an->current_return_type = e->as.func_decl.ret_type;
            an->inside_function     = true;

            // Parâmetros são `VarDecl`: validá-los já os registra na tabela com o offset do frame.
            check_block(an, e->as.func_decl.params, e->as.func_decl.param_count);
            check_block(an, e->as.func_decl.body, e->as.func_decl.body_count);

            an->current_return_type = saved_return;
            an->inside_function     = saved_inside;

            tarm_symbol_table_scope_end(an->symbols, scope);

            result = e->as.func_decl.ret_type;
            break;
        }

        // --- chamada ----------------------------------------------------------------------------
        case ExprCall:
        case ExprMethod: result = check_call(an, e); break;

        // --- condicional ------------------------------------------------------------------------
        case ExprConditional:
            expect_type(an, &e->as.conditional.cond, tarm_datatype_of(Bool), "condição de 'if'");
            check_block(an, e->as.conditional.then_body, e->as.conditional.then_count);
            check_block(an, e->as.conditional.else_body, e->as.conditional.else_count);
            result = tarm_datatype_of(Void);
            break;

        case ExprWhile:
            expect_type(an, &e->as.while_loop.cond, tarm_datatype_of(Bool), "condição de 'while'");
            check_block(an, e->as.while_loop.body, e->as.while_loop.body_count);
            result = tarm_datatype_of(Void);
            break;

        // --- retorno ----------------------------------------------------------------------------
        case ExprReturn: {
            if (!an->inside_function) {
                tarm_error_at(an->diag, e->line, e->col, "'return' fora do corpo de uma função");
                result = tarm_datatype_of(Void);
                break;
            }

            if (e->as.ret.value)
                expect_type(an, &e->as.ret.value, an->current_return_type, "'return'");
            else if (an->current_return_type.type != Void)
                tarm_error_at(an->diag, e->line, e->col,
                              "'return' sem valor em função que devolve '%s'",
                              tarm_semantic_type_name(an->current_return_type.type));

            result = tarm_datatype_of(Void);
            break;
        }
        default: tarm_error_at(an->diag, e->line, e->col, "expressão desconhecida");
    }

    e->type = result;
    return result;
}

// ------------------------------------------------------------------------------------------------
// Ponto de entrada
// ------------------------------------------------------------------------------------------------

void tarm_semantic_init(SemanticAnalyzer *an, Diagnostics *diag, Arena *arena, SymbolTable *symbols,
                        FunctionTable *functions) {
    an->diag                = diag;
    an->arena               = arena;
    an->symbols             = symbols;
    an->functions           = functions;
    an->current_return_type = tarm_datatype_of(Void);
    an->inside_function     = false;
}

// Passagem prévia: registra só as assinaturas, sem olhar corpo nenhum. É o que permite uma função
// chamar outra definida **depois** dela no arquivo — sem isso, a ordem em que as funções aparecem
// no `.tm` viraria regra da linguagem.
static void declare_functions(SemanticAnalyzer *an, Expr **program, size_t count) {
    for (size_t i = 0; i < count; i++) {
        Expr *e = program[i];
        if (!e || e->kind != ExprFuncDecl) continue;

        // Os parâmetros são nós `ExprVarDecl`; a assinatura guarda só os tipos, na ordem.
        BaseType params[TARM_MAX_PARAMS];
        size_t   n = e->as.func_decl.param_count;
        if (n > TARM_MAX_PARAMS) n = TARM_MAX_PARAMS;

        for (size_t p = 0; p < n; p++) {
            const Expr *decl = e->as.func_decl.params[p];
            params[p]        = decl ? decl->as.var_decl.type.type : Void;
        }

        const char *reason = NULL;
        if (!tarm_function_table_declare(an->functions, e->as.func_decl.name,
                                         e->as.func_decl.name_len, e->as.func_decl.ret_type.type,
                                         params, e->as.func_decl.param_count, &reason))
            tarm_error_at(an->diag, e->line, e->col, "função '%.*s': %s",
                          (int)e->as.func_decl.name_len, e->as.func_decl.name,
                          reason ? reason : "não foi possível registrar");
    }
}

bool tarm_semantic_analyse(SemanticAnalyzer *an, Expr **program, size_t count) {
    declare_functions(an, program, count);

    for (size_t i = 0; i < count; i++)
        check_expr(an, &program[i]);

    return !tarm_diag_has_errors(an->diag);
}
