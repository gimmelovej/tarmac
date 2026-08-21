// ================================================================================================
// File: driver.c
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================

// `mkstemps`, `environ` e as funções de `<spawn.h>`/`<unistd.h>` usadas aqui são POSIX/GNU, e a
// build fixa `-std=c11` sem extensões — sem esta macro, definida **antes** de qualquer include,
// elas não seriam declaradas e entrariam por declaração implícita.
#define _DEFAULT_SOURCE

#ifndef TARM_RUNTIME_DIR
#define TARM_RUNTIME_DIR "runtime"
#endif

#include "driver.h"
#include "errors.h"
#include "file.h"
#include "lexer.h"
#include "arena.h"
#include "parser.h"
#include "semantic.h"
#include "symbol_table.h"
#include "function_table.h"
#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>

// Ambiente do processo, herdado pelos filhos.
extern char **environ;

// Arquivos de runtime montados e linkados junto com o programa. Cada um vira um `.o` temporário
// próprio; ver `load_runtime_assembly`.
static const char *RUNTIME_SOURCES[] = {
    "object.s", "alloc.s", "audio.s", "io.s", "string.s", "takeoff.s",
};
#define RUNTIME_SOURCE_COUNT (sizeof RUNTIME_SOURCES / sizeof *RUNTIME_SOURCES)

// Teto de argumentos na linha de link: `ld`, `--gc-sections`, o objeto do programa, os da runtime,
// `-o`, o alvo e o NULL final.
#define MAX_LINK_ARGS (RUNTIME_SOURCE_COUNT + 8)

// ------------------------------------------------------------------------------------------------
// Execução de processos externos
// ------------------------------------------------------------------------------------------------

