# Arquitetura do Tarmac (port em C)

Visão conceitual do pipeline de compilação e das decisões de design que não cabem — ou não vale a
pena repetir — em cada comentário de código. Os cabeçalhos Doxygen em `include/*.h` referenciam
seções específicas deste arquivo via `@see docs/architecture.md#<secao>`.

Este documento descreve o **estado atual** do port em C. Dois assuntos têm documento próprio: a
gramática reconhecida pelo Parser, em [`docs/parser.md`](parser.md), e as rotinas de suporte em
assembly, em [`docs/runtime.md`](runtime.md). O "quando" e o "por quê" de cada mudança ficam no
[`CHANGELOG.md`](../CHANGELOG.md); o que de fato funciona de ponta a ponta está na tabela de
[Estado do desenvolvimento](../README.md#estado-do-desenvolvimento).

## Pipeline

O destino é o mesmo do Tarmac em C++: código-fonte `.tm` até um binário ELF x86-64. Todas as etapas
existem em C, e o Driver já as encadeia de ponta a ponta:

```
código-fonte (.tm)
   │
   ▼  tarm_read_entire_file()      (file.c)
char *buffer                        + tamanho em bytes
   │
   ▼  tarm_lexer_tokenize()        (lexer.c)
TokenList                           (fatias do buffer acima)
   │
   ▼  tarm_parser_program()        (parser.c)
Expr **  + contagem                 (árvore na arena)
   │
   ▼  tarm_semantic_analyse()      (semantic.c)
mesma árvore, mutada                (coerções implícitas inseridas como ExprCast)
   │
   ▼  tarm_codegen_generate()      (codegen.c)
assembly x86-64 (AT&T), num FILE *
   │
   ▼  as + ld --gc-sections        (driver.c, com runtime/*.s)
binário ELF                         (entrada em `_start`, de runtime/takeoff.s)
```

Cada etapa depende da anterior apenas pelo dado que ela produz (`char *`, `TokenList`, `Expr **`,
`FILE *`) e pelos recursos que atravessam o pipeline inteiro: o `Diagnostics`, onde os erros se
acumulam, a `Arena`, onde a árvore é alocada, e as duas tabelas (símbolos e funções). Não há estado
global: quem amarra as etapas é `tarm_drive` (`driver.c`), que também é o dono desses recursos — ver
[Propriedade da memória](#propriedade-da-memória-quem-aloca-quem-libera).

**Barreiras entre etapas.** Depois de cada uma, o Driver confere o `Diagnostics` antes de seguir:
mesmo que a varredura chegue ao fim do arquivo, uma sequência com tokens `Invalid` não vira árvore;
uma árvore com erro semântico não vira assembly. É o preço de acumular em vez de lançar — sem
exceção que desvie o fluxo sozinha, é o chamador que decide onde a compilação para. Em compensação,
a Codegen pode assumir entrada válida: qualquer inconsistência que chegue lá é bug do compilador,
não erro do usuário.

## Duas tabelas, e por que são separadas

A análise semântica e a geração de código consultam dois catálogos distintos:

- **`SymbolTable`** (`symbol_table.h`) — as **variáveis**: tipo, tamanho, e onde o valor mora
  (offset relativo a `%rbp` para local, rótulo `globobj_N` em `.data` para global).
- **`FunctionTable`** (`function_table.h`) — as **funções e métodos**: tipo de retorno, tipos dos
  parâmetros, se é nativa, variádica ou método (e, nesse caso, o tipo do receptor).

Separá-las não é organização: variáveis e funções vivem em espaços de nome distintos (um programa
pode ter uma variável e uma função de mesmo nome), e o offset de stack que a tabela de símbolos
carrega não significa nada para uma função.

Ambas são **vetores com busca linear**. Para a dezena de nomes de uma função isso é mais rápido que
uma tabela hash e não custa código nenhum de manutenção; se um dia o perfil acusar, o índice entra
sem mexer na API.

**Escopo por marca de pilha.** No Tarmac em C++ cada função ganhava uma `SymbolTable` própria (um
*frame*). Aqui a tabela é única e o escopo é uma marca: `tarm_symbol_table_scope_begin` guarda o
topo do vetor e zera o offset de frame; `..._scope_end` descarta tudo que foi declarado depois.
Como os globais são registrados antes da primeira função, ficam abaixo de qualquer marca e
continuam visíveis — e dois `int i` em funções diferentes convivem sem colidir.

A `FunctionTable` é preenchida em **duas frentes**: as nativas, antes de a árvore ser percorrida, e
as do usuário, numa passagem prévia que só lê as assinaturas. É essa passagem que permite uma função
chamar outra definida depois dela no arquivo — sem ela, a ordem em que as funções aparecem no `.tm`
viraria regra da linguagem.

A Codegen mantém a **própria** `SymbolTable`, em vez de herdar a da semântica: a análise abre e
fecha os escopos dela por conta própria e não deixa nada para trás, então os offsets de frame são
atribuídos de novo aqui, função a função, junto com os rótulos `globobj_N` dos dados estáticos.

## Do C++ para o C

O Tarmac em C++ apoiava boa parte da sua estrutura em recursos da linguagem: exceções para os
erros, `std::string_view` para as fatias de texto, `std::vector` para as sequências, `unique_ptr`
para o tempo de vida da AST e herança para os nós da árvore. Nenhum deles existe em C, e
substituí-los é a maior parte do trabalho deste port. As seções abaixo registram o que entrou no
lugar de cada um.

### Erros: diagnóstico acumulado em vez de exceções

No C++, cada fase lançava uma exceção própria (`LexicalError`, `SyntaxError`, `SemanticError`, ...)
e a compilação parava no primeiro problema. Aqui, todas as etapas compartilham um `Diagnostics`
(`errors.h`), criado pelo Driver e passado adiante por ponteiro: um erro é **registrado** e o
controle volta a quem chamou, que decide se continua ou aborta.

O ganho é relatar vários erros de uma varredura só — o Lexer, por exemplo, transforma um caractere
desconhecido em token `Invalid`, anota o erro e segue. Em troca, cada função passa a devolver um
`bool` de sucesso que o chamador **precisa** conferir: não há exceção para desviar o fluxo por
conta própria.

Duas categorias continuam separadas, como as exceções separavam:

- **Erro do usuário** (`tarm_error_at`, `tarm_error`) — culpa do programa `.tm` ou da linha de
  comando. Acumula em `error_count` e permite continuar. Com posição, sai como
  `erro <linha>:<coluna>: <mensagem>`.
- **Falha do sistema** (`tarm_system_error`) — `malloc`/`fopen`/`realloc` que não deram certo.
  Reportada via `perror` e **não** contabilizada: não é um erro do programa compilado, e quem
  chamou deve abortar a etapa.

Um limite (`max_errors`, hoje 20) corta a impressão em cascata: a partir dele a contagem continua
subindo — o resumo final segue verdadeiro — mas a saída para, com um aviso emitido uma única vez.

### Tokens como fatias do buffer de origem

Um `Token` (`types.h`) não guarda texto: guarda `start` (ponteiro para dentro do buffer do arquivo)
e `len`. É o equivalente do `std::string_view` que o Lexer em C++ usava, e a razão é a mesma — a
varredura de um arquivo inteiro não faz uma única alocação por lexema.

A consequência atravessa o projeto: o buffer devolvido por `tarm_read_entire_file` **precisa viver
mais que os tokens**, e mais que a AST, se os nós guardarem fatias. Liberá-lo cedo não quebra nada
visivelmente — deixa todo token com um ponteiro pendurado. Por isso ele é liberado só no ponto de
limpeza de `tarm_drive`, depois de todas as etapas.

Duas consequências práticas de o lexema não terminar em `'\0'`:

- comparar exige o comprimento (`slice_eq`, em `lexer.c`, e não `strcmp`);
- imprimir exige `%.*s` com `len`, e não `%s`.

### Vetores dinâmicos no lugar de `std::vector`

`TokenList` (`types.h`) é o padrão de vetor dinâmico do projeto: `data`/`count`/`capacity`,
inicializado com `{0}` e crescido por dobra de capacidade em `tokens_push` (`lexer.c`). Dobrar
mantém o custo total de realocação linear no número de tokens, que só é conhecido no fim da
varredura.

O cuidado que o `std::vector` dispensava: `realloc` devolvendo NULL **não** libera o bloco antigo,
então a falha é tratada sem sobrescrever `data` — a lista continua consistente para quem for
liberá-la. E a lista é publicada em `*out` só no sucesso: se a alocação falhar no meio, o que já
havia é liberado ali mesmo e o chamador não recebe uma sequência pela metade.

### Arena no lugar de `unique_ptr`

A AST do Tarmac em C++ vivia em `unique_ptr<Expr>`: cada nó se destruía sozinho, em cascata. Em C,
a alternativa direta seria um `free` por nó — muito código de limpeza para uma estrutura que nasce
e morre **inteira**, uma vez por compilação.

A arena (`arena.h`/`arena.c`) resolve isso: `arena_alloc` entrega pedaços sequenciais, não existe
liberação individual, e um `arena_free` no fim devolve tudo de uma vez. Além de eliminar o risco de
vazamento nó a nó, deixa os nós próximos na memória, o que favorece as travessias repetidas que a
análise semântica e a geração de código fazem.

A implementação é um *bump allocator* sobre uma lista de blocos de 64 KiB. Cada alocação empurra o
cursor `used` do bloco da frente; quando não cabe mais, um bloco novo entra na **frente** da lista,
de modo que a alocação seguinte olhe só `head`. O espaço que sobrou nos blocos antigos é abandonado
de propósito — procurar buraco custaria mais do que o desperdício que evita. Um pedido maior que
64 KiB ganha um bloco do tamanho exato, então nenhum tamanho é grande demais para a arena.

Duas consequências que a interface não mostra sozinha: a memória **não vem zerada**, e `arena_init`
não aloca nada — o primeiro bloco só nasce na primeira alocação. Isso torna barata uma arena que
nunca é usada, mas faz `arena_free` depender de `arena_init` ter acontecido: sem ela, `head` é lixo
da pilha e a liberação percorre uma lista inexistente.

### A AST como união etiquetada

No C++, um nó da AST era uma classe: `Expr` como base e uma subclasse por construção, com o campo
`kind` (`ExprKind`) servindo de tag para o despacho por `switch` + `static_cast`. Em C não há
herança, então o que sobra da ideia é justamente a parte que já era explícita: **a tag**.

Um `Expr` (`ast.h`) é uma struct única com `kind`, a posição de origem e uma `union` com uma
variante por construção. Quem percorre a árvore faz `switch (e->kind)` e lê o membro correspondente
— o mesmo despacho de antes, sem o `static_cast`. O preço é que a `union` não sabe qual campo foi
escrito: ler a variante errada é comportamento indefinido, e a disciplina de manter tag e leitura
alinhadas passa a ser do programador.

Duas coisas mudaram para melhor no caminho:

- **Todo nó carrega `line`/`col`.** Na versão em C++, a AST não guardava origem, e só o Lexer e o
  Parser conseguiam citar linha/coluna num erro; da análise semântica em diante a posição se perdia.
  Aqui o nó nasce com a posição do token que abriu a construção.
- **Nomes são fatias, não cópias.** `name` + `name_len` apontam para o buffer de código-fonte, como
  nos tokens — nenhuma `std::string` por identificador. Em troca, o buffer precisa viver enquanto a
  árvore viver.

**Duas memórias durante a construção.** Uma produção não sabe quantos filhos vai reconhecer, e a
arena não permite realocar o que já entregou — ela só empurra o cursor para a frente. Por isso a
lista de filhos cresce numa `ExprList`, vetor comum de heap, e só no fim é copiada para a arena por
`ast_list_commit`, que já devolve o temporário ao sistema. É o mesmo motivo pelo qual o `Buffer` do
arquivo e a `TokenList` também vivem fora da arena: só o que tem tamanho final conhecido entra nela.

**Categoria e forma também são campos distintos.** O tipo de um valor mora em `DataType`
(`types.h`), que envolve um `BaseType` — a categoria (`Int`, `String`, ...) — e acrescenta a forma:
`is_array`, `array_len` e o `size_of` de **um** elemento. Um array não ganha categoria própria:
`int[3]` guarda `Int` em `type`, e o que o distingue de um `int` é o `is_array`.

Separar as duas dimensões evita duplicar cada tipo da linguagem numa versão "array de", e mantém
indexado por uma dimensão só tudo que se importa apenas com a categoria — as máscaras de tipo
aceito da `FunctionTable`, o despacho de `print` por tipo de argumento, as mensagens de erro. Quem
precisa da forma lê os campos ao lado.

**Tag e tipo são campos distintos.** `kind` diz o que o nó **é** (a construção) e `type` diz o que
ele **vale** (o `DataType` resolvido pela análise semântica). Os dois convivem porque respondem a
perguntas diferentes: a Codegen despacha pelo primeiro e escolhe a rotina de runtime pelo segundo —
é assim que `print` sabe chamar `tarm_print_str` num caso e `tarm_print_int` no outro. Guardar um
no lugar do outro apaga a construção e deixa a árvore ilegível.

## Propriedade da memória (quem aloca, quem libera)

Sem destrutores, a regra de posse precisa estar escrita. A convenção do projeto é que **o Driver é
o dono dos recursos de uma compilação**, e as etapas apenas usam o que recebem:

| Recurso | Aloca | Libera | Quem só usa |
|---|---|---|---|
| Buffer do arquivo-fonte | `tarm_read_entire_file` (`malloc`) | `tarm_drive`, no `cleanup` | Lexer (`Lexer::src`), tokens (`Token::start`) |
| `TokenList` | `tokens_push` (`realloc`) | `tarm_lexer_tokens_free` | Parser (guarda o descritor, não o vetor) |
| Nós e vetores da AST | `ast_expr_new`/`ast_list_commit`, via `arena_alloc` | `arena_free` | Análise semântica, geração de código |
| `ExprList` (temporária) | `ast_list_push` (`realloc`) | `ast_list_free`, sempre chamada por `ast_list_commit` | só a produção que a abriu |
| `SymbolTable`, `FunctionTable` | `entry_push` (`realloc`) | `tarm_*_table_free` | Análise semântica, geração de código |
| Caminhos de saída (`.s`, `.o`, executável) | `replace_extension`/`malloc` | `free`, no `cleanup` | montagem e link |
| `.o` temporários da runtime | `mkstemps` | `temp_objects_cleanup` (`unlink` + `free`) | o `ld` |
| `Arena`, `Diagnostics` | pilha, em `tarm_drive` | — | Todas as etapas, por ponteiro |

Daí a forma de `tarm_drive`: um rótulo `cleanup` único no fim, em vez de liberar em cada retorno
intermediário. Todo caminho de erro passa pelo mesmo ponto, e uma etapa nova entra sem multiplicar
os pontos de liberação.

## Convenções de nomenclatura e visibilidade

O que os *namespaces* e o `private:` do C++ resolviam sozinho aqui é convenção:

- **`tarm_` prefixa tudo que é público** (`tarm_lexer_tokenize`, `tarm_error_at`) — o equivalente
  do `namespace tarm`, num projeto que linka sem libc e não quer colidir com nada.
- **`static` marca o que é interno** ao arquivo `.c` (`peek`, `consume`, `make_token`, `report`).
  Nomes curtos e não prefixados são um sinal de que a função não faz parte da interface.
- **`tarm_<modulo>_init` é o construtor** de cada struct de estado (`Lexer`, `Parser`, `File`): a
  struct é declarada pelo chamador, normalmente na pilha, e inicializada por essa chamada.
- **Struct opaca para esconder layout**: `ArenaBlock` é declarada em `arena.h` e definida só em
  `arena.c` — quem inclui o cabeçalho não consegue depender do formato interno.

## Build e sanitizers

O build (`CMakeLists.txt`) fixa **C11 sem extensões GNU** (`CMAKE_C_EXTENSIONS OFF`), para que um
uso acidental de extensão apareça agora e não em outra máquina, e assume **Debug** quando nenhum
tipo de build é escolhido — sem isso, `cmake ..` sem argumentos sairia sem `-g` e sem sanitizers,
em silêncio.

Em Debug entram ASan e UBSan, com `-fno-sanitize-recover=all`. Essa é a camada que, em C,
substitui boa parte do que o sistema de tipos e os contêineres do C++ garantiam sozinhos:
*out-of-bounds* num vetor dinâmico, *use-after-free* de um buffer que os tokens ainda apontam,
*overflow* com sinal numa conta de offset. Os sanitizers precisam aparecer também na linha de
link, senão o binário sai sem instrumentação.

Os avisos (`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes`)
valem em toda build, não só em Debug. `-Wconversion`/`-Wsign-conversion` importam mais aqui do que
na maioria dos projetos: um compilador manipula offsets, tamanhos e índices o tempo todo, e um
`int` virando `size_t` sem aviso é fonte clássica de bug difícil.
