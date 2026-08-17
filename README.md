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
> `if`/`else`, `while`, strings, chamadas nativas e alocação. Falta o que está marcado como
> pendente em [Estado do desenvolvimento](#estado-do-desenvolvimento), com destaque para `float`,
> que é reconhecido pela linguagem mas ainda não chega ao assembly.

---

## Estrutura de pastas e módulos

```
tarmac/
├── include/        # Cabeçalhos (.h) — interface pública de cada etapa do pipeline
├── src/            # Implementação (.c) de todo o compilador
├── runtime/        # Rotinas de suporte em assembly (.s), montadas e linkadas ao programa
├── docs/           # Documentação de referência conceitual (arquitetura, parser, runtime)
├── CMakeLists.txt  # Build do executável `tarm`
└── README.md       # Este arquivo
```

| Módulo | Responsabilidade |
|---|---|
| `include/types.h` | Vocabulário compartilhado: `TokenKind`, `Token` (fatia do buffer de origem), `TokenList`, `DataType` e `FrameType`. |
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
| `runtime/*.s` | Implementações das funções nativas e o `_start`. Ver [`docs/runtime.md`](docs/runtime.md). |
| `src/main.c` | Ponto de entrada do executável `tarm`. |

---

## A linguagem

A linguagem Tarmac foi desenhada para ser simples e de baixo nível, inspirada na filosofia da
linguagem **C**: poucos recursos, mas com controle direto sobre memória e execução. Toda instrução
termina com `;`, todo código executável vive dentro de uma função, e o ponto de entrada é `main`:

```tarmac
int function soma(int a, int b) {
    return a + b;
}

int function main() {
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
| Parser | Ponto e vírgula dentro de um bloco | 🟡 Frouxo | exigido no nível superior (`expect`), apenas consumido dentro de um bloco (`match`) — `int x = 1 int y = 2` passa |
| Parser | Recuperação de erro (sincronização) | ⚪ Não iniciado | a primeira produção que falha encerra a análise: um erro de sintaxe por rodada |
| Parser | Menos unário, `!=`, `&&`/`||` | ⚪ Não iniciado | `-5` é erro de sintaxe; não há token para os operadores lógicos |
| AST | Nós da árvore: união etiquetada por `ExprKind`, com o tipo resolvido em `type` e a posição de origem em cada nó | 🟢 Implementado | nomes e textos são fatias do buffer, como nos tokens; ver [`docs/architecture.md`](docs/architecture.md#a-ast-como-união-etiquetada) |
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
| Runtime | `read_buf` e o tipo `buffer` | ⚪ Fora deste port | a rotina continua em `runtime/io.s`, sem quem a chame |
| Driver | Pipeline completo, com barreira de diagnóstico entre etapas | 🟢 Implementado | todos os recursos são declarados antes do primeiro `goto`, então a limpeza nunca vê variável indeterminada |
| Driver | Montagem e link com `as` + `ld`, sem `gcc` e sem shell | 🟢 Implementado | `posix_spawnp` + `waitpid` com código de saída conferido; `.o` temporários removidos mesmo em caso de falha |
| Driver | Limpeza dos intermediários | 🟢 Implementado | os `.o` (do programa e da runtime) saem ao fim; o `.s` fica, para quem quiser ler o código gerado |
| Driver | Código de saída do processo | 🟢 Implementado | `EXIT_SUCCESS`/`EXIT_FAILURE`, para que `tarm x.tm && ./x` funcione |
| Exemplo | `example.tm` | 🟢 Compila e roda | passeio por globais, funções, coerção, método `len()`, `if`/`else`, `while`, `atoi` e alocação |

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
