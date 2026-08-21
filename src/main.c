// ================================================================================================
// File: main.c
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
// Ponto de entrada do executável `tarm`: repassa a linha de comando ao Driver, que é quem conhece
// o pipeline, e traduz o resultado num código de saída.
//
// O `bool` do Driver vira `EXIT_SUCCESS`/`EXIT_FAILURE`, e não o valor cru: no shell, 0 é sucesso
// — devolver o booleano direto inverteria o significado para quem encadeia `tarm x.tm && ./x`.
#include <stdio.h>
#include <stdlib.h>
#include "driver.h"

int main(int argc, char *argv[]) {
    bool ok = tarm_drive(argc, argv);

    if (ok)
        printf("[TARMAC]: Compilação finalizada com sucesso.\n");
    else
        printf("[TARMAC]: Compilação interrompida.\n");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
