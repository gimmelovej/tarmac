# Changelog

Todas as mudanças notáveis do Tarmac em C são registradas aqui. O formato é baseado em
[Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/); a numeração recomeça do zero neste
port, e não continua a do compilador em C++ — a `0.1.0-alpha` daqui tem menos recursos que a
`0.6.4-beta` de lá, e emendar as duas séries faria a versão mentir.

Este arquivo guarda o histórico de "o que mudou" (o antes e o depois). A documentação de referência
(`README.md`, `docs/`) descreve apenas o **estado atual** — quando quiser saber quando/por que algo
passou a funcionar de um jeito, é aqui que se olha.

## [Não lançado]

**Atribuições compostas** na linguagem: `+=`, `-=`, `*=` e `/=`, em variável e em elemento de
array.

```tarmac
total += soma(2, 3);
v[i] *= 2;              // elemento de array também é alvo válido
x /= 2 + 2;             // o lado direito agrupa inteiro antes: `x = x / 4`
```

### Adicionado
- **Atribuições compostas** (`+=`, `-=`, `*=`, `/=`), como **açúcar sintático**: não há nó próprio.
  `parse_assignment` desfaz `a += b` em `a = a + b` — um `ExprAssign` cujo valor é um `ExprBinary`
  comum — então a análise semântica e a geração de código não conhecem construção nova, e as regras
  de tipo, a coerção implícita e o código emitido são os da forma estendida. As consequências do
  dessugaramento (o alvo aparece duas vezes na árvore e é avaliado duas vezes) estão registradas em
  `docs/parser.md#atribuições-compostas-como-açúcar`.
- Os tokens `PlusEqual`, `MinusEqual`, `StarEqual` e `SlashEqual` no Lexer, resolvidos pelo mesmo
  *lookahead* de um caractere que `==`, `>=` e `<=` já usavam.
- `OpNone` em `BinaryOp`: o sentinela interno com que `parse_assignment` distingue o `=` simples de
  um composto. Nenhuma expressão o produz e ele nunca chega a um nó.

### Documentação
- `docs/parser.md` ganhou a subseção "Atribuições compostas, como açúcar", com a tabela de
  precedência atualizada.
- A pendência defasada "Sem menos unário" saiu de `docs/parser.md` — o recurso entrou na
  `0.3.0-alpha` e a lista não tinha acompanhado.
- `example.tm` cobre os quatro operadores compostos, inclusive sobre elemento de array.

## [0.3.0-alpha] - 2026-08-21

**Menos unário** na linguagem e **impressão com sinal** na runtime — as duas metades do mesmo
recurso: até aqui não havia como escrever um número negativo nem como vê-lo.

```tarmac
int negativo = -7;
print(negativo);        // -7
print(-contagem * 2);   // o unário fica acima da multiplicação
```

### Adicionado
- **Menos unário**, como **açúcar sintático**: não há nó próprio. Num literal, o sinal é dobrado no
  próprio `ExprInteger`; numa expressão, `-x` vira `0 - x` (um `ExprBinary` com `OpSub`). Dobrar o
  literal não é só economia de um nó — sem isso, `-9223372036854775808` teria de passar pelo
  positivo correspondente, que não cabe em `int64_t`.
- A produção `parse_unary` entra entre `parse_multiplicative` e `parse_postfix`, então o unário tem
  precedência **maior** que a multiplicação (`-a * b` agrupa como `(-a) * b`) e é recursiva, o que
  faz `-(-x)` funcionar.
- **`_format_int`** no runtime: guarda o sinal, formata o módulo com `_format_uint` e, se era
  negativo, recua um byte e escreve o `'-'` na frente. Escrever da direita para a esquerda é o que
  torna isso barato — o sinal entra por último, sem deslocar nada. `INT64_MIN` cai de pé por
  acidente feliz: `negq` sobre ele devolve ele mesmo, e a leitura sem sinal dá o módulo certo.

### Corrigido
- **`array_len` vinha do token errado.** A captura do literal de tamanho havia subido para o topo de
  `parse_type`, antes de `match_type_kw`, e passou a ler o token **anterior ao próprio tipo**: um
  `int[3]` precedido de `{` virava um array de 75 elementos (`'{' - '0'`), reservando 336 bytes de
  frame e deixando a checagem de faixa inútil — `v[50]` compilava. O literal volta a ser lido depois
  do `expect` que o consome.

### Documentação
- `docs/parser.md` ganhou a subseção "Menos unário, como açúcar", com a tabela de precedência
  atualizada; `docs/runtime.md`, o funcionamento de `_format_int`.
- O nó dedicado (`ExprUnary`) e as **atribuições compostas** — a próxima implementação — entraram no
  [`TODO.md`](TODO.md).
