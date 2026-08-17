// ================================================================================================
// File: codegen.c
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
// Emissão de assembly x86-64 (System V AMD64, sintaxe AT&T). O contrato da etapa está em
// codegen.h; aqui ficam só as escolhas de implementação.

#include "codegen.h"

#include <stdlib.h>
#include <string.h>

// Registradores de argumento da ABI System V, na ordem.
static const char *ARG_REGS[6] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };
#define MAX_REG_ARGS 6

// Todo slot local ocupa 8 bytes: mantém `%rsp` alinhado sem cálculo por tipo, e as operações
// abaixo trabalham em 64 bits de ponta a ponta.
#define SLOT_SIZE 8

// ------------------------------------------------------------------------------------------------
// Declarações adiantadas
// ------------------------------------------------------------------------------------------------
static bool gen_expr(Codegen *cg, const Expr *e);
static bool gen_block(Codegen *cg, Expr *const *items, size_t count);

// ------------------------------------------------------------------------------------------------
// Pilha
// ------------------------------------------------------------------------------------------------
//
// A ABI exige `%rsp` múltiplo de 16 no momento do `call`. O prólogo deixa o frame alinhado, mas
// um `pushq` pendente (o lado esquerdo de uma operação binária, por exemplo) desalinha — e uma
// chamada a `printf` nessa situação falha ao tocar registradores SSE. Por isso a contagem: quem
// emite um push registra aqui, e `call_align` corrige antes de chamar.

static void emit_push(Codegen *cg, const char *reg) {
    fprintf(cg->out, "    pushq   %s\n", reg);
    cg->stack_depth++;
}

static void emit_pop(Codegen *cg, const char *reg) {
    fprintf(cg->out, "    popq    %s\n", reg);
    cg->stack_depth--;
}

// Devolve quantos bytes de correção foram inseridos, para `call_unalign` desfazer.
static int call_align(Codegen *cg) {
    if (cg->stack_depth % 2 == 0) return 0;
    fprintf(cg->out, "    subq    $8, %%rsp\n");
    return 8;
}

static void call_unalign(Codegen *cg, int pad) {
    if (pad) fprintf(cg->out, "    addq    $%d, %%rsp\n", pad);
}

// ------------------------------------------------------------------------------------------------
// Auxiliares
// ------------------------------------------------------------------------------------------------

static size_t next_label(Codegen *cg) {
    return cg->label_counter++;
}

static void unsupported(Codegen *cg, const Expr *e, const char *what) {
    tarm_error_at(cg->diag, e->line, e->col,
                  "geração de código ainda não suporta %s", what);
}

// Reserva um slot no frame corrente para a variável e devolve o offset relativo a `%rbp`.
static bool declare_local(Codegen *cg, const Expr *decl, int *out_offset) {
    return tarm_symbol_table_declare(&cg->symbols,
                                     decl->as.var_decl.name,
                                     decl->as.var_decl.name_len,
                                     decl->as.var_decl.type,
                                     SLOT_SIZE, out_offset);
}

// Percorre a subárvore somando os slots que o corpo vai precisar. Roda **antes** de emitir o
// prólogo, porque o `subq` precisa do total e as declarações só aparecem no meio do corpo.
static size_t count_slots(Expr *const *items, size_t count) {
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        const Expr *e = items[i];
        if (!e) continue;

        switch (e->kind) {
            case ExprVarDecl:
                n++;
                break;
            case ExprConditional:
                n += count_slots(e->as.conditional.then_body, e->as.conditional.then_count);
                n += count_slots(e->as.conditional.else_body, e->as.conditional.else_count);
                break;
            case ExprWhile:
                n += count_slots(e->as.while_loop.body, e->as.while_loop.body_count);
                break;
            default:
                break;
        }
    }
    return n;
}

// ------------------------------------------------------------------------------------------------
// Endereçamento de variável
// ------------------------------------------------------------------------------------------------

