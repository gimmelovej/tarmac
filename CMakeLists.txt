# ================================================================================================
# File: CMakeLists.txt
# Build do compilador Tarmac (executável `tarm`). Ver README.md para o pipeline de compilação e
# a convenção de runtime em assembly.
#
# VERSÃO MÍNIMA — cobre apenas o necessário para compilar e depurar durante o desenvolvimento.
# Não trata de instalação, testes, empacotamento ou suporte a MSVC. Ver notas no fim do arquivo.
# ================================================================================================
cmake_minimum_required(VERSION 3.16)
project(tarmac LANGUAGES C)

# ------------------------------------------------------------------------------------------------
# Padrão da linguagem
# ------------------------------------------------------------------------------------------------
# STANDARD_REQUIRED impede que o CMake caia silenciosamente num padrão anterior se o compilador
# não suportar C11. EXTENSIONS OFF força -std=c11 em vez de -std=gnu11: garante que qualquer uso
# acidental de extensão GNU (typeof, statement expressions, aritmética em void*) vire erro aqui,
# e não meses depois em outra máquina.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ------------------------------------------------------------------------------------------------
# Tipo de build
# ------------------------------------------------------------------------------------------------
# Sem isto, `cmake ..` sem argumentos deixa CMAKE_BUILD_TYPE vazio — que não significa "Debug",
# significa "nenhum": as flags de _DEBUG abaixo seriam ignoradas por completo e o build sairia sem
# -g e sem sanitizers, silenciosamente. Debug é o default certo para o dia a dia deste projeto.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Tipo de build" FORCE)
endif()

# ------------------------------------------------------------------------------------------------
# Flags por tipo de build
# ------------------------------------------------------------------------------------------------
# -g3  : inclui informação de macros, permitindo ao GDB expandi-las na sessão. Relevante porque as
#        estruturas genéricas (arrays dinâmicos, hash tables) serão construídas com macros.
# -O0  : sem inlining nem reordenação — o passo a passo do depurador corresponde ao código escrito.
# -fno-omit-frame-pointer : stack traces confiáveis.
# -fsanitize=address,undefined : em C, esta é a camada que substitui boa parte das checagens que o
#        compilador C++ fazia sozinho. ASan pega out-of-bounds, use-after-free e vazamentos; UBSan
#        pega overflow com sinal, shift inválido, ponteiro desalinhado.
# -fno-sanitize-recover=all : aborta no primeiro erro em vez de imprimir e seguir — facilita
#        capturar o ponto exato no depurador.
set(CMAKE_C_FLAGS_DEBUG
    "-g3 -O0 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all")

# Os sanitizers precisam aparecer TAMBÉM na linha de link, senão o binário sai sem instrumentação
# (ou falha ao linkar). É o erro mais comum ao ativá-los.
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-fsanitize=address,undefined")

set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# Gera compile_commands.json, consumido pelo clangd para autocompletar e diagnóstico no editor.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------------------------------------
# Alvo
# ------------------------------------------------------------------------------------------------
# Fontes do próprio compilador (lexer -> parser -> sema -> codegen -> driver), sem incluir a runtime
# em assembly (runtime/*.s), que não é compilada aqui: ela é entregue ao `as`/`ld` em tempo de
# execução do compilador. Ainda não portada — `runtime/` está vazio (ver README.md).
add_executable(tarm
    src/main.c
    src/file.c
    src/driver.c
    src/erros.c
    src/lexer.c
    src/arena.c
    src/parser.c
    src/semantic.c
    src/ast.c
    src/codegen.c
    src/symbol_table.c
    src/function_table.c
)

# Avisos valem em TODA build, não só em Debug — são a rede de segurança que compensa o que o
# sistema de tipos do C não pega.
# -Wconversion / -Wsign-conversion : conversões implícitas que truncam ou trocam sinal. Um
#     compilador manipula offsets, tamanhos e índices o tempo todo; int virando size_t sem aviso
#     é fonte clássica de bug difícil.
# -Wshadow          : variável interna escondendo outra de escopo externo.
# -Wstrict-prototypes : proíbe `void f()`, que em C significa "argumentos não especificados" e não
#     "sem argumentos" — armadilha herdada do C antigo que não existe em C++.
target_compile_options(tarm PRIVATE
    -Wall -Wextra -Wpedantic
    -Wconversion -Wsign-conversion
    -Wshadow -Wstrict-prototypes
)

# Informa ao driver, em tempo de compilação, onde encontrar os arquivos .s da runtime — evita
# depender do diretório de trabalho em que `tarm` é executado.
# DÍVIDA: grava caminho absoluto no binário. Funciona para desenvolvimento, mas quebra se o
# repositório for movido ou se `tarm` for instalado em /usr/local/bin. Revisar ao tratar instalação.
target_compile_definitions(tarm PRIVATE
    TARM_RUNTIME_DIR="${CMAKE_SOURCE_DIR}/runtime"
)

target_include_directories(tarm PRIVATE include)