- `example.tm` cobre o unário, inclusive a interação com a precedência da multiplicação.

## [0.2.2-alpha] - 2026-08-20

**Índice variável** em arrays, na leitura e na escrita — o que torna array utilizável dentro de um
laço, que era o principal buraco do recurso.

```tarmac
int i = 0;
while i < 3 {
    v[i] = v[i] * 2;
    i = i + 1;
}
```

> 🚧 O recurso continua **em desenvolvimento**. A contrapartida do índice variável é que a checagem
> estática de faixa deixou de cobrir todos os casos: **não há verificação em tempo de execução**, e
> um índice fora da faixa lê memória vizinha ou a corrompe. É o item 1 do [`TODO.md`](TODO.md).
>
> Fora dos arrays, fica registrado que **número negativo ainda não imprime**: `_format_uint` trata o
> valor como sem sinal, então `0 - 5` sai como `18446744073709551611`.

### Adicionado
- **Índice variável** (`v[i]`), em leitura e escrita, pelo modo de endereçamento escalado do x86
  (`offset(%rbp, %rcx, escala)`) — a multiplicação sai de graça na própria instrução, e a escala é o
  `size_of` do elemento (o processador aceita 1, 2, 4 e 8, que cobrem todos os tipos da linguagem).
  Na escrita, o valor a gravar é empilhado enquanto o índice é avaliado, porque os dois terminam em
  `%rax`.
- **A base de uma atribuição indexada precisa ser um array**: `x[5] = 9` num escalar passa a ser
  recusado na análise semântica. Sem esse teste, virava uma escrita 20 bytes além do slot.

### Corrigido
- **Uma fonte única para o tamanho de um elemento.** `size_of_base` (parser.c) e
  `tarm_symbol_table_data_size` davam valores diferentes para `Float` e `String`, e como a reserva do
  frame saía de uma e os offsets da outra, o espaço não fechava: **três variáveis `string` numa
  função já bastavam** — a terceira caía 16 bytes fora do frame e o valor se perdia (`xxyyzz` saía
  como `xxyy`). `size_of_base` foi removida e o Parser passa a usar `tarm_datatype_of`, que resolve
  o tamanho pela tabela de símbolos; `String` foi corrigida para 8 (o slot guarda um ponteiro para
  o objeto, não o objeto).
- **`int64` acima de 2³² deixou de truncar na impressão.** `_format_uint` (runtime/io.s) dividia em
  32 bits (`divl`), então qualquer valor a partir de 4294967296 saía como os seus 32 bits baixos —
  `5000000000` virava `705032704`. Passa a dividir em 64 bits (`divq`), com o teste de fim do laço
  também alargado: em 32 bits, um quociente com os bits baixos zerados encerraria o laço cedo.
  `_format_uint_padded4` continua em 32 bits de propósito, porque só recebe a parte fracionária de
  um `float` já escalada (0..9999) e `divq` é bem mais lento.
- O tamanho do elemento na leitura indexada vinha do tipo do **nó** e passa a vir do símbolo, que é
  quem de fato conhece a declaração.

### Documentação
- Seção de arrays reescrita no README e em `docs/parser.md`, com a subseção nova sobre índice
  literal versus variável e o que cada caminho garante.
- `docs/runtime.md` ganhou o porquê da divisão em 64 bits, de `_format_uint_padded4` continuar em
  32, e o registro de que número negativo sai como complemento de dois.
- Saíram da documentação registros de pendências que eram troca de uma palavra, e não decisão de
  projeto; ficaram só as que exigem implementação de verdade.
- Comentários defasados removidos: a nota de includes faltantes em `ast.c` (resolvida há duas
  versões), a de `parse_primary` sobre tokens que o Lexer já emite, e o aviso de `driver.h` de que o
  executável não saía correto.
- `example.tm` passa a cobrir índice variável dentro de um `while`.

## [0.2.1-alpha] - 2026-08-19

Correções do suporte a array que entrou na `0.2.0-alpha`, mais a **atribuição a elemento**. Os dois
bugs que corrompiam memória — sobreposição de elementos e frame subdimensionado — saíram.