// Escreve em `buf` o operando de memória da variável: `-40(%rbp)` para local, `globobj_3(%rip)`
// para global. Devolve `false` se o nome não estiver na tabela (bug do compilador, não do usuário).
static bool var_operand(Codegen *cg, const char *name, uint32_t name_len,
                        char *buf, size_t buf_size) {
    const Symbol *sym = tarm_symbol_table_find(&cg->symbols, name, name_len);
    if (!sym) return false;

    if (sym->is_global)
        snprintf(buf, buf_size, "globobj_%zu(%%rip)", sym->label_id);
    else
        snprintf(buf, buf_size, "%d(%%rbp)", sym->offset);

    return true;
}

// ------------------------------------------------------------------------------------------------
// Literais de string
// ------------------------------------------------------------------------------------------------
//
// Uma String circula pela linguagem como ponteiro para um **objeto com header** de 24 bytes
// (`OBJ_DATA`/`OBJ_LEN`/`OBJ_CAP`), o mesmo formato que a runtime aloca em tempo de execução — é
// isso que deixa `tarm_print_str` e o método `len()` operarem sobre um literal sem distinção.
// Ver docs/runtime.md#objetos-com-header.
//
// Um literal, sendo imutável, não precisa de alocação: o header já nasce em `.rodata`, com custo
// zero em tempo de execução.

// Comprimento do texto depois de resolver os escapes. O Lexer guarda a fatia crua (com a barra) e
// quem decodifica é o `as`, ao montar o `.string` — mas o `OBJ_LEN` gravado no header precisa ser
// o do texto final, não o do lexema.
static size_t decoded_length(const char *text, uint32_t len) {
    size_t n = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (text[i] == '\\' && i + 1 < len) i++;
        n++;
    }
    return n;
}

// Repassa o texto ao `as`, que entende as mesmas sequências que o Lexer valida. Duas exceções: a
// aspa simples escapada não é escape dentro de aspas duplas, e a aspa dupla crua precisa ganhar
// barra para não fechar a string do assembler.
static void emit_string_bytes(Codegen *cg, const char *text, uint32_t len) {
    fputs("    .string \"", cg->out);

    for (uint32_t i = 0; i < len; i++) {
        char c = text[i];

        if (c == '\\' && i + 1 < len) {
            char escaped = text[++i];
            if (escaped == '\'') fputc('\'', cg->out);
            else                 fprintf(cg->out, "\\%c", escaped);
            continue;
        }

        if (c == '"') fputs("\\\"", cg->out);
        else          fputc(c, cg->out);
    }

    fputs("\"\n", cg->out);
}

// Emite o objeto e devolve o id do rótulo. A seção é trocada no meio da emissão e devolvida em
// seguida: o literal é descoberto enquanto o `.text` é escrito, e juntar tudo numa passagem prévia
// custaria uma varredura extra da árvore só para isso.
static size_t emit_string_object(Codegen *cg, const char *text, uint32_t len) {
    size_t id     = cg->string_counter++;
    size_t nbytes = decoded_length(text, len);

    fprintf(cg->out, "    .section .rodata\n");
    fprintf(cg->out, "strobj_%zu:\n", id);
    fprintf(cg->out, "    .quad   strbytes_%zu\n", id);
    fprintf(cg->out, "    .quad   %zu\n", nbytes);
    fprintf(cg->out, "    .quad   %zu\n", nbytes);
    fprintf(cg->out, "strbytes_%zu:\n", id);
    emit_string_bytes(cg, text, len);

    return id;
}

// ------------------------------------------------------------------------------------------------
// Operações binárias
// ------------------------------------------------------------------------------------------------

// Emite `setcc` + zero-extend: o resultado de uma comparação é 0 ou 1 em `%rax`.
static void gen_compare(Codegen *cg, const char *setcc) {
    fprintf(cg->out, "    cmpq    %%rcx, %%rax\n");
    fprintf(cg->out, "    %-7s %%al\n", setcc);
    fprintf(cg->out, "    movzbq  %%al, %%rax\n");
}

