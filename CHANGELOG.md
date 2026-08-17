# Changelog

Todas as mudanças notáveis do Tarmac em C são registradas aqui. O formato é baseado em
[Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/); a numeração recomeça do zero neste
port, e não continua a do compilador em C++ — a `0.1.0-alpha` daqui tem menos recursos que a
`0.6.4-beta` de lá, e emendar as duas séries faria a versão mentir.

Este arquivo guarda o histórico de "o que mudou" (o antes e o depois). A documentação de referência
(`README.md`, `docs/`) descreve apenas o **estado atual** — quando quiser saber quando/por que algo
passou a funcionar de um jeito, é aqui que se olha.

## [0.1.0-alpha] - 2026-08-17

Primeira versão do Tarmac reescrito em C, a partir do compilador que nasceu em C++ em
[gimmelovej/tarmac-cpp](https://github.com/gimmelovej/tarmac-cpp) (à altura da `0.6.4-beta`). O
pipeline fecha de ponta a ponta: `tarm example.tm && ./example` compila e roda. Ver
[Estado do desenvolvimento](README.md#estado-do-desenvolvimento) para o que falta — `float` na
geração de código e o tipo `buffer` são as ausências mais visíveis em relação ao original.

### Adicionado
- Esqueleto do pipeline em C: `Diagnostics` (`errors.h`), leitura do arquivo-fonte (`file.h`),
  Lexer (`lexer.h`), primitivos de cursor do Parser (`parser.h`) e o Driver que amarra as etapas
  (`driver.h`).
- Lexer reconhecendo identificadores, as doze palavras-chave da linguagem, pontuação e os
  operadores de um e dois caracteres (`==`, `>=`, `<=`), com posição de linha/coluna e recuperação
  de erro — um lexema desconhecido vira token `Invalid` e a varredura continua.
- `TokenList`: vetor dinâmico de tokens com crescimento por dobra de capacidade, no lugar do
  `std::vector<Token>`.
- **Parser descendente recursivo** (`parser.c`), com as duas cadeias de produção: instrução (nível
  superior → declaração → instrução) e precedência de expressão (atribuição → igualdade →
  relacional → aditiva → multiplicativa → posfixa → primária). Reconhece declaração de função com
  parâmetros, variável global e local, blocos, `if`/`else`, `while`, `return`, chamadas de função e
  de método, e agrupamento por parênteses. Documentado produção a produção em
  [`docs/parser.md`](docs/parser.md).
- **AST** (`ast.h`/`ast.c`): nó como união etiquetada por `ExprKind`, no lugar da hierarquia de
  classes do Tarmac em C++. Cada nó carrega a posição (`line`/`col`) do token que abriu a
  construção — algo que a AST em C++ não guardava. A `ExprList` monta os filhos num vetor de heap
  temporário e `ast_list_commit` os copia para a arena quando o tamanho final é conhecido.
- **Análise semântica** (`semantic.h`/`semantic.c`): resolve o tipo de cada nó, registra as
  variáveis na tabela de símbolos, insere `ExprCast` nas conversões implícitas permitidas (todas a
  partir de `Int`), confere a faixa de um literal atribuído a `char`, exige `bool` na condição de
  `if`/`while` e compara cada `return` com o tipo declarado da função. Diferente da versão em C++,
  um erro não interrompe a análise: acumula e segue.
- **Geração de código** (`codegen.h`/`codegen.c`): emite assembly x86-64 (AT&T, System V AMD64) com
  prólogo/epílogo por função, parâmetros vindos dos registradores da ABI, variáveis locais em slots
  de 8 bytes, globais em `.data` como `globobj_N`, `if`/`else`, `while` e `return` com rótulos
  próprios, e correção de alinhamento de `%rsp` antes de cada `call`.
- **Tabela de símbolos** (`symbol_table.h`) e **tabela de funções** (`function_table.h`):
  respectivamente as variáveis (tipo, tamanho, offset de frame ou rótulo global) e as assinaturas
  conhecidas (nativas, do usuário e métodos, com o tipo do receptor).
- **Runtime em assembly** (`runtime/*.s`): `_start` próprio, objetos com header de 24 bytes,
  `print_*`, `atoi`, `strlen`, alocação por `mmap` e por `brk` (heap linear) e `emit_note`.
  Documentada em [`docs/runtime.md`](docs/runtime.md).
- **Montagem e link no Driver**: cada `runtime/*.s` vira um `.o` temporário via `mkstemps`, o
  programa é montado com `as` e tudo é linkado com `ld --gc-sections` — sem `gcc`, sem libc e sem
  shell (`posix_spawnp` + `waitpid`, com código de saída conferido). Os temporários são removidos
  mesmo quando a montagem falha no meio.
- `DataType` e `FrameType` em `types.h`, mais os tokens `KwTrue`/`KwFalse` e o nó `ExprCast`.
- Lexer completo: literais de string e de caractere (guardando só o conteúdo, com escapes
  validados), literais de ponto flutuante, comentários de linha e `true`/`false` como palavras-chave.
- Alocador de arena (`arena.h`/`arena.c`), que substitui o `unique_ptr<Expr>` da AST: *bump
  allocator* sobre uma lista de blocos de 64 KiB, sem liberação individual, com um `arena_free`
  único no fim. Um pedido maior que o bloco padrão ganha um bloco do tamanho exato.
- Driver montando a etapa seguinte: uma barreira de diagnóstico entre Lexer e Parser (a análise
  sintática não começa sobre uma sequência que já se sabe errada) e a criação da `Arena` e do
  `Parser` que a recebe.
- Build CMake com C11 sem extensões GNU, Debug como tipo padrão, ASan/UBSan (também na linha de
  link) e o conjunto de avisos que compensa o que o sistema de tipos do C não pega
  (`-Wconversion`, `-Wsign-conversion`, `-Wshadow`, `-Wstrict-prototypes`).
- Documentação de referência: `README.md`, `docs/architecture.md`, `docs/parser.md`,
  `docs/runtime.md`, `CONTRIBUTING.md` e este changelog; cabeçalhos Doxygen em `include/*.h`,
  comentários de projeto em `src/*.c` e cabeçalhos de interface (`In`/`Out`/`Clobbers`) em
  `runtime/*.s`.

### Corrigido
- **Escopo por função na tabela de símbolos.** `reset_frame` só zerava o offset e deixava as
  entradas para trás, então duas funções com um `int x` cada colidiam como redeclaração. No lugar
  dele entram `tarm_symbol_table_scope_begin`/`_scope_end`: uma marca de pilha que descarta os
  locais ao sair da função e preserva os globais, declarados antes de qualquer marca.
- **`FunctionTable` ligada ao pipeline.** Uma passagem prévia registra as assinaturas das funções
  do usuário antes de validar qualquer corpo — o que permite chamar uma função definida depois no
  arquivo —, e `check_call` confere existência, aridade e o tipo de cada posição, com coerção
  implícita quando permitida. Antes, chamada nenhuma era validada.
- **`print` deixou de emitir `printf@PLT`.** O Driver linka sem libc, então um programa que usasse
  `print` não linkava. A chamada passa a ser despachada pelo tipo já anotado no argumento para a
  rotina correspondente de `runtime/io.s` (`tarm_print_int`, `tarm_print_str`, ...), com o rótulo
  vindo da `FunctionTable`.
- **Literais de string chegam à geração de código.** Cada um vira um objeto estático em `.rodata`
  (`strobj_N` com o header de 24 bytes, seguido dos bytes em `strbytes_N`), no mesmo formato que a
  runtime aloca no heap — é o que faz `tarm_print_str` e `len()` operarem sobre literal e valor
  dinâmico sem distinção. Uma `string` global guarda o ponteiro para esse objeto.
- **Símbolos de função com *mangling*** (`soma` → `tarm_soma`), como no Tarmac em C++. `main` é a
  exceção: é o nome que o `_start` de `runtime/takeoff.s` chama.
- **Escapes decodificados.** `'\n'` chegava ao nó como a barra invertida. Um literal de caractere
  passa a ser resolvido no Parser; um de string é repassado cru ao `as`, que já entende as mesmas
  sequências, com o comprimento do header contado depois da decodificação.
- **Inicializador de variável global** precisa ser um literal constante, e o erro é reportado com
  posição. Antes, um inicializador não literal era trocado por zero em silêncio.
- **Faixa estourada não gera erro duplo.** `char c = 999;` relatava a faixa e, em cima, um
  "inicializador espera 'char', recebeu 'int'" genérico.
- **`mkstemps` declarada**: `_DEFAULT_SOURCE` antes dos includes, no lugar da declaração implícita
  que a build acusava. A compilação do `tarm` passa a sair sem nenhum aviso.
- **Código de saída do processo**: `EXIT_SUCCESS`/`EXIT_FAILURE` em vez do `bool` cru, que invertia
  o significado para quem encadeia `tarm x.tm && ./x`.
- O `.o` do programa é removido junto com os da runtime; o `.s` fica.
- `example.tm` voltou a ser um passeio pela linguagem — usava `int main()`, sem a palavra-chave
  `function` que a gramática exige, e não compilava.

### Alterado (em relação ao Tarmac em C++)
- **Erros deixaram de ser exceções.** A hierarquia de `include/errors.hpp` (`LexicalError`,
  `SyntaxError`, `SemanticError`, ...) deu lugar a um `Diagnostics` único, criado pelo Driver e
  passado por ponteiro a cada etapa: o erro é acumulado com posição e o controle volta a quem
  chamou. Uma varredura passa a relatar vários erros de uma vez, ao custo de cada função devolver
  um `bool` de sucesso que o chamador precisa conferir. Um limite (20 erros) corta a impressão em
  cascata sem falsear o resumo final.
- **Lexemas deixaram de ser `std::string_view`.** Um `Token` guarda `start` + `len` apontando para
  dentro do buffer do arquivo — mesma ideia, com a consequência agora explícita: o buffer precisa
  viver mais que os tokens, e comparar exige o comprimento (`slice_eq`) em vez de `strcmp`.
- **A posse da memória virou convenção documentada.** Sem destrutores, o Driver passa a ser o dono
  dos recursos de uma compilação, com um ponto único de limpeza (`cleanup`) no fim de
  `tarm_drive` — ver [`docs/architecture.md`](docs/architecture.md#propriedade-da-memória-quem-aloca-quem-libera).
- **`namespace tarm` e `private:` viraram convenção de nome**: prefixo `tarm_` no que é público,
  `static` no que é interno ao `.c`, struct opaca (`ArenaBlock`) quando o layout não deve vazar.

## Histórico anterior (Tarmac em C++)

O caminho até aqui — da primeira versão do lexer à `0.6.4-beta`, com funções definidas pelo
usuário, variáveis globais, `if`/`else`, `while` e a runtime em *assembly* — está no
[changelog do Tarmac em C++](https://github.com/gimmelovej/tarmac-cpp/blob/master/CHANGELOG.md).