> 🚧 O recurso continua **em desenvolvimento**. O caminho de **escrita** ainda não confere o que o
> de leitura confere: `v[i] = 5` com `i` variável compila e escreve num offset arbitrário. Ver
> [Arrays](README.md#arrays-novo-e-em-desenvolvimento) e o [`TODO.md`](TODO.md), onde é o item 1.

### Adicionado
- **Atribuição a elemento de array** (`v[0] = 9`), com a mesma coerção implícita de qualquer
  atribuição. O alvo já era reconhecido pelo Parser desde a `0.2.0-alpha`; agora a análise semântica
  resolve o tipo esperado e a Codegen emite o endereço do slot.

### Corrigido
- **Elementos não se sobrepõem mais.** O inicializador gravava tudo com `movq` (8 bytes) num passo
  de `size_of`, então num `int[3]` cada elemento invadia o seguinte e `{10, 20, 30}` lia de volta
  `10 0 30`. Três helpers passam a escolher a largura do acesso pelo tamanho do elemento:
  `mov_suffix`/`reg_a` na escrita e `mov_load` na leitura — esta com extensão de **sinal**, para que
  um `int` negativo continue negativo em `%rax`.
- **Espaço reservado no frame.** `count_slots` contava um slot por declaração e `declare_local`
  reservava `SLOT_SIZE`; um `int[10]` reservava 8 bytes e escrevia 40, invadindo o resto do frame.
  Agora `count_slots` soma `ceil(size_of * array_len / 8)` slots e `declare_local` deixa o tamanho a
  cargo da tabela de símbolos, que multiplica por `array_len` — as duas contas precisam concordar,
  porque uma dimensiona o `subq` e a outra distribui os offsets dentro dele.
- **Faixa do índice** comparava com `>` no lugar de `>=`: `v[2]` num `int[2]` passava. A mensagem
  também ficou mais direta ("elemento fora de alcance").
- **`-Wpedantic` em `count_slots`**: um rótulo de `case` seguido direto de uma declaração não é
  válido em C11. A build volta a sair sem nenhum aviso.

### Alterado
- `example.tm` passa a usar `int[3]` (era `int64[3]`, escolhido só para contornar a sobreposição) e
  cobre a atribuição a elemento e um `char[4]`.

## [0.2.0-alpha] - 2026-08-17

Suporte inicial a **arrays** — o primeiro recurso da linguagem que nasce neste port, e não veio
herdado do compilador em C++.

> 🚧 O recurso é **novo e está em desenvolvimento**. Já dá para declarar, inicializar e ler, mas
> elementos de menos de 8 bytes se sobrepõem, o índice precisa ser literal e atribuir a um elemento
> não é gerado. As limitações estão listadas em
> [Arrays](README.md#arrays-novo-e-em-desenvolvimento) e no [`TODO.md`](TODO.md), que passa a ser a
> lista viva do que vem a seguir.

### Adicionado
- **Arrays**: `int64[3] v = { 10, 20, 30 };` e `v[0]`. O tamanho vem **antes** do nome, colado ao
  tipo, para que a forma seja lida junto com a categoria numa passagem só (`parse_type`). Cobre a
  declaração com tamanho fixo, o inicializador `{ ... }` — que só vale numa declaração, não é
  expressão de primeira classe — e a leitura por índice literal.
- Nós `ExprIndex` e `ExprArrayLit` na AST, tokens `LBracket`/`RBracket` no Lexer, e a checagem
  semântica da contagem de elementos, do tipo de cada um e da faixa do índice.
- [`TODO.md`](TODO.md): as próximas correções de patch e novidades, em ordem de prioridade, com o
  arquivo de cada uma.

### Alterado
- **`DataType` virou duas coisas**: `BaseType` guarda a categoria (`Int`, `String`, ...) e
  `DataType` a envolve com a forma (`is_array`, `array_len`, `size_of` de um elemento). Isso evita
  duplicar cada tipo da linguagem numa versão "array de", e mantém indexado por uma dimensão só
  tudo que só se importa com a categoria — as máscaras da `FunctionTable`, o despacho de `print`,
  as mensagens de erro. `tarm_datatype_of` monta o `DataType` de um escalar com o `size_of` já
  resolvido.
- **`ExprVarDecl` e `ExprAssign` guardam um nó**, não um par nome/comprimento: `var_decl.obj` e
  `assign.target` são `Expr *`. É o que permite o alvo de uma atribuição ser um `ExprIndex`, e dá
  posição própria a cada um nas mensagens de erro.
- Comparadores renomeados no `enum` de tokens para o que de fato são: `GBrackets`/`LBrackets` →
  `Greater`/`Less`, e os pares com `=` na mesma linha. Os colchetes agora são `LBracket`/`RBracket`.

### Corrigido
- `ExprArrayLit` não era tratado no `switch` da geração de código (`-Wswitch`): um literal de array
  fora de uma declaração passa a virar erro explícito, em vez de cair no fim da função.
- Leitura de `var_decl.obj` **antes** do teste de `kind` na emissão dos globais, o que lia a
  variante errada da união em todo nó que não fosse uma declaração de variável.
- `go_back` removida de `parser.c`: função morta, sem chamador, que a build acusava
  (`-Wunused-function`).

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