// Esquerda em `%rax`, direita em `%rcx`; resultado em `%rax`.
static bool gen_binary_op(Codegen *cg, const Expr *e) {
    switch (e->as.binary.op) {
        case OpAdd: fprintf(cg->out, "    addq    %%rcx, %%rax\n"); return true;
        case OpSub: fprintf(cg->out, "    subq    %%rcx, %%rax\n"); return true;
        case OpMul: fprintf(cg->out, "    imulq   %%rcx, %%rax\n"); return true;

        // `cqto` estende o sinal de %rax para %rdx:%rax, que é o dividendo de `idivq`.
        case OpDiv:
            fprintf(cg->out, "    cqto\n");
            fprintf(cg->out, "    idivq   %%rcx\n");
            return true;

        case OpEq:   gen_compare(cg, "sete");  return true;
        case OpGt:   gen_compare(cg, "setg");  return true;
        case OpLt:   gen_compare(cg, "setl");  return true;
        case OpGtEq: gen_compare(cg, "setge"); return true;
        case OpLtEq: gen_compare(cg, "setle"); return true;
    }
    return false;
}

// ------------------------------------------------------------------------------------------------
// Chamadas
// ------------------------------------------------------------------------------------------------

// *Mangling* do símbolo de uma função do usuário: `soma` vira `tarm_soma`. `main` é a exceção — o
// `_start` de `runtime/takeoff.s` chama esse nome exato, então ele precisa sair sem prefixo.
//
// Como no Tarmac em C++, o prefixo separa o que o usuário declarou do que o assembler e o linker
// já usam. Ele não isola das rotinas de runtime, que também são `tarm_*`: uma função chamada
// `print_int` colidiria com `tarm_print_int` na hora do link.
static void mangle(const Expr *decl_or_call, const char *name, uint32_t name_len,
                   char *buf, size_t buf_size) {
    (void)decl_or_call;

    if (name_len == 4 && memcmp(name, "main", 4) == 0)
        snprintf(buf, buf_size, "main");
    else
        snprintf(buf, buf_size, "tarm_%.*s", (int)name_len, name);
}

// Rótulo do runtime (nativa) ou o nome com *mangling* (usuário). Devolve NULL quando a assinatura
// não está na tabela ou o tipo não tem implementação — o que, depois das barreiras da semântica,
// significa bug do compilador.
static const char *call_symbol(Codegen *cg, const Expr *e, char *buf, size_t buf_size) {
    const bool is_method = (e->kind == ExprMethod);

    const FunctionSignature *sig =
        is_method ? tarm_function_table_find_method(cg->functions, e->as.call.name,
                                                    e->as.call.name_len,
                                                    e->as.call.args[0]->type)
                  : tarm_function_table_find(cg->functions, e->as.call.name, e->as.call.name_len);

    if (!sig) return NULL;

    if (!sig->is_native) {
        mangle(e, e->as.call.name, e->as.call.name_len, buf, buf_size);
        return buf;
    }

    // Nativa com despacho por tipo (`print`): é o tipo já anotado no argumento que escolhe a rotina.
    DataType dispatch = Void;
    if (sig->dispatch_param >= 0 && (size_t)sig->dispatch_param < e->as.call.arg_count)
        dispatch = e->as.call.args[sig->dispatch_param]->type;

    return tarm_function_table_symbol(sig, dispatch);
}

// `print` é variádica: cada argumento vira uma chamada própria à rotina do runtime que corresponde
// ao seu tipo (`tarm_print_int`, `tarm_print_str`, ...). Nenhuma delas insere `\n` — a quebra de
// linha é do programa, como em C. Ver docs/runtime.md#convencoes-especificas-de-io.
static bool gen_print(Codegen *cg, const Expr *e) {
    const FunctionSignature *sig =
        tarm_function_table_find(cg->functions, e->as.call.name, e->as.call.name_len);
    if (!sig) {
        unsupported(cg, e, "chamada a 'print' sem assinatura registrada");
        return false;
    }

    for (size_t i = 0; i < e->as.call.arg_count; i++) {
        const Expr *arg = e->as.call.args[i];

        const char *symbol = tarm_function_table_symbol(sig, arg->type);
        if (!symbol) {
            unsupported(cg, arg, "imprimir um valor desse tipo");
            return false;
        }

        if (!gen_expr(cg, arg)) return false;

        fprintf(cg->out, "    movq    %%rax, %%rdi\n");
        int pad = call_align(cg);
        fprintf(cg->out, "    call    %s\n", symbol);
        call_unalign(cg, pad);
    }

    fprintf(cg->out, "    xorl    %%eax, %%eax\n");
    return true;
}

