# Runtime — Rotinas de suporte em assembly

Referência conceitual para os arquivos de [`runtime/`](../runtime). Os cabeçalhos dentro de cada
`.s` descrevem a **interface** de cada rotina (`In`/`Out`/`Clobbers` e a *syscall* usada) e apontam
para as seções abaixo via `Reference: docs/runtime.md#<secao>`; este documento guarda o raciocínio
que não cabe num cabeçalho de rotina.

A runtime é o único código do projeto escrito à mão em assembly. Ela não é compilada pelo CMake:
cada `.s` é montado com `as` em tempo de execução do compilador e linkado ao objeto do programa —
ver [Como a runtime é montada e linkada](#como-a-runtime-é-montada-e-linkada).

Todas as rotinas seguem a **System V AMD64 ABI**: argumentos inteiros/ponteiro em `%rdi`, `%rsi`,
`%rdx`, `%rcx`, `%r8`, `%r9`; ponto flutuante em `%xmm0`+; retorno em `%rax` (ou `%xmm0`); `%rbx`,
`%rbp` e `%r12`–`%r15` preservados pela rotina chamada.

---

## Arquivos

```
runtime/
├── takeoff.s   # _start (ponto de entrada; monta argc/argv/envp e chama main)
├── error.s     # fatal_error_ (aborto de execução; mensagem em stderr e exit 1)
├── object.s    # tarm_obj_new, tarm_obj_len, tarm_buf_str (objetos com header)
├── object.inc  # layout do header (OBJ_DATA/OBJ_LEN/OBJ_CAP/OBJ_SIZE), incluído pelos .s que precisam
├── alloc.s     # tarm_mmap_alloc, tarm_brk_alloc, tarm_mmap_free, tarm_brk_free
├── io.s        # tarm_print_int/float/str/bool/char, tarm_read_buf
├── string.s    # tarm_atoi, tarm_strlen
└── audio.s     # tarm_emit_note (onda quadrada PCM em stdout)
```

---

## Objetos com header

`String` e `Buffer` compartilham um formato: um bloco no heap com **header fixo de 24 bytes**
seguido do payload. O ponteiro que circula pela linguagem aponta sempre para o header.

| Offset | Campo | Conteúdo |
|---|---|---|
| 0 | `OBJ_DATA` | ponteiro para o payload (header + 24) |
| 8 | `OBJ_LEN` | bytes atualmente usados |
| 16 | `OBJ_CAP` | capacidade do payload em bytes |
| 24 | `OBJ_SIZE` | início do payload |

É por isso que uma `String` viaja como um **único ponteiro** na convenção de chamada: as rotinas de
I/O leem `OBJ_DATA`/`OBJ_LEN` do header em vez de receber o comprimento à parte, e não dependem de
terminador nulo. Os métodos `.len()` (`tarm_obj_len`) e `.str()` (`tarm_buf_str`, a identidade)
operam direto sobre o header.

Como cada `.s` é montado isoladamente, nenhum arquivo enxerga as constantes do outro: os offsets
vivem em `object.inc` e são trazidos por `.include "object.inc"` onde forem usados.

---

## Tabela de syscalls Linux x86-64 usadas

O número da chamada vai em `%rax`; os argumentos seguem a ordem `%rdi`, `%rsi`, `%rdx`, `%r10`,
`%r8`, `%r9` — note o `%r10` no lugar do `%rcx` da ABI de funções, porque a instrução `syscall`
destrói `%rcx`.

| Nº | Nome | Onde | Para quê |
|---|---|---|---|
| 0 | `read` | `io.s` | ler o conteúdo de um arquivo em `read_buf` |
| 1 | `write` | `io.s`, `audio.s`, `error.s` | toda saída: `print_*`, as amostras de `emit_note` e a mensagem de aborto |
| 2 | `open` | `io.s` | abrir o arquivo de `read_buf` |
| 3 | `close` | `io.s` | fechar o descritor depois da leitura |
| 9 | `mmap` | `alloc.s` | `mmap_alloc`: mapeamento anônimo independente |
| 11 | `munmap` | `alloc.s` | `mmap_free` |
| 12 | `brk` | `alloc.s` | `brk_alloc`/`brk_free`: mover o *program break* |
| 60 | `exit` | `takeoff.s`, `error.s` | encerrar o processo: com o retorno de `main`, ou com 1 num aborto |

Não há libc no binário gerado: tudo que o programa faz com o sistema passa por esta tabela.

---

## Heap `brk` linear

`tarm_brk_alloc` e `tarm_brk_free` gerenciam um heap **linear**: existe um único ponteiro, o
*program break*, e alocar é empurrá-lo para a frente. Não há contabilidade de blocos, lista de
livres nem cabeçalho de alocação.

A consequência atravessa a linguagem: **`brk_free(p)` libera `p` e tudo que foi alocado depois de
`p`**. A liberação é obrigatoriamente LIFO — na prática, o mesmo modelo da arena que o compilador
usa para a AST, e pela mesma razão: um alocador completo custaria mais código do que o projeto
ganha com ele.

```tarmac
int64 a = brk_alloc(64);
int64 b = brk_alloc(64);
brk_free(a);   // libera "a" e também "b"
```

Quando isso não serve, existe o outro par: `mmap_alloc`/`mmap_free` mapeiam regiões **independentes**
via `mmap`/`munmap`, liberadas por ponteiro e tamanho, sem afetar as demais.

`tarm_obj_new` aloca pelo `brk`, então todo objeto (String/Buffer criado em tempo de execução)
segue essa disciplina.

---

<a id="aborto-de-execucao"></a>

## Aborto de execução

`fatal_error_` (`error.s`) escreve uma mensagem em *stderr* e encerra o processo com código 1. **Não
retorna** — quem desvia para lá não precisa preservar registrador nem alinhar a pilha.

O tratamento é **genérico de propósito**, nesta primeira versão: uma mensagem só, sem dizer o que
falhou nem onde. O que se ganha já é o essencial — o programa para em vez de continuar sobre memória
inválida. Uma versão com causa e posição virá depois.

Hoje o único desvio para lá é a **verificação de faixa de array** emitida pela geração de código,
quando o índice é uma variável:

```
cmpq    $3, %rax          # 3 = array_len
jge     fatal_error_
movslq  -48(%rbp, %rax, 4), %rax
```

Com índice **literal**, a faixa continua sendo conferida em tempo de compilação, e nenhum código de
verificação é emitido — o custo em tempo de execução só existe onde o valor não pode ser conhecido
antes.

O símbolo foge do prefixo `tarm_` das demais rotinas da runtime; o sufixo `_` é o que o separa do
espaço de nomes das funções do usuário, que a Codegen prefixa com `tarm_`.

---

<a id="convencoes-especificas-de-io"></a>

## Convenções específicas de I/O

- **Nenhuma rotina de `print` insere `\n` automaticamente.** Quebra de linha é responsabilidade do
  programa, como em C.
- **`tarm_print_str` lê do header** (`OBJ_DATA`/`OBJ_LEN`), então imprime exatamente os bytes do
  objeto — sem depender de terminador nulo e sem varrer a string para achar o tamanho.
- **`tarm_print_int` imprime com sinal.** Quem trata isso é `_format_int`: guarda o sinal, formata o
  módulo com `_format_uint` e, se era negativo, recua um byte e escreve o `'-'` na frente. Escrever
  da direita para a esquerda é o que torna isso barato — o sinal entra por último, sem deslocar
  nada. `INT64_MIN` cai de pé por acidente feliz: `negq` sobre ele devolve ele mesmo, e a leitura
  sem sinal de `_format_uint` dá exatamente o módulo que se quer.
- **`_format_uint` divide em 64 bits** (`divq`), então cobre toda a faixa de `int64`. A divisão em
  32 bits que havia antes truncava qualquer valor a partir de 2³², e um `int64` grande saía como os
  seus 32 bits baixos.
- **`_format_uint_padded4` continua em 32 bits**, de propósito: ela só recebe a parte fracionária de
  um `float` já escalada (0..9999), e `divq` é sensivelmente mais lento que `divl` no x86.
- **`tarm_print_float`** formata a parte fracionária com 4 dígitos fixos (a parte é escalada por
  10000 e formatada com zeros à esquerda por `_format_uint_padded4`).
- **`tarm_read_buf` lê no máximo 32 bytes** para um buffer temporário na stack antes de copiar para
  o objeto — o arquivo é truncado nesse limite.
- **`tarm_atoi` recebe o header do objeto**, não o texto: o ponteiro real vem de `OBJ_DATA`. Para no
  primeiro byte que não é dígito, e devolve 0 se não houver nenhum.
- **`tarm_strlen` recebe o texto direto** (terminado em NUL), e não o header — é a exceção, usada
  onde o objeto ainda não existe.

---

<a id="audio-emit_note"></a>

## Áudio: `emit_note`

`tarm_emit_note(freq, amp, ms)` gera uma **onda quadrada PCM** (16 bits, mono, 44100 Hz) e escreve
as amostras em *stdout*, em blocos. É suporte a áudio bem inicial: não há formato de arquivo, nem
cabeçalho WAV — só amostras cruas, pensadas para serem redirecionadas a um player:

```bash
./programa | aplay -f S16_LE -r 44100 -c 1
```

`freq = 0` gera silêncio, o que dá pausas entre notas. A onda é montada invertendo o sinal da
amostra a cada meio-período, contado em amostras, sem nenhuma aritmética de ponto flutuante.

Como a saída vai para *stdout*, um programa que use `emit_note` não pode também usar `print` para
mensagens — os bytes se misturariam ao fluxo de áudio.

---

## Como a runtime é montada e linkada

O Tarmac **não usa `gcc` nem shell**. Depois de gerar o `.s` do programa, o Driver (`src/driver.c`):

1. monta o programa com `as` (`programa.s` → `programa.o`);
2. monta **cada** `runtime/*.s` em um `.o` temporário próprio, com nome gerado por `mkstemps` no
   diretório temporário do sistema, passando `-I <runtime>` para que o `.include "object.inc"`
   resolva;
3. linka tudo com `ld --gc-sections`, que descarta as seções não referenciadas — assim só as
   rotinas efetivamente usadas entram no binário;
4. remove os `.o` temporários, inclusive quando a montagem falha no meio da lista.

`as` e `ld` são invocados por `posix_spawnp` + `waitpid`, sem shell: cada argumento chega como texto
literal (espaços e aspas num caminho são apenas caracteres) e o código de saída é conferido de fato,
então uma falha de montagem ou de link vira erro registrado, não uma mensagem de sucesso indevida.

**Sem libc, sem crt0**: quem inicializa o processo é o `_start` de `takeoff.s`, que monta
`argc`/`argv`/`envp` a partir da pilha inicial do kernel, alinha `%rsp`, chama `main` e encerra pela
syscall `exit` com o valor devolvido em `%eax`. É por isso que a função `main` do programa é emitida
sem *mangling*: o nome precisa casar com o `call main` de `takeoff.s`.

---

## Como o compilador chega até aqui

A Codegen não conhece os nomes das rotinas: ela pergunta à `FunctionTable`
(`include/function_table.h`), onde cada nativa da linguagem está ligada ao rótulo que a implementa.

| Na linguagem | Rótulo emitido | Observação |
|---|---|---|
| `print(v)` | `tarm_print_int` / `tarm_print_float` / `tarm_print_str` / `tarm_print_bool` / `tarm_print_char` | escolhido pelo **tipo do argumento**, já anotado no nó pela análise semântica |
| `atoi(s)` | `tarm_atoi` | recebe o ponteiro do objeto String |
| `<string>.len()` | `tarm_obj_len` | método: o receptor entra como primeiro argumento |
| `mmap_alloc` / `mmap_free` | `tarm_mmap_alloc` / `tarm_mmap_free` | regiões independentes |
| `brk_alloc` / `brk_free` | `tarm_brk_alloc` / `tarm_brk_free` | heap linear, liberação LIFO |
| `emit_note(f, a, ms)` | `tarm_emit_note` | amostras cruas em stdout |

Um literal de string vira um objeto **estático** em `.rodata` (`strobj_N` com o header, seguido dos
bytes em `strbytes_N`), no mesmo formato que `tarm_obj_new` produziria no heap — é o que deixa
`tarm_print_str` e `tarm_obj_len` operarem sobre literal e valor dinâmico sem distinção.

Ficam de fora, por enquanto:

- **`read_buf` e o tipo `Buffer`**, que existem no Tarmac em C++ e não foram trazidos para este
  port. `tarm_read_buf` continua na runtime, sem quem a chame.
- **`tarm_print_float`**, alcançável pela tabela mas não pela geração de código, que ainda não
  emite valores de ponto flutuante.
- **`tarm_strlen`**, usada internamente pela runtime, sem correspondente na linguagem.

Ver a tabela de [Estado do desenvolvimento](../README.md#estado-do-desenvolvimento) para o
acompanhamento.
