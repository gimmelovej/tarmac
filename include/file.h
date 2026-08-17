// ================================================================================================
// File: file.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Leitura do arquivo-fonte para memória, em uma tacada só.
/// @details O Lexer trabalha sobre um buffer contíguo e terminado em `'\0'`, e os tokens guardam
/// fatias dele — por isso o arquivo é lido inteiro de uma vez, e não em pedaços.
/// @see docs/architecture.md#propriedade-da-memória-quem-aloca-quem-libera

#ifndef TARM_FILE_H
#define TARM_FILE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "errors.h"

/// @brief Estado do leitor de arquivos.
/// @details Hoje carrega apenas o `Diagnostics` para onde os erros de E/S são reportados; existe
/// como struct para que a interface não precise mudar quando surgirem caminho de busca, cache de
/// arquivos já lidos ou leitura de módulos incluídos.
typedef struct {
    Diagnostics *diag;
} File;


/// @brief Prepara o leitor, ligando-o ao diagnóstico do compilador.
void tarm_file_init(File *fl, Diagnostics *diag);

/// @brief Lê o arquivo inteiro para um buffer alocado com malloc, terminado em '\0'.
/// @param fl Leitor já inicializado por `tarm_file_init`.
/// @param filename Caminho do arquivo a ler.
/// @param out_length Recebe o número de bytes lidos. Pode ser NULL.
/// @return Buffer com o conteúdo, ou NULL em caso de erro (mensagem já impressa em stderr).
/// @note O chamador é dono do buffer e deve liberá-lo com free().
/// @warning O buffer precisa sobreviver ao Lexer **e** ao Parser: os tokens apontam para dentro
/// dele (ver `Token` em types.h). Liberá-lo antes deixa todo token com ponteiro pendurado.
char *tarm_read_entire_file(File *fl, const char *filename, size_t *out_length);


#endif