// Argumentos são avaliados um a um e empilhados; só depois vão para os registradores da ABI. Sem
// isso, avaliar o segundo argumento destruiria o primeiro, que já estaria em %rdi.
//
// Num método, o receptor é `args[0]` (convenção do Parser) e entra na sequência como qualquer
// outro argumento — é por isso que a mesma função serve aos dois casos.
static bool gen_call(Codegen *cg, const Expr *e) {
    size_t argc = e->as.call.arg_count;

    if (argc > MAX_REG_ARGS) {
        unsupported(cg, e, "chamadas com mais de 6 argumentos");
        return false;
    }

    char        buf[128];
    const char *symbol = call_symbol(cg, e, buf, sizeof buf);
    if (!symbol) {
        tarm_error_at(cg->diag, e->line, e->col,
                      "chamada sem símbolo resolvido: '%.*s'",
                      (int)e->as.call.name_len, e->as.call.name);
        return false;
    }

    for (size_t i = 0; i < argc; i++) {
        if (!gen_expr(cg, e->as.call.args[i])) return false;
        emit_push(cg, "%rax");
    }

    // Desempilha na ordem inversa: o último empilhado é o último argumento.
    for (size_t i = argc; i-- > 0; )
        emit_pop(cg, ARG_REGS[i]);

    int pad = call_align(cg);
    fprintf(cg->out, "    call    %s\n", symbol);
    call_unalign(cg, pad);
    return true;
}

// ------------------------------------------------------------------------------------------------
// Expressões
// ------------------------------------------------------------------------------------------------

