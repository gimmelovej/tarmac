// ================================================================================================
// File: file.c
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
#include "file.h"

void tarm_file_init(File *fl, Diagnostics *diag) {
    fl->diag = diag;
}

// O tamanho é descoberto pela própria posição do arquivo (`fseek` até o fim + `ftell`), em vez de
// `stat`: mantém a leitura em C padrão, sem depender de POSIX. Cada passo pode falhar por conta
// própria, então todos são conferidos — um `ftell` negativo viraria um `malloc` gigante logo
// abaixo.
//
// O `+ 1` no `malloc` é o byte do `'\0'`: o Lexer chega ao fim do buffer por `len`, mas o
// terminador deixa o conteúdo utilizável por qualquer função de string durante a depuração.
char *tarm_read_entire_file(File *fl, const char *filename, size_t *out_length) {

    if (!filename) {
        tarm_error(fl->diag, "Nome do arquivo alvo não fornecido");
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        tarm_error(fl->diag, "Não foi possível abrir o arquivo");
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        tarm_error(fl->diag, "Não foi possível percorrer o arquivo");
        fclose(file);
        return NULL;
    }

    long fSize = ftell(file);
    if (fSize < 0) {
        tarm_error(fl->diag, "Não foi possível determinar a posição do arquivo");
        fclose(file);
        return NULL;
    }

    size_t file_size = (size_t)fSize;

    char *buffer = malloc(file_size + 1);
    if (!buffer) {
        tarm_error(fl->diag, "Não foi possível alocar memória.");
        fclose(file);
        return NULL;
    }

    fseek(file, 0, SEEK_SET);
    // O comprimento devolvido é o que `fread` de fato leu, não o tamanho do arquivo: é ele que
    // termina o buffer e é ele que o Lexer usa como limite.
    size_t bytes_read  = fread(buffer, 1, file_size, file);
    buffer[bytes_read] = '\0';

    fclose(file);

    if (out_length) {
        *out_length = bytes_read;
    }

    return buffer;
}