// Executa um programa sem passar por um shell.
//
// `posix_spawnp` faz, numa chamada só, o que tradicionalmente seria fork+exec:
//   fork -> duplica o processo atual (o filho continua no mesmo ponto do código)
//   exec -> substitui a imagem do filho pelo programa pedido
// Como não há shell, cada elemento de `argv` chega ao programa como argumento literal: espaços,
// aspas e ';' em caminhos são apenas caracteres.
//
// `argv` precisa terminar em NULL, e os ponteiros precisam continuar válidos até o spawn retornar.
static bool run_or_fail(Diagnostics *diag, char *const argv[]) {
    if (!argv || !argv[0]) {
        tarm_error(diag, "run_or_fail: lista de argumentos vazia");
        return false;
    }

    pid_t pid = 0;

    // O sufixo 'p' de spawnp = procura o executável no PATH. Os dois NULL são file actions
    // (redirecionar descritores) e atributos; sem eles, o filho herda stdin/stdout/stderr.
    int rc = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
    if (rc != 0) { // devolve o código de erro direto, NÃO usa errno
        tarm_error(diag, "falha ao iniciar '%s': %s", argv[0], strerror(rc));
        return false;
    }

    // Sem esperar, seguiríamos para o passo seguinte antes deste terminar — e o filho viraria
    // zumbi (o kernel guarda o status até alguém recolhê-lo).
    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) { // aqui SIM é errno; um sinal só interrompeu a espera
            tarm_error(diag, "waitpid falhou para '%s'", argv[0]);
            return false;
        }
    }

    // `status` é um valor empacotado, não o código de saída: precisa das macros.
    if (WIFSIGNALED(status)) {
        tarm_error(diag, "%s morto pelo sinal %d", argv[0], WTERMSIG(status));
        return false;
    }
    if (!WIFEXITED(status)) {
        tarm_error(diag, "%s terminou de forma anômala", argv[0]);
        return false;
    }
    if (WEXITSTATUS(status) != 0) {
        tarm_error(diag, "%s falhou (código %d)", argv[0], WEXITSTATUS(status));
        return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------
// Caminhos
// ------------------------------------------------------------------------------------------------

// `exemplo.tm` + ".s" -> `exemplo.s`; `exemplo.tm` + "" -> `exemplo`. Devolve uma string nova, do
// chamador, ou NULL se faltar memória.
//
// Como no Tarmac em C++, procura o último '.' do caminho inteiro — um diretório com ponto no nome
// (`./v1.2/programa`) confunde a divisão. Pendência conhecida.
static char *replace_extension(const char *path, const char *new_ext) {
    const char *dot      = strrchr(path, '.');
    size_t      base_len = dot ? (size_t)(dot - path) : strlen(path);
    size_t      ext_len  = strlen(new_ext);

    char *out = malloc(base_len + ext_len + 1);
    if (!out) return NULL;

    memcpy(out, path, base_len);
    memcpy(out + base_len, new_ext, ext_len);
    out[base_len + ext_len] = '\0';
    return out;
}

// `runtime` + "print.s" -> `runtime/print.s`.
static char *join_path(const char *dir, const char *name) {
    size_t dir_len  = strlen(dir);
    size_t name_len = strlen(name);

    char *out = malloc(dir_len + 1 + name_len + 1);
    if (!out) return NULL;

    memcpy(out, dir, dir_len);
    out[dir_len] = '/';
    memcpy(out + dir_len + 1, name, name_len);
    out[dir_len + 1 + name_len] = '\0';
    return out;
}

// ------------------------------------------------------------------------------------------------
// Runtime
// ------------------------------------------------------------------------------------------------

// Objetos temporários criados por `load_runtime_assembly`. Ficam registrados aqui para que a
// limpeza do Driver os remova mesmo quando a montagem falha no meio da lista.
typedef struct {
    char  *paths[RUNTIME_SOURCE_COUNT];
    size_t count;
} TempObjects;

static void temp_objects_cleanup(TempObjects *tmp) {
    for (size_t i = 0; i < tmp->count; i++) {
        if (tmp->paths[i]) {
            unlink(tmp->paths[i]);
            free(tmp->paths[i]);
            tmp->paths[i] = NULL;
        }
    }
    tmp->count = 0;
}

// Monta cada `.s` da runtime em um `.o` temporário próprio (`as`; o nome é gerado por `mkstemps` no
// diretório temporário do sistema) e linka todos com o objeto do programa (`ld`).
//
// O `-I runtime_dir` deixa o `as` resolver `.include "object.inc"` — constantes de layout
// compartilhadas entre arquivos que são montados isoladamente. `--gc-sections` descarta seções não
// referenciadas, mantendo só as rotinas efetivamente usadas.
static bool load_runtime_assembly(Diagnostics *diag, const char *runtime_dir,
                                  const char *program_obj, const char *target, TempObjects *tmp) {
    char  *link_argv[MAX_LINK_ARGS];
    size_t link_n = 0;

    link_argv[link_n++] = "ld";
    link_argv[link_n++] = "--gc-sections";
    link_argv[link_n++] = (char *)program_obj;

    for (size_t i = 0; i < RUNTIME_SOURCE_COUNT; i++) {
        char *src_path = join_path(runtime_dir, RUNTIME_SOURCES[i]);
        if (!src_path) {
            tarm_system_error("não foi possível montar o caminho do arquivo de runtime");
            return false;
        }

        // Confere a existência antes de chamar o `as`: a mensagem daqui nomeia o arquivo de
        // runtime que falta, enquanto a do montador falaria de um caminho solto.
        if (access(src_path, R_OK) != 0) {
            tarm_error(diag, "não foi possível carregar o arquivo de runtime: %s",
                       RUNTIME_SOURCES[i]);
            free(src_path);
            return false;
        }

        // mkstempS (com S) preserva o sufixo: os 2 últimos caracteres, ".o", ficam intactos.
        static const char template[] = "/tmp/tarm-XXXXXX.o";
        char *obj_path               = malloc(sizeof template);
        if (!obj_path) {
            tarm_system_error("não foi possível alocar o caminho do objeto temporário");
            free(src_path);
            return false;
        }
        memcpy(obj_path, template, sizeof template);

        int fd = mkstemps(obj_path, 2);
        if (fd == -1) {
            tarm_error(diag, "falha ao criar objeto temporário: %s", strerror(errno));
            free(obj_path);
            free(src_path);
            return false;
        }
        close(fd); // o `as` reabre o caminho; aqui só interessava reservar o nome

        // Registrado antes da montagem: se o `as` falhar, a limpeza ainda remove o arquivo.
        tmp->paths[tmp->count++] = obj_path;

        char *as_argv[] = {"as",
                           "-I",
                           (char *)runtime_dir,
                           "--fatal-warnings",
                           "--noexecstack",
                           src_path,
                           "-o",
                           obj_path,
                           NULL};
        bool  assembled = run_or_fail(diag, as_argv);
        free(src_path);
        if (!assembled) return false;

        link_argv[link_n++] = obj_path;
    }

    link_argv[link_n++] = "-o";
    link_argv[link_n++] = (char *)target;
    link_argv[link_n]   = NULL;

    return run_or_fail(diag, link_argv);
}

// ------------------------------------------------------------------------------------------------
// Pipeline
// ------------------------------------------------------------------------------------------------

// Uma compilação inteira, do argumento de linha de comando ao executável. Todo recurso nasce aqui e
// morre no rótulo `cleanup` — um ponto único de saída, no lugar de liberar em cada retorno
// intermediário.
//
// Todos os recursos são declarados e inicializados **antes** do primeiro `goto`. Em C o salto não
// executa inicializador nenhum que esteja no caminho: com a declaração no meio da função, um
// `goto cleanup` disparado antes dela chegaria à limpeza com a variável indeterminada, e o `free`
// sobre lixo derrubaria o processo.
bool tarm_drive(int argc, char *argv[]) {
    Diagnostics   diag;
    SymbolTable   symbols;
    FunctionTable functions;
    Arena         arena;
    TokenList     toks     = {0};
    TempObjects   tmp_objs = {{0}, 0};

    char  *source    = NULL;
    char  *asm_path  = NULL;
    char  *obj_path  = NULL;
    char  *exe_path  = NULL;
    FILE  *out       = NULL;
    Expr **program   = NULL;
    size_t program_n = 0;
    size_t len       = 0;
    bool   generated = false;
    bool   ok        = false;

    tarm_diag_init(&diag);
    tarm_symbol_table_init(&symbols);
    tarm_function_table_init(&functions);
    arena_init(&arena);

    if (argc < 2) {
        tarm_error(&diag, "uso: tarm <arquivo_alvo>");
        goto cleanup;
    }

    // --- leitura -----------------------------------------------------------------------------
    //
    // O buffer do arquivo precisa viver mais que o Lexer: os tokens apontam para dentro dele (ver
    // `Token`, em types.h), e os nós da AST herdam essas fatias. Por isso ele só é liberado na
    // limpeza, depois de todo uso — inclusive o da geração de código.
    File fl;
    tarm_file_init(&fl, &diag);

    source = tarm_read_entire_file(&fl, argv[1], &len);
    if (!source) goto cleanup; // o erro já foi registrado pela leitura

    // --- análise léxica ----------------------------------------------------------------------
    Lexer lx;
    tarm_lexer_init(&lx, source, len, &diag);

    if (!tarm_lexer_tokenize(&lx, &toks)) goto cleanup;

    // Barreira: a varredura pode ter chegado ao fim com tokens `Invalid` pelo caminho, e não faz
    // sentido pedir uma árvore de uma sequência que já se sabe errada. É aqui que o "acumular em
    // vez de lançar" volta a virar decisão de fluxo.
    if (tarm_diag_has_errors(&diag)) goto cleanup;

    // Um `EndOfFile` logo no primeiro token significa entrada só com espaços e comentários. Sem
    // esta recusa, o pipeline seguiria até produzir um objeto sem símbolo nenhum, e o erro só
    // apareceria no `ld`, sem relação visível com a causa.
    if (toks.count == 0 || toks.data[0].kind == EndOfFile) {
        tarm_error(&diag, "não há conteúdo para compilar (arquivo de entrada vazio)");
        goto cleanup;
    }

    // --- análise sintática -------------------------------------------------------------------
    //
    // A arena é liberada apenas na limpeza: a AST vive nela, e a análise semântica e a geração de
    // código ainda vão percorrê-la. Liberá-la aqui invalidaria a árvore inteira de uma vez.
    Parser ps;
    tarm_parser_init(&ps, &toks, &diag, &arena);

    if (!tarm_parser_program(&ps, &program, &program_n)) goto cleanup;

    if (tarm_diag_has_errors(&diag)) goto cleanup;

    // --- análise semântica -------------------------------------------------------------------
    if (!tarm_function_table_register_natives(&functions)) goto cleanup;

    SemanticAnalyzer an;
    tarm_semantic_init(&an, &diag, &arena, &symbols, &functions);

    if (!tarm_semantic_analyse(&an, program, program_n)) goto cleanup;

    if (tarm_diag_has_errors(&diag)) goto cleanup;

    // --- geração de código -------------------------------------------------------------------
    //
    // Última etapa do compilador propriamente dito, e a única que só roda com o diagnóstico limpo:
    // todas as barreiras acima já passaram, então qualquer inconsistência daqui em diante é bug do
    // compilador, não erro do usuário.
    asm_path = replace_extension(argv[1], ".s");
    exe_path = replace_extension(argv[1], "");
    if (!asm_path || !exe_path) {
        tarm_system_error("não foi possível montar os caminhos de saída");
        goto cleanup;
    }

    obj_path = malloc(strlen(exe_path) + 3); // ".o" + '\0'
    if (!obj_path) {
        tarm_system_error("não foi possível montar o caminho do objeto");
        goto cleanup;
    }
    sprintf(obj_path, "%s.o", exe_path);

    out = fopen(asm_path, "w");
    if (!out) {
        tarm_error(&diag, "não foi possível abrir para escrita: %s", asm_path);
        goto cleanup;
    }

    Codegen cg;
    tarm_codegen_init(&cg, out, &diag, &functions);
    generated = tarm_codegen_generate(&cg, program, program_n);
    tarm_codegen_free(&cg);

    // Fechado antes do `as`, que vai lê-lo: com o buffer ainda na mão do compilador, o montador
    // leria um arquivo truncado. O código de `fclose` é conferido porque é nele que aparece uma
    // falha de escrita adiada (disco cheio, por exemplo).
    if (fclose(out) != 0) {
        out = NULL;
        tarm_error(&diag, "falha ao escrever o assembly em: %s", asm_path);
        goto cleanup;
    }
    out = NULL;

    if (!generated) goto cleanup;

    // --- montagem e link ---------------------------------------------------------------------
    //
    // O `&&` do shell vira sequência normal: se o `as` falhar, `run_or_fail` devolve false e o
    // `ld` nunca é alcançado.
    {
        char *as_argv[] = {"as", asm_path, "-o", obj_path, NULL};
        if (!run_or_fail(&diag, as_argv)) goto cleanup;
    }

    if (!load_runtime_assembly(&diag, TARM_RUNTIME_DIR, obj_path, exe_path, &tmp_objs))
        goto cleanup;

    // O objeto do programa é intermediário, como os da runtime: sai junto. O `.s` fica, porque é o
    // que se quer ler quando a curiosidade é sobre o código gerado.
    unlink(obj_path);

    fprintf(stdout, "Executável gerado: %s\n", exe_path);
    ok = true;

cleanup:
    // Ordem inversa da criação. Nenhuma destas chamadas depende de a etapa correspondente ter
    // rodado: todas partem de um estado inicializado no topo da função.
    if (out) fclose(out);

    temp_objects_cleanup(&tmp_objs); // remove os `.o` da runtime, inclusive em caso de falha

    arena_free(&arena); // derruba a AST inteira
    tarm_function_table_free(&functions);
    tarm_symbol_table_free(&symbols);
    tarm_lexer_tokens_free(&toks);

    free(obj_path);
    free(exe_path);
    free(asm_path);
    free(source);

    // O resumo sai do `Diagnostics` e não do caminho percorrido: erros acumulados em qualquer
    // etapa chegam aqui, mesmo que a etapa tenha seguido em frente depois deles.
    if (diag.error_count > 0) fprintf(stderr, "%u erro(s) encontrado(s).\n", diag.error_count);

    return ok;
}