// Toda expressão deixa o resultado em `%rax`.
static bool gen_expr(Codegen *cg, const Expr *e) {
    if (!e) return true;

    char operand[64];

    switch (e->kind) {

    case ExprInteger:
        fprintf(cg->out, "    movq    $%lld, %%rax\n", (long long)e->as.integer.value);
        return true;

    case ExprBool:
        fprintf(cg->out, "    movq    $%d, %%rax\n", e->as.boolean.value ? 1 : 0);
        return true;

    case ExprChar:
        fprintf(cg->out, "    movq    $%d, %%rax\n", (int)(unsigned char)e->as.char_lit.value);
        return true;

    case ExprIdentifier:
        if (!var_operand(cg, e->as.identifier.name, e->as.identifier.len,
                         operand, sizeof operand)) {
            tarm_error_at(cg->diag, e->line, e->col,
                          "variável sem slot na geração de código: '%.*s'",
                          (int)e->as.identifier.len, e->as.identifier.name);
            return false;
        }
        fprintf(cg->out, "    movq    %s, %%rax\n", operand);
        return true;

    // O lado esquerdo é empilhado enquanto o direito é avaliado — avaliar direto em %rcx perderia
    // o valor assim que o lado direito usasse o registrador.
    case ExprBinary:
        if (!gen_expr(cg, e->as.binary.left))  return false;
        emit_push(cg, "%rax");
        if (!gen_expr(cg, e->as.binary.right)) return false;
        fprintf(cg->out, "    movq    %%rax, %%rcx\n");
        emit_pop(cg, "%rax");
        return gen_binary_op(cg, e);

    case ExprAssign:
        if (!gen_expr(cg, e->as.assign.value)) return false;
        if (!var_operand(cg, e->as.assign.name, e->as.assign.name_len,
                         operand, sizeof operand)) {
            tarm_error_at(cg->diag, e->line, e->col,
                          "variável sem slot na geração de código: '%.*s'",
                          (int)e->as.assign.name_len, e->as.assign.name);
            return false;
        }
        fprintf(cg->out, "    movq    %%rax, %s\n", operand);
        return true;

    // Local: reserva o slot agora (o prólogo já contou com ele) e grava o valor inicial.
    // Global: o dado já está em `.data`; aqui só resta o valor inicial, se houver.
    case ExprVarDecl: {
        if (e->as.var_decl.frame == Global) {
            if (!e->as.var_decl.initializer) return true;
            if (!gen_expr(cg, e->as.var_decl.initializer)) return false;
            if (!var_operand(cg, e->as.var_decl.name, e->as.var_decl.name_len,
                             operand, sizeof operand)) return false;
            fprintf(cg->out, "    movq    %%rax, %s\n", operand);
            return true;
        }

        int offset;
        if (!declare_local(cg, e, &offset)) {
            tarm_error_at(cg->diag, e->line, e->col,
                          "não foi possível reservar slot para '%.*s'",
                          (int)e->as.var_decl.name_len, e->as.var_decl.name);
            return false;
        }

        if (e->as.var_decl.initializer) {
            if (!gen_expr(cg, e->as.var_decl.initializer)) return false;
        } else {
            fprintf(cg->out, "    xorl    %%eax, %%eax\n");
        }
        fprintf(cg->out, "    movq    %%rax, %d(%%rbp)\n", offset);
        return true;
    }

    // As conversões da linguagem partem sempre de `Int`. Em slots de 64 bits, alargar é no-op;
    // só truncar (Char) e normalizar (Bool) exigem instrução.
    case ExprCast:
        if (!gen_expr(cg, e->as.cast.operand)) return false;
        switch (e->as.cast.castKind) {
            case IntToInt64:
                return true;
            case IntToChar:
                fprintf(cg->out, "    movzbq  %%al, %%rax\n");
                return true;
            case IntToBool:
                fprintf(cg->out, "    testq   %%rax, %%rax\n");
                fprintf(cg->out, "    setne   %%al\n");
                fprintf(cg->out, "    movzbq  %%al, %%rax\n");
                return true;
            case IntToFloat:
                unsupported(cg, e, "conversão para 'float'");
                return false;
        }
        return false;

    // O objeto vai para `.rodata` e o que fica em `%rax` é o ponteiro para o header — o mesmo que
    // uma String alocada em tempo de execução entregaria.
    case ExprString: {
        size_t id = emit_string_object(cg, e->as.string_lit.text, e->as.string_lit.len);
        fprintf(cg->out, "    .text\n");
        fprintf(cg->out, "    leaq    strobj_%zu(%%rip), %%rax\n", id);
        return true;
    }

    case ExprCall: {
        bool is_print = e->as.call.name_len == 5 &&
                        memcmp(e->as.call.name, "print", 5) == 0;
        return is_print ? gen_print(cg, e) : gen_call(cg, e);
    }

    case ExprMethod:
        return gen_call(cg, e);

    // --- instruções de fluxo -----------------------------------------------------------------

    case ExprConditional: {
        size_t l_else = next_label(cg);
        size_t l_end  = next_label(cg);

        if (!gen_expr(cg, e->as.conditional.cond)) return false;
        fprintf(cg->out, "    testq   %%rax, %%rax\n");
        fprintf(cg->out, "    je      .L%zu\n", l_else);

        if (!gen_block(cg, e->as.conditional.then_body, e->as.conditional.then_count))
            return false;
        fprintf(cg->out, "    jmp     .L%zu\n", l_end);

        fprintf(cg->out, ".L%zu:\n", l_else);
        if (!gen_block(cg, e->as.conditional.else_body, e->as.conditional.else_count))
            return false;

        fprintf(cg->out, ".L%zu:\n", l_end);
        return true;
    }

    case ExprWhile: {
        size_t l_top = next_label(cg);
        size_t l_end = next_label(cg);

        fprintf(cg->out, ".L%zu:\n", l_top);
        if (!gen_expr(cg, e->as.while_loop.cond)) return false;
        fprintf(cg->out, "    testq   %%rax, %%rax\n");
        fprintf(cg->out, "    je      .L%zu\n", l_end);

        if (!gen_block(cg, e->as.while_loop.body, e->as.while_loop.body_count))
            return false;
        fprintf(cg->out, "    jmp     .L%zu\n", l_top);
        fprintf(cg->out, ".L%zu:\n", l_end);
        return true;
    }

    // O epílogo fica no rótulo do fim da função, para não duplicá-lo a cada `return`.
    case ExprReturn:
        if (e->as.ret.value) {
            if (!gen_expr(cg, e->as.ret.value)) return false;
        } else {
            fprintf(cg->out, "    xorl    %%eax, %%eax\n");
        }
        fprintf(cg->out, "    jmp     .Lret%zu\n", cg->return_label);
        return true;

    // --- ainda não suportados ----------------------------------------------------------------

    case ExprFloat:
        unsupported(cg, e, "literais de ponto flutuante");
        return false;

    case ExprFuncDecl:
        unsupported(cg, e, "função aninhada");
        return false;
    }

    return false;
}

