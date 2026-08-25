# TARMAC em C (pré-release alfa) — Compilador

## Visão geral

Tarmac é um projeto pessoal de compilador de baixo nível: a proposta é compilar uma linguagem
própria e observar, passo a passo, como um código-fonte se transforma em *assembly*. Este
repositório é a **releitura em C** do compilador, que nasceu em C++ em
[gimmelovej/tarmac-cpp](https://github.com/gimmelovej/tarmac-cpp) — mesma linguagem-alvo, mesmo
destino (*assembly* x86-64, sintaxe AT&T, System V AMD64 ABI), reescrito sem exceções, sem
contêineres da biblioteca padrão e sem destrutores.

O objetivo não é competir com compiladores de produção, e sim servir como material de estudo. A
troca de linguagem é parte disso: sem `std::vector`, `std::string_view`, `unique_ptr` ou herança,
cada estrutura que o C++ oferecia pronta precisa ser construída e justificada à mão — e é aí que se
enxerga o que elas de fato faziam. As decisões desse caminho estão em
[`docs/architecture.md`](docs/architecture.md); a gramática reconhecida pelo Parser, em
[`docs/parser.md`](docs/parser.md).

> 🎉 **O pipeline fecha.** Lexer, Parser, análise semântica, geração de código e a montagem/link com
> `as` e `ld` estão no lugar, com a runtime em assembly: `./build/tarm example.tm && ./example`
> compila e roda. Já dá para escrever programas de verdade — funções com parâmetros, globais,
> `if`/`else`, `while`, strings, chamadas nativas e alocação.
>
> 🔤 **A palavra-chave `function` saiu.** Uma função passa a se declarar como em C —
> `int soma(int a, int b) { ... }` —, e o que a distingue de uma variável global é o parêntese
> depois do nome. É uma **mudança incompatível**: todo `.tm` escrito antes precisa perder o
> `function`.
>
> ➕ **Atribuições compostas.** `+=`, `-=`, `*=` e `/=` já funcionam, em variável e em elemento de
> array — como açúcar, desfeito no Parser: `a += b` compila exatamente como `a = a + b`, com as
> mesmas regras de tipo e coerção.
>
> ➖ **Menos unário e números negativos.** `-x` já é reconhecido (como açúcar: o literal é dobrado
> no nó, e `-x` vira `0 - x`), e a runtime passou a imprimir negativo — `print(0 - 5)` sai `-5`, e
> não mais o complemento de dois em decimal.
>
> 🚧 **Arrays (novo, em desenvolvimento).** Declarar, inicializar, ler e atribuir já funcionam, com
> índice literal, variável ou calculado (`v[i + 1]`). A **faixa é conferida sempre**: na compilação
> quando o índice é literal, e em tempo de execução nos demais casos — na leitura e na escrita —,
> abortando o programa em vez de ler ou corromper memória vizinha. O que falta para fechar o recurso
> está em [Arrays](#arrays-novo-e-em-desenvolvimento) e no [`TODO.md`](TODO.md).

---

## Estrutura de pastas e módulos

```
tarmac/
├── include/        # Cabeçalhos (.h) — interface pública de cada etapa do pipeline
├── src/            # Implementação (.c) de todo o compilador
├── runtime/        # Rotinas de suporte em assembly (.s), montadas e linkadas ao programa
├── docs/           # Documentação de referência conceitual (arquitetura, parser, runtime)
├── .vscode/        # Configuração compartilhada do editor (ver a nota abaixo)
├── .clang-format   # Estilo oficial de formatação, aplicado pelo formatOnSave e pelo Ctrl+Shift+I
├── CMakeLists.txt  # Build do executável `tarm`
├── TODO.md         # Próximas correções e novidades, em ordem de prioridade
└── README.md       # Este arquivo
```

> **Por que `.vscode/` é versionado.** O diretório alinha as propriedades de C entre as extensões
> do editor — a C/C++ da Microsoft, por padrão, entra em conflito com o projeto e chega a aceitar
> snippets de C++ em arquivos C. O `settings.json` guarda a associação `*.h → C` (sem ela, um
> cabeçalho novo abre como C++) e o `editor.formatOnSave`, que aplica o `.clang-format` da raiz a
> cada salvamento — assim o estilo do código não depende de configuração local de quem clona.
> Trechos de desenho manual (agrupamento de enums, a união da AST) ficam protegidos por
> `// clang-format off/on`.

| Módulo | Responsabilidade |
|---|---|
| `include/types.h` | Vocabulário compartilhado: `TokenKind`, `Token` (fatia do buffer de origem), `TokenList`, `BaseType`/`DataType` e `FrameType`. |
| `include/errors.h`, `src/erros.c` | `Diagnostics`: acumula erros com posição, com limite de impressão. |
| `include/file.h`, `src/file.c` | Lê o arquivo-fonte inteiro para um buffer terminado em `'\0'`. |
| `include/lexer.h`, `src/lexer.c` | Código-fonte (texto) → `TokenList`. |
| `include/parser.h`, `src/parser.c` | `TokenList` → AST, por descida recursiva. Ver [`docs/parser.md`](docs/parser.md). |
| `include/ast.h`, `src/ast.c` | Nós da árvore (união etiquetada por `ExprKind`) e a lista temporária que os monta. |
| `include/semantic.h`, `src/semantic.c` | Valida tipos na AST e insere conversões implícitas (`ExprCast`). |
| `include/symbol_table.h`, `src/symbol_table.c` | Variáveis declaradas: tipo, tamanho, offset de frame ou rótulo `globobj_N`. |
| `include/function_table.h`, `src/function_table.c` | Assinaturas das funções nativas e das declaradas pelo usuário. |
| `include/codegen.h`, `src/codegen.c` | AST validada → *assembly* x86-64 (AT&T, System V AMD64). |
| `include/arena.h`, `src/arena.c` | Alocador de arena: blocos sequenciais para os nós da AST, liberados de uma vez só. |
| `include/driver.h`, `src/driver.c` | Orquestra o pipeline e monta/linka o binário com `as` + `ld`, sem shell. |
| `runtime/*.s` | Implementações das funções nativas, o `_start` e o aborto de execução. Ver [`docs/runtime.md`](docs/runtime.md). |
| `src/main.c` | Ponto de entrada do executável `tarm`. |

---

## A linguagem

A linguagem Tarmac foi desenhada para ser simples e de baixo nível, inspirada na filosofia da
linguagem **C**: poucos recursos, mas com controle direto sobre memória e execução. Toda instrução
termina com `;`, todo código executável vive dentro de uma função, e o ponto de entrada é `main`:

```tarmac
int soma(int a, int b) {
    return a + b;
}

int main() {
    int resultado = soma(3, 4);
    print(resultado);   // imprime: 7
    return 0;
}
```

São seis tipos (`int`, `int64`, `float`, `bool`, `char`, `string`), com coerção implícita a partir
de literais inteiros, `if`/`else`, `while`, funções com parâmetros e um punhado de funções nativas.

| Nativa | Parâmetros | Retorno | Resumo |
|---|---|---|---|
| `print(v)` | `int`, `int64`, `float`, `string`, `bool`, `char` | `void` | Exibe `v` em *stdout*. **Não** insere `\n`. |
| `atoi(s)` | `string` | `int` | Converte texto decimal; para no primeiro byte que não é dígito. |
| `<string>.len()` | — | `int64` | Comprimento em bytes, lido do header do objeto. |
| `mmap_alloc(n)` / `mmap_free(p, n)` | `int` / `int64`,`int` | `int64` / `void` | Regiões independentes via `mmap`. |
| `brk_alloc(n)` / `brk_free(p)` | `int` / `int64` | `int64` / `void` | Heap linear: liberar `p` libera **tudo** acima dele. |
| `emit_note(f, a, ms)` | `int`,`int`,`int` | `void` | Onda quadrada PCM em *stdout*. |

Os detalhes de cada rotina estão em [`docs/runtime.md`](docs/runtime.md), e a gramática produção a
produção em [`docs/parser.md`](docs/parser.md). O [`example.tm`](example.tm) na raiz é um passeio
por tudo que o compilador já leva do `.tm` ao binário.

### Arrays (novo e em desenvolvimento)

Chegaram na `0.2.0-alpha`. O tamanho vem **antes** do nome, colado ao tipo — assim o tipo é lido de
uma vez só, com a forma junto:

```tarmac
int[3] medidas = { 10, 20, 30 };
print(medidas[1]);                  // imprime: 20
medidas[1] = 99;                    // atribuição a elemento

int i = 0;
while i < 3 {
    medidas[i] = medidas[i] * 2;    // índice variável, na leitura e na escrita
    i = i + 1;
}
```

O que **já funciona**: declarar com tamanho fixo, inicializar com `{ ... }`, ler e atribuir um
elemento por índice literal, variável ou calculado, a checagem do tipo de cada elemento, e a **faixa
conferida sempre** — na compilação com índice literal, em tempo de execução nos demais casos.
Cada elemento é acessado na largura do seu tipo, então `int[3]` (12 bytes) e `char[4]` (4 bytes)
convivem sem se sobrepor; com índice variável o endereço sai do modo escalado do x86, sem instrução
de multiplicação.

O que **ainda não**, e vale saber antes de usar:

| Limitação | Efeito prático |
|---|---|
| Inicializador menor que o declarado | `int[3] v = {1}` é aceito, e o resto fica com o que houvesse na stack |
| Sem array como parâmetro, retorno ou global | não reconhecido |

A faixa é conferida nos dois momentos: com índice **literal**, na análise semântica, sem emitir
código nenhum; com índice **calculado**, por uma comparação antes do acesso — na leitura e na
escrita — que desvia para `fatal_error_` e aborta o programa. O detalhamento técnico está em
[`docs/parser.md`](docs/parser.md#arrays-novo-e-em-desenvolvimento), e o que sobra, no
[`TODO.md`](TODO.md).

> Dois recursos do [Tarmac em C++](https://github.com/gimmelovej/tarmac-cpp) não vieram para este
> port: o tipo **`buffer`** (e a nativa `read_buf` que o produzia) e o **ponto flutuante** na
> geração de código — `float` é reconhecido pela linguagem, mas ainda não vira assembly.

---

## Como compilar e executar

**1. Configure e compile o compilador com CMake** (necessário na primeira vez, ou após alterar o
código-fonte do compilador):

```bash
cmake -S . -B build
cmake --build build
```

Sem `-DCMAKE_BUILD_TYPE`, o build sai em **Debug**: `-g3 -O0` mais ASan e UBSan, que abortam no
primeiro erro de memória (ver [`docs/architecture.md`](docs/architecture.md#build-e-sanitizers)).
Para uma build otimizada, `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`.

**2. Crie um arquivo-fonte com a extensão `.tm`:**

```bash
nano arquivo.tm
```

**3. Execute o Tarmac sobre o seu arquivo `.tm`:**

```bash
./build/tarm arquivo.tm
```

Esse comando roda o pipeline inteiro: *lexer*, *parser*, análise semântica, geração do *assembly*
(gravado em `arquivo.s`, que fica no disco para quem quiser ler o código gerado), montagem com `as`
e link com `ld --gc-sections` junto dos objetos da runtime. Os `.o` intermediários são removidos ao
fim. Os erros saem em *stderr* como `erro <linha>:<coluna>: <mensagem>`, seguidos do total.

**4. Rode o binário gerado:**

```bash
./arquivo
```

O executável não depende de libc: quem inicializa o processo é o `_start` de
[`runtime/takeoff.s`](runtime/takeoff.s).

---

## Estado do desenvolvimento

| Camada | Recurso | Status | Observação |
|---|---|---|---|
| Build | CMake, C11 sem extensões GNU, avisos e sanitizers | 🟢 Implementado | Debug como padrão; ASan/UBSan também na linha de link |
| Build | Compilação do executável `tarm` | 🟢 Implementado | compila e linka sem nenhum aviso |
| Diagnóstico | `Diagnostics`: erro com/sem posição, limite de impressão, resumo final | 🟢 Implementado | substitui as exceções por fase do Tarmac em C++ — ver [`docs/architecture.md`](docs/architecture.md#erros-diagnóstico-acumulado-em-vez-de-exceções) |
| Diagnóstico | Falha de sistema separada do erro do usuário (`tarm_system_error`) | 🟢 Implementado | via `perror`, sem entrar na contagem |
| Arquivo | Leitura do fonte inteiro para buffer terminado em `'\0'` | 🟢 Implementado | tamanho por `fseek`/`ftell`; o chamador é dono do buffer |
| Lexer | Identificadores, palavras-chave, pontuação e operadores (incluindo `==`, `>=`, `<=`) | 🟢 Implementado | tokens como fatias do buffer, sem cópia de texto |
| Lexer | Literais numéricos (inteiro e ponto flutuante) | 🟢 Implementado | letra e dígito são despachados separadamente, então um número não entra pelo caminho do identificador |
| Lexer | Literais de string e de caractere | 🟢 Implementado | o token guarda só o conteúdo, sem as aspas; não terminado, escape inválido e char com mais de um caractere viram erro com posição |
| Lexer | Comentário de linha (`//`) | 🟢 Implementado | `skipTrivia` atravessa comentário e espaço em branco no mesmo laço |
| Lexer | Posição (linha/coluna) e recuperação de erro | 🟢 Implementado | linha e coluna saem do par congelado no início do lexema; lexema desconhecido vira `Invalid` e a varredura segue |
| Lexer | Decodificação de escapes | 🟢 Implementado | o Lexer **valida** e guarda a fatia crua; quem decodifica é o Parser (caractere) e a Codegen, que repassa a string ao `as` — ver [`docs/parser.md`](docs/parser.md#literais-e-escapes) |
| Parser | Gramática completa: nível superior, funções, globais, blocos, `if`/`else`, `while`, `return`, precedência de expressão, chamadas e métodos | 🟢 Implementado | ver [`docs/parser.md`](docs/parser.md) para a gramática produção a produção |
| Parser | Declaração de nível superior sem a palavra-chave `function` | 🟢 Implementado | função e global compartilham a produção `parse_scope_declaration`; é o `(` depois do nome que as separa — ver [`docs/parser.md`](docs/parser.md#nível-superior) |
| Parser | Ponto e vírgula dentro de um bloco | 🟡 Frouxo | exigido no nível superior (`expect`), apenas consumido dentro de um bloco (`match`) — `int x = 1 int y = 2` passa |
| Parser | Recuperação de erro (sincronização) | ⚪ Não iniciado | a primeira produção que falha encerra a análise: um erro de sintaxe por rodada |
| Parser | Menos unário (`-x`) | 🟢 Implementado | como açúcar: o literal é dobrado no próprio nó, e `-x` vira `0 - x`; um `ExprUnary` dedicado está previsto |
| Parser | `!=` e operadores lógicos (`&&`, `||`) | ⚪ Não iniciado | não há token para eles no Lexer |
| Parser | Atribuições compostas (`+=`, `-=`, `*=`, `/=`) | 🟢 Implementado | como açúcar: `a += b` é desfeito no Parser em `a = a + b`, então semântica e codegen veem uma atribuição comum — ver [`docs/parser.md`](docs/parser.md#atribuições-compostas-como-açúcar) |
| Array | Declaração com tamanho fixo (`int[3] v`), inicializador `{ ... }`, leitura e atribuição | 🟡 **Novo, em desenvolvimento** | ver [Arrays](#arrays-novo-e-em-desenvolvimento) |
| Array | Índice variável, em leitura e escrita | 🟢 Implementado | endereçamento escalado do x86 (`offset(%rbp, %rcx, escala)`), sem instrução de multiplicação — é o que torna array utilizável dentro de um `while` |
| Array | Acesso na largura do elemento | 🟢 Implementado | `mov_suffix`/`reg_a` na escrita e `mov_load` (com extensão de sinal) na leitura: `int[3]` ocupa 12 bytes, `char[4]` ocupa 4 |
| Array | Espaço reservado no frame | 🟢 Implementado | `count_slots` soma `ceil(size_of * array_len / 8)` slots, e a tabela de símbolos distribui os offsets pela mesma conta |
| Array | Checagem estática de faixa | 🟢 Implementado | com índice literal, fora de `[0, array_len)` é recusado |
| Array | Base da atribuição precisa ser array | 🟢 Implementado | `x[5] = 9` num escalar é recusado na análise semântica |
| Tipos | Tamanho de elemento com fonte única | 🟢 Implementado | `tarm_symbol_table_data_size` é a única tabela; o Parser a alcança por `tarm_datatype_of` |
| Array | Verificação de faixa em tempo de execução | 🟢 Implementado | na leitura **e** na escrita: `cmpq`/`jae` antes do acesso, com desvio para `fatal_error_`. Sendo comparação sem sinal, a mesma instrução cobre o índice negativo |
| Array | Índice como expressão (`v[i + 1]`) | 🟢 Implementado | qualquer expressão que resolva para `int` serve; o valor é avaliado para um registrador e entra no modo escalado |
| Array | Inicializador menor que o declarado | 🟡 Aceito em silêncio | `int[3] v = {1}` deixa o resto com o que houvesse na stack |
| Array | Como parâmetro, retorno ou variável global | ⚪ Não iniciado | — |
| AST | Nós da árvore: união etiquetada por `ExprKind`, com o tipo resolvido em `type` e a posição de origem em cada nó | 🟢 Implementado | nomes e textos são fatias do buffer, como nos tokens; ver [`docs/architecture.md`](docs/architecture.md#a-ast-como-união-etiquetada) |
| Tipos | `BaseType` (categoria) separado de `DataType` (categoria + forma de array) | 🟢 Implementado | evita duplicar cada tipo numa versão "array de"; quem só precisa da categoria lê `type` |
| AST | `ExprList` → arena (`ast_list_push`/`ast_list_commit`) | 🟢 Implementado | vetor temporário no heap, copiado para a arena quando o tamanho final é conhecido |
| AST | Arena (`arena_init`/`arena_alloc`/`arena_free`) | 🟢 Implementado | blocos de 64 KiB, *bump allocator*, liberação única; ver [`docs/architecture.md`](docs/architecture.md#arena-no-lugar-de-unique_ptr) |
| AST | Sinalização de falha em `ast_list_commit` | 🟡 A revisar | devolve NULL tanto para lista vazia quanto para falha da arena, com `*out_count` já preenchido — o chamador não distingue os dois casos |
| AST | Alinhamento entregue por `arena_alloc` | 🟡 A revisar | o tamanho é arredondado para 16, mas o cabeçalho de 24 bytes desloca `data`, então os ponteiros saem alinhados a 8 |
| Semântica | Tipos, coerção implícita, faixa de `char`, condição de `if`/`while`, tipo do `return` | 🟢 Implementado | erro não interrompe a análise: acumula e segue |
| Semântica | Escopo por função | 🟢 Implementado | marca de pilha na `SymbolTable`: locais somem ao sair da função, globais permanecem |
| Semântica | Validação de chamadas | 🟢 Implementado | existência, aridade e o tipo de cada posição, com coerção implícita; passagem prévia registra as assinaturas, então uma função pode chamar outra definida depois |
| Semântica | Inicializador de global | 🟢 Implementado | precisa ser literal constante — vira `.quad` em `.data`, e não há onde executar código antes do programa |
| Codegen | Expressões, `if`/`else`, `while`, `return`, prólogo/epílogo por função, parâmetros nos registradores da ABI | 🟢 Implementado | alinhamento de `%rsp` conferido antes de cada `call` |
| Codegen | Chamadas: nativas pelo rótulo da `FunctionTable`, do usuário com *mangling* `tarm_<nome>` | 🟢 Implementado | `main` é a exceção do *mangling* — é o nome que o `_start` chama |
| Codegen | `print` despachado por tipo | 🟢 Implementado | escolhe `tarm_print_int`/`_str`/`_bool`/`_char` pelo `type` já anotado no argumento |
| Codegen | Literais de string e globais `string` | 🟢 Implementado | objeto com header em `.rodata` (`strobj_N` + `strbytes_N`), no mesmo formato do heap |
| Codegen | Ponto flutuante | ⚪ Não iniciado | recusado com erro explícito: os valores viajam em registradores inteiros, e `float` exige a família `%xmm` |
| Codegen | Mais de 6 argumentos ou parâmetros | ⚪ Não iniciado | recusado com erro explícito; falta a passagem pela stack |
| Runtime | `print_*`, `atoi`, `strlen`, objetos com header, `mmap`/`brk`, `emit_note`, `_start` | 🟢 Implementada | ver [`docs/runtime.md`](docs/runtime.md) |
| Runtime | Impressão de `int64` em toda a faixa | 🟢 Implementado | `_format_uint` divide em 64 bits; antes truncava a partir de 2³² |
| Runtime | Impressão de número negativo | 🟢 Implementado | `_format_int` guarda o sinal, formata o módulo com `_format_uint` e escreve o `-` na frente; `INT64_MIN` inclusive |
| Runtime | Aborto de execução (`fatal_error_`, `runtime/error.s`) | 🟡 Genérico | mensagem única em *stderr* e `exit(1)`, sem causa nem posição; ver [`docs/runtime.md`](docs/runtime.md#aborto-de-execucao) |
| Runtime | `read_buf` e o tipo `buffer` | ⚪ Fora deste port | a rotina continua em `runtime/io.s`, sem quem a chame |
| Driver | Pipeline completo, com barreira de diagnóstico entre etapas | 🟢 Implementado | todos os recursos são declarados antes do primeiro `goto`, então a limpeza nunca vê variável indeterminada |
| Driver | Montagem e link com `as` + `ld`, sem `gcc` e sem shell | 🟢 Implementado | `posix_spawnp` + `waitpid` com código de saída conferido; `.o` temporários removidos mesmo em caso de falha |
| Driver | Limpeza dos intermediários | 🟢 Implementado | os `.o` (do programa e da runtime) saem ao fim; o `.s` fica, para quem quiser ler o código gerado |
| Driver | Código de saída do processo | 🟢 Implementado | `EXIT_SUCCESS`/`EXIT_FAILURE`, para que `tarm x.tm && ./x` funcione |
| Exemplo | `example.tm` | 🟢 Compila e roda | passeio por globais, funções, coerção, método `len()`, `if`/`else`, `while`, `atoi`, alocação, arrays, menos unário e atribuições compostas |

Legenda: 🟢 implementado e verificado · 🟡 em desenvolvimento/parcial · 🔴 bug conhecido · ⚪ não iniciado.

---

## Por que C, e não mais C++

O Tarmac nasceu em C++ e chegou lá a um compilador completo. A troca não foi por insatisfação com
o resultado — foi porque, num projeto cujo propósito é **aprender como um compilador funciona por
dentro**, o C++ estava respondendo perguntas que eu queria ter feito.

Um compilador é, em boa parte, gerência de memória e de tempo de vida: quem é dono do texto do
arquivo, quanto tempo um nó da árvore precisa viver, quando um vetor pode ser realocado sem
invalidar o que aponta para dentro dele. Com `std::vector`, `std::string_view` e `unique_ptr`, essas
respostas vêm prontas — o código funciona sem que se precise formulá-las. Em C não vêm: cada uma
virou uma decisão explícita, escrita e documentada.

Foi assim que apareceram a arena no lugar do `unique_ptr`, o token como fatia do buffer de origem,
o `Diagnostics` acumulado no lugar das exceções por fase. Nenhuma dessas ideias é nova; o que muda é
que agora elas estão **visíveis**, cada uma com o seu porquê registrado em
[`docs/architecture.md`](docs/architecture.md#do-c-para-o-c). Escrever a arena obriga a entender por
que a AST nasce e morre inteira. Escrever o vetor dinâmico obriga a descobrir que `realloc`
devolvendo NULL não libera o bloco antigo.

Há um ganho colateral de propósito: o C combina melhor com o alvo. O Tarmac gera assembly x86-64 e
linka sem libc, com um `_start` próprio — a distância entre o que o compilador escreve e o que ele
é feito passou a ser bem menor.

E há o que se perde, dito sem romantismo: o compilador não avisa mais quando um tipo não bate, não
há destrutor para garantir a liberação, e o `switch` sobre a tag de um nó pode esquecer um caso. O
que substitui isso é disciplina — convenções de posse escritas nos cabeçalhos, `-Wconversion` e
companhia ligados em toda build, e ASan/UBSan no Debug. Está tudo em
[`CONTRIBUTING.md`](CONTRIBUTING.md).

> O compilador em C++ continua vivo e completo em
> [gimmelovej/tarmac-cpp](https://github.com/gimmelovej/tarmac-cpp), e segue sendo a referência da
> linguagem enquanto este port não alcança a paridade. Os dois lado a lado são metade da graça.

### O que muda, em resumo

| No Tarmac em C++ | No Tarmac em C |
|---|---|
| Exceção por fase (`LexicalError`, `SyntaxError`, ...) | `Diagnostics` acumulado, passado por ponteiro; cada função devolve `bool` |
| `std::string_view` no lexema | `Token` guarda ponteiro + comprimento no buffer de origem |
| `std::vector<Token>` | `TokenList` (`data`/`count`/`capacity`) com crescimento por dobra |
| `unique_ptr<Expr>` na AST | Arena: alocação sequencial, liberação única no fim |
| Hierarquia de classes `Expr` + `static_cast` | Struct única com a tag `ExprKind` e uma `union` por construção |
| `SymbolTable` por escopo (um *frame* por função) | Tabela única, com escopo por marca de pilha |
| `std::system` para chamar `as`/`ld` | `posix_spawnp` + `waitpid`, sem shell, com código de saída conferido |
| `namespace tarm` / `private:` | Prefixo `tarm_` no que é público, `static` no que é interno |
| Tipo `buffer` e a nativa `read_buf` | Fora deste port; `string` continua como objeto com header |

---

## Contribuindo

Padrão de documentação (Doxygen em C), como compilar e verificar localmente e estilo de commit
estão em [`CONTRIBUTING.md`](CONTRIBUTING.md).

## O que vem a seguir

As próximas correções de patch e as novidades planejadas, em ordem de prioridade e com o arquivo de
cada uma, ficam em [`TODO.md`](TODO.md) — começando pelo que falta para fechar o suporte a array.

## Histórico de mudanças

O registro de "o que mudou" a cada versão — o antes e o depois — fica em
[`CHANGELOG.md`](CHANGELOG.md). Diferente de `README`, `LICENSE` ou `CONTRIBUTING`, o GitHub não
reconhece o changelog como um *community health file*, então este link é a forma de chegar até ele.

## Licença

Distribuído sob a licença [MIT](LICENSE).

---

## Sobre o projeto

A ideia por trás do Tarmac é dar uma mãozinha a quem quer aprender a programar de verdade e
explorar o funcionamento interno do mundo computacional. Em um cenário cada vez mais dominado por
IA, é fácil perder o hábito do aprendizado autônomo e genuíno, o que é uma pena, porque a área de
computação é bonita demais para ser só consumida de forma passiva.

De qualquer forma, espero que esse projeto ajude alguém, de alguma forma.

> *So much depends upon a red wheelbarrow*
