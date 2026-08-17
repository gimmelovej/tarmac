// ================================================================================================
// File: erros.c
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
#include "errors.h"
#include <stdarg.h>
#include <stdio.h>

void tarm_diag_init(Diagnostics *d) {
    d->error_count = 0;
    d->max_errors  = 20;
}

bool tarm_diag_has_errors(const Diagnostics *d) {
    return d->error_count > 0;
}

// Caminho único de saída das mensagens de erro: as variádicas públicas só montam o prefixo e
// delegam aqui, para que formato e contagem não divirjam entre elas.
//
// O limite existe porque um único erro de sintaxe costuma fazer o Parser produzir dezenas de
// erros derivados — a partir de `max_errors` a contagem continua subindo (o resumo final segue
// verdadeiro), mas a impressão para, com um aviso emitido uma única vez.
static void report(Diagnostics *d, const char *prefix,
                   const char *fmt, va_list ap) {
    if (d->max_errors && d->error_count == d->max_errors) {
        fprintf(stderr, "erros demais, suprimindo o restante.\n");
        d->error_count++;
        return;
    }
    if (d->max_errors && d->error_count > d->max_errors) {
        d->error_count++;
        return;
    }

    fputs(prefix, stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    d->error_count++;
}

void tarm_error_at(Diagnostics *d, uint32_t line, uint32_t col,
                   const char *fmt, ...) {
    char prefix[64];
    snprintf(prefix, sizeof prefix, "erro %u:%u: ", line, col);

    va_list ap;
    va_start(ap, fmt);
    report(d, prefix, fmt, ap);
    va_end(ap);
}

void tarm_error(Diagnostics *d, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    report(d, "erro: ", fmt, ap);
    va_end(ap);
}

// Falha do ambiente, não do programa `.tm`: `perror` acrescenta a descrição de `errno`, e nada é
// contabilizado no `Diagnostics` (ver errors.h).
void tarm_system_error(const char *message) {
    perror(message);
}