static bool gen_block(Codegen *cg, Expr *const *items, size_t count) {
    for (size_t i = 0; i < count; i++)
        if (!gen_expr(cg, items[i])) return false;
    return true;
}

// ------------------------------------------------------------------------------------------------
// Funções
// ------------------------------------------------------------------------------------------------

static bool gen_function(Codegen *cg, const Expr *e) {
    // Cada função tem seu próprio frame: os offsets recomeçam do topo e as variáveis da função
    // anterior saem da tabela, de modo que os mesmos nomes possam ser reutilizados. O rótulo de
    // epílogo também é novo — dois `.Lret` iguais seriam redefinição de símbolo para o assembler.
    size_t scope     = tarm_symbol_table_scope_begin(&cg->symbols);
    cg->return_label = next_label(cg);
    cg->stack_depth  = 0;

    size_t slots = e->as.func_decl.param_count
                 + count_slots(e->as.func_decl.body, e->as.func_decl.body_count);

    // Alinhado em 16 pela ABI; os 32 bytes iniciais do frame ficam reservados.
    size_t frame = ((slots * SLOT_SIZE + 32) + 15) & ~(size_t)15;

    if (e->as.func_decl.param_count > MAX_REG_ARGS) {
        unsupported(cg, e, "funções com mais de 6 parâmetros");
        return false;
    }

    char symbol[128];
    mangle(e, e->as.func_decl.name, e->as.func_decl.name_len, symbol, sizeof symbol);

    fprintf(cg->out, "\n    .text\n");
    fprintf(cg->out, "    .globl  %s\n", symbol);
    fprintf(cg->out, "    .type   %s, @function\n", symbol);
    fprintf(cg->out, "%s:\n", symbol);

    fprintf(cg->out, "    pushq   %%rbp\n");
    fprintf(cg->out, "    movq    %%rsp, %%rbp\n");
    fprintf(cg->out, "    subq    $%zu, %%rsp\n", frame);

    // Os parâmetros chegam em registradores; o corpo os lê como variáveis, então cada um vai para
    // o seu slot logo na entrada.
    for (size_t i = 0; i < e->as.func_decl.param_count; i++) {
        const Expr *p = e->as.func_decl.params[i];
        int offset;
        if (!declare_local(cg, p, &offset)) {
            tarm_error_at(cg->diag, p->line, p->col, "parâmetro duplicado");
            return false;
        }
        fprintf(cg->out, "    movq    %s, %d(%%rbp)\n", ARG_REGS[i], offset);
    }

    if (!gen_block(cg, e->as.func_decl.body, e->as.func_decl.body_count))
        return false;

    // Queda pelo fim sem `return`: devolve 0.
    fprintf(cg->out, "    xorl    %%eax, %%eax\n");
    fprintf(cg->out, ".Lret%zu:\n", cg->return_label);
    fprintf(cg->out, "    movq    %%rbp, %%rsp\n");
    fprintf(cg->out, "    popq    %%rbp\n");
    fprintf(cg->out, "    ret\n");

    tarm_symbol_table_scope_end(&cg->symbols, scope);
    return true;
}

