// ================================================================================================
// File: driver.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Orquestrador de ponta a ponta do pipeline do Tarmac.
/// @see docs/architecture.md#pipeline

#ifndef TARM_DRIVER_H
#define TARM_DRIVER_H

#include <stdbool.h>
#include <stddef.h>

/// @brief Executa a compilação inteira: lê o arquivo-fonte e o conduz pelas etapas do pipeline.
/// @param argc Contagem de argumentos recebida em `main`.
/// @param argv Argumentos recebidos em `main`; `argv[1]` é o arquivo `.tm` a compilar.
/// @return `true` se a compilação chegou ao fim sem erros.
/// @details É aqui que vivem o `Diagnostics` do processo e a posse dos recursos de uma compilação
/// (buffer do arquivo, lista de tokens, arena da AST, tabelas de símbolos e de funções): cada etapa
/// recebe o que precisa por parâmetro e o Driver libera tudo num ponto único de saída.
/// @details O pipeline completo passa por leitura → léxico → sintaxe → semântica → geração de
/// código, e termina montando o `.s` com `as` e linkando com `ld` os objetos da runtime — sem
/// `gcc` e sem shell, como no Tarmac em C++.
/// @note Entre uma etapa e a seguinte há sempre uma barreira sobre o `Diagnostics`: sem exceção
/// para desviar o fluxo, é o Driver que decide onde a compilação para.
/// @note O que cada etapa já cobre — e o que ainda falta — está na tabela de estado do README.
bool tarm_drive(int argc, char* argv[]);

#endif
