// ================================================================================================
// File: errors.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Diagnóstico de erros do compilador: contagem, limite e formatação das mensagens.
/// @details Onde o Tarmac em C++ lançava uma exceção por fase (`LexicalError`, `SyntaxError`, ...),
/// o port em C **acumula**: cada etapa registra o que encontrou num `Diagnostics` e devolve o
/// controle a quem chamou, que decide se continua. Isso permite relatar vários erros de uma
/// varredura só, em vez de parar no primeiro.
/// @see docs/architecture.md#erros-diagnóstico-acumulado-em-vez-de-exceções

#ifndef TARM_ERRORS_H
#define TARM_ERRORS_H

#include <stdbool.h>
#include <stdint.h>

/// @brief Habilita a checagem de `printf` do GCC/Clang nas funções variádicas deste cabeçalho.
/// @details Sem isto, um `%s` recebendo um `int` só apareceria como lixo na saída (ou um
/// *segfault*) em tempo de execução; com a checagem, vira aviso de compilação.
#if defined(__GNUC__)
#define TARM_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define TARM_PRINTF(f, a)
#endif

/// @brief Estado de diagnóstico compartilhado por todas as etapas do pipeline.
/// @details Uma única instância é criada pelo Driver e passada adiante por ponteiro (`diag`) ao
/// Lexer, ao Parser e aos demais módulos — é o que faz um erro registrado no Lexer ser visto pelo
/// Driver no fim da compilação.
typedef struct {
    uint32_t error_count;
    uint32_t max_errors; // 0 = sem limite
} Diagnostics;

/// @brief Zera a contagem e aplica o limite padrão de erros relatados.
void tarm_diag_init(Diagnostics *d);

/// @brief Algum erro foi registrado até agora?
/// @return `true` se `error_count` for maior que zero.
bool tarm_diag_has_errors(const Diagnostics *d);

/// @brief Erro do usuário, com posição no código-fonte. Acumula e permite continuar.
/// @param d Diagnóstico onde o erro é contabilizado.
/// @param line Linha do início do lexema/token que causou o erro (1-based).
/// @param col Coluna do início do lexema/token que causou o erro (1-based).
/// @param fmt Mensagem no formato de `printf`, sem quebra de linha final.
/// @note Emitida em *stderr* como `erro <linha>:<coluna>: <mensagem>`.
void tarm_error_at(Diagnostics *d, uint32_t line, uint32_t col, const char *fmt, ...)
    TARM_PRINTF(4, 5);

/// @brief Erro do usuário sem posição (ex.: argumento de linha de comando).
/// @param fmt Mensagem no formato de `printf`, sem quebra de linha final.
void tarm_error(Diagnostics *d, const char *fmt, ...) TARM_PRINTF(2, 3);

/// @brief Falha do sistema (`malloc`, `fopen`, ...), relatada via `perror`.
/// @param message Prefixo que descreve a operação que falhou.
/// @note **Não acumula** no `Diagnostics`: não é um erro do programa `.tm`, e sim do ambiente —
/// quem chama deve abortar a etapa em vez de seguir tentando.
void tarm_system_error(const char *message);

#endif