// ------------------------------------------------------------------------------------------------
// Programa
// ------------------------------------------------------------------------------------------------

void tarm_codegen_init(Codegen *cg, FILE *out, Diagnostics *diag, FunctionTable *functions) {
    cg->out            = out;
    cg->diag           = diag;
    cg->functions      = functions;
    cg->label_counter  = 0;
    cg->string_counter = 0;
    cg->stack_depth    = 0;
    cg->return_label   = 0;
    tarm_symbol_table_init(&cg->symbols);
}

void tarm_codegen_free(Codegen *cg) {
    tarm_symbol_table_free(&cg->symbols);
}

bool tarm_codegen_generate(Codegen *cg, Expr **program, size_t count) {
    // --- primeira passagem: dados estáticos -------------------------------------------------
    //
    // Globais precisam existir em `.data` antes de qualquer função referenciá-las, e é aqui que
    // ganham o rótulo `globobj_N` que `var_operand` vai emitir. O valor inicial precisa ser um
    // literal constante — a semântica já barrou o resto, porque não há onde executar código antes
    // de o programa começar.
    fprintf(cg->out, "    .data\n");

    for (size_t i = 0; i < count; i++) {
        const Expr *e = program[i];
        if (!e || e->kind != ExprVarDecl) continue;

        size_t label_id;
        if (!tarm_symbol_table_declare_global(&cg->symbols,
                                              e->as.var_decl.name, e->as.var_decl.name_len,
                                              e->as.var_decl.type, SLOT_SIZE, &label_id)) {
            tarm_error_at(cg->diag, e->line, e->col, "variável global duplicada: '%.*s'",
                          (int)e->as.var_decl.name_len, e->as.var_decl.name);
            return false;
        }

        // A coerção implícita pode ter envolvido o literal num `ExprCast`; o valor está embaixo.
        const Expr *init = e->as.var_decl.initializer;
        while (init && init->kind == ExprCast) init = init->as.cast.operand;

        // Uma String global guarda o **ponteiro** para o objeto do literal, emitido em `.rodata`
        // logo antes — daí a troca de seção no meio da lista de globais.
        if (init && init->kind == ExprString) {
            size_t id = emit_string_object(cg, init->as.string_lit.text, init->as.string_lit.len);
            fprintf(cg->out, "    .data\n");
            fprintf(cg->out, "    .align  8\n");
            fprintf(cg->out, "globobj_%zu:\n", label_id);
            fprintf(cg->out, "    .quad   strobj_%zu\n", id);
            continue;
        }

        long long initial = 0;
        if (init) {
            switch (init->kind) {
                case ExprInteger: initial = (long long)init->as.integer.value;          break;
                case ExprBool:    initial = init->as.boolean.value ? 1 : 0;             break;
                case ExprChar:    initial = (long long)(unsigned char)init->as.char_lit.value; break;
                default:
                    tarm_error_at(cg->diag, e->line, e->col,
                                  "inicializador de global não suportado para '%.*s'",
                                  (int)e->as.var_decl.name_len, e->as.var_decl.name);
                    return false;
            }
        }

        fprintf(cg->out, "    .align  8\n");
        fprintf(cg->out, "globobj_%zu:\n", label_id);
        fprintf(cg->out, "    .quad   %lld\n", initial);
    }

    // --- segunda passagem: código ------------------------------------------------------------
    fprintf(cg->out, "\n    .text\n");

    for (size_t i = 0; i < count; i++) {
        const Expr *e = program[i];
        if (!e || e->kind != ExprFuncDecl) continue;
        if (!gen_function(cg, e)) return false;
    }

    // Declara que o programa não precisa de stack executável. Sem isso o linker emite aviso e
    // marca o binário com stack executável, o que alguns sistemas recusam.
    fprintf(cg->out, "\n    .section .note.GNU-stack,\"\",@progbits\n");

    return !tarm_diag_has_errors(cg->diag);
}