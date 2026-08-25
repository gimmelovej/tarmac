# Parser — Gramática e estado de implementação

Referência conceitual para `include/parser.h` / `src/parser.c`. Os comentários no código apontam
para as seções abaixo via `@see docs/parser.md#<secao>`; este documento existe para não repetir, em
cada função, o raciocínio por trás da gramática.

---

## Visão geral

O Parser é um *parser descendente recursivo* clássico, com duas cadeias de produção:

1. **Cadeia de instrução** — decide o que uma linha do programa *é*.
2. **Cadeia de precedência de expressão** — decide como uma expressão dentro de uma instrução é
   *agrupada*.

```
tarm_parser_program
 └─ parse_top_level
     └─ <tipo> <nome> ...     → parse_scope_declaration
         ├─ seguido de "("    → ExprFuncDecl
         └─ qualquer outra    → ExprVarDecl (frame Global)

parse_declaration
 ├─ <tipo> <nome> [= ...]  → ExprVarDecl (frame Local)
 └─ (default)              → parse_statement

parse_statement
 ├─ "if" ...      → ExprConditional
 ├─ "while" ...   → ExprWhile
 ├─ "return" ...  → ExprReturn
 └─ (default)     → parse_expression
```

Cada produção devolve um `Expr *` da arena, ou NULL quando falha. Não há estado compartilhado além
do `Parser`: cursor, `Diagnostics` e arena.

---

## Nível superior

Todo item de nível superior precisa começar por uma palavra-chave de tipo (`int`, `int64`, `float`,
`char`, `string`, `bool`) — que é ou o tipo de retorno de uma função, ou o tipo de uma variável
global. Uma instrução solta sem esse prefixo (uma chamada direta, por exemplo) é rejeitada com erro
de sintaxe, citando o que apareceu no lugar.

O tipo é consumido por `match_type_kw` **antes** de se saber qual das duas construções vem, e
`parse_scope_declaration` o recupera com `previous(ps)`. É também de lá que sai a posição
(`line`/`col`) do nó, para que o erro aponte o começo da declaração e não o meio dela.

Logo depois do tipo vem o nome, lido por `parse_identifier_expr` — e é só então, olhando o
**caractere seguinte**, que se sabe o que está sendo declarado: `(` abre uma lista de parâmetros e o
nó vira `ExprFuncDecl`; qualquer outra coisa faz dele um `ExprVarDecl` de frame `Global`.

```tarmac
int main() { ... }            // declaração de função
int contador = 0;             // variável global
print("oi");                  // ERRO: não começa com um tipo
```

> **A palavra-chave `function` saiu na `0.5.0-alpha`.** Antes, a declaração era
> `int function main() { ... }`, e era o token `function` que separava as duas produções — que até
> então eram duas funções distintas, `parse_function_declaration` e `parse_global_declaration`. Como
> o cabeçalho das duas é idêntico (tipo e nome), a palavra-chave era o único trabalho que a gramática
> exigia do programador para dizer algo que o `(` já dizia. As duas produções viraram uma só,
> `parse_scope_declaration`, e a linguagem ficou mais perto do C — ao custo de uma **quebra de
> compatibilidade**: todo `.tm` anterior precisa perder o `function`.

---

## Blocos e o ponto e vírgula

`parse_body_block` reconhece `{ ... }` e devolve a lista pelos parâmetros de saída — um bloco não é
um nó, é o *conteúdo* de um (corpo de função, ramo de `if`, corpo de `while`).

A regra do `;` é a mesma dos dois lados: uma instrução que termina em `}` (um `if`/`while` aninhado,
o corpo de uma função) dispensa o ponto e vírgula, e as demais o exigem. É por isso que tanto o
laço do bloco quanto o do nível superior conferem `previous(ps).kind != RBrace` antes de pedir o
`;`.

---

## Declarações de variável

`<tipo> <nome> [= <inicializador>]`, com o nó `ExprVarDecl` carregando o `FrameType` que diz onde a
variável vive:

| Onde | Produção | `frame` | Destino previsto |
|---|---|---|---|
| Fora de qualquer função | `parse_scope_declaration` | `Global` | dado estático em `.data` |
| Dentro de uma função | `parse_declaration` | `Local` | slot na stack |

As duas produções são quase idênticas de propósito: o que muda é só o `frame`, e é ele que a
análise semântica e a geração de código vão consultar para decidir o tratamento. `External` existe
no `enum` (`types.h`) mas ainda não é produzido por ninguém.

Parâmetros de função passam pela mesma `parse_declaration`, e daí saem como nós `ExprVarDecl` —
o que também significa que um parâmetro com inicializador (`int f(int a = 1)`) é aceito
pela sintaxe, ainda que não tenha significado.

---

## Instruções (`if`, `while`, `return`)

```tarmac
if x > 5 { ... } else { ... }     // parênteses opcionais
while contador < 5 { ... }
return x + 1;                      // ou `return;`
```

Os parênteses da condição são opcionais sem nenhum tratamento especial: `parse_expression` para
sozinha no `{`, porque nenhuma produção da cadeia de expressão casa com `LBrace`. Quando eles
aparecem, entram por `parse_primary`, como um agrupamento comum.

- **`if`** — o ramo `else` é opcional; quando não existe, o nó fica com `else_body = NULL` e
  `else_count = 0`, escritos explicitamente (a arena não zera o que entrega).
- **`while`** — mesma forma, sem ramo alternativo.
- **`return`** — o valor é opcional: um `;` logo depois da palavra-chave produz `ret.value = NULL`.

A validação de tipo da condição (precisa resolver para `bool`) **não** acontece aqui — é assunto da
análise semântica.

---

## Expressões e precedência

A precedência não está nos nós: ela é a própria ordem das produções. Cada nível chama o de cima e
só agrupa o que sobra, então a árvore já sai com o agrupamento certo.

| Nível | Produção | Operadores | Associatividade |
|---|---|---|---|
| 1 | `parse_assignment` | `=` `+=` `-=` `*=` `/=` | à direita |
| 2 | `parse_equality` | `==` | à esquerda |
| 3 | `parse_relational` | `>` `>=` `<` `<=` | à esquerda |
| 4 | `parse_additive` | `+` `-` | à esquerda |
| 5 | `parse_multiplicative` | `*` `/` | à esquerda |
| 6 | `parse_unary` | `-` (menos unário) | à direita |
| 7 | `parse_postfix` | `[ ]` (indexação), `.` (método) | à esquerda |
| 8 | `parse_primary` | literais, identificador, chamada, `( )` | — |

A associatividade à esquerda vem de reaproveitar `left` como acumulador dentro do laço: cada
operador encontrado transforma o que já havia no filho esquerdo do nó novo. A atribuição inverte
isso ao chamar a si mesma para o lado direito, e é por isso que `a = b = c` vira `a = (b = c)`.

**Alvo de atribuição.** O lado esquerdo é reconhecido como expressão qualquer e só depois é
conferido: se não for um `ExprIdentifier` nem um `ExprIndex`, sai erro de sintaxe apontando o início
dele. Isso mantém a produção simples e ainda assim rejeita `1 = 2` com uma mensagem específica, em
vez de um "token inesperado" genérico. O `ExprIndex` como alvo é o que permite `v[0] = 9` — ver
[Arrays](#arrays-novo-e-em-desenvolvimento).

### Menos unário, como açúcar

`-x` **não tem nó próprio**: `parse_unary` o desfaz em duas formas, conforme o operando.

```tarmac
int a = -5;      // o literal já nasce negativo
int b = -a;      // vira `0 - a`, um ExprBinary com OpSub
```

Dobrar o literal na hora não é só economia de um nó. Sem isso, `-9223372036854775808` teria de
passar pelo positivo correspondente, que não cabe em `int64_t`; com a dobra, o valor nunca existe
positivo. É o mesmo motivo pelo qual `_format_int`, no runtime, lida com `INT64_MIN` sem caso
especial.

A produção chama a si mesma, o que faz `-(-x)` funcionar e coloca o unário **acima** da
multiplicação na precedência — ele fica entre `parse_multiplicative` e `parse_postfix`, então
`-a * b` agrupa como `(-a) * b`.

> Um nó dedicado (`ExprUnary`) está previsto. Enquanto o açúcar serve, a análise semântica e a
> geração de código não precisam conhecer construção nenhuma nova: o que chega a elas é um `OpSub`
> comum. Ver o [`TODO.md`](../TODO.md).

### Atribuições compostas, como açúcar

`+=`, `-=`, `*=` e `/=` também **não têm nó próprio**: `parse_assignment` desfaz cada um na forma
estendida ainda no Parser.

```tarmac
a += 5;       // vira `a = a + 5`
v[i] *= 2;    // vira `v[i] = v[i] * 2` — elemento de array também é alvo válido
```

O Lexer entrega cada operador como um token só (`PlusEqual`, `MinusEqual`, `StarEqual`,
`SlashEqual`), pelo mesmo *lookahead* de um caractere que `==`, `>=` e `<=` já usavam. No Parser,
`match_compound` traduz o token na operação binária correspondente e a devolve por parâmetro;
`OpNone` — o membro de `BinaryOp` que nenhuma expressão produz — é o sentinela que distingue "era
um `=` simples" de "era um composto" sem uma segunda variável de controle. Ele nunca chega a um nó:
existe só dentro de `parse_assignment`, e é por isso que a análise semântica e a geração de código
não têm um caso para ele.

A vantagem é a mesma do menos unário: nenhuma fase posterior conhece construção nova. O que chega
adiante é um `ExprAssign` cujo valor é um `ExprBinary` comum, então as regras de tipo, a coerção
implícita e o código emitido são exatamente os de `a = a + 5` escrito por extenso. O alvo passa
pela mesma validação do `=` (precisa ser `ExprIdentifier` ou `ExprIndex`), e a precedência não
muda: o lado direito é lido inteiro pela própria `parse_assignment`, e o composto é aplicado por
fora — `x *= 2 + 3` agrupa como `x = x * (2 + 3)`.

Duas consequências do dessugaramento que valem registro:

- **O alvo aparece duas vezes na árvore.** O mesmo nó `left` vira o alvo do `ExprAssign` **e** o
  filho esquerdo do `ExprBinary`. Num erro que envolve o alvo, os dois caminhos são validados e o
  diagnóstico sai dobrado: `y += 1` com `y` não declarado relata "atribuição a variável não
  declarada" e "variável não declarada", ambos para o mesmo `y`.
- **O alvo é avaliado duas vezes na execução** — uma como destino da escrita, outra como operando
  da leitura. Hoje isso não tem efeito observável, porque as formas de alvo aceitas (variável e
  índice literal ou variável simples) não têm efeito colateral. No dia em que o índice aceitar uma
  expressão qualquer (`v[f()] += 1`, com `f` mexendo em estado), a dobra passa a ser comportamento
  visível e esta decisão terá de ser revista.

---

## Chamadas e métodos

```tarmac
soma(3, 4)      // ExprCall   — identificador seguido de '('
nome.len()      // ExprMethod — receptor antes do '.'
```

Os dois usam a mesma variante da união (`as.call`), e a diferença está na tag e no conteúdo de
`args`: numa chamada de método o **receptor entra como `args[0]`**, seguido dos argumentos
explícitos. É a convenção que a análise semântica e a geração de código vão esperar ao resolver a
assinatura pelo par (tipo do receptor, nome).

`parse_postfix` roda em laço, de modo que `a.b().c()` encadeia: cada método vira o receptor do
seguinte. O mesmo laço cuida da indexação, mas os dois caminhos são exclusivos — `v[0].len()` não é
reconhecido.

A lista de argumentos é montada numa `ExprList` — vetor de heap temporário — e só no fim copiada
para a arena por `ast_list_commit`. Ver
[`docs/architecture.md`](architecture.md#a-ast-como-união-etiquetada) para o porquê das duas
memórias.

Quem a chamada resolve — se existe, quantos argumentos aceita e de que tipos — é assunto da análise
semântica, contra a `FunctionTable`. O Parser só reconhece a forma.

---

## Literais e escapes

O Lexer entrega literais de string e de caractere **sem as aspas** e com as sequências de escape
validadas mas **não decodificadas**: `'\n'` chega como os dois bytes `\` e `n`. Isso é deliberado —
um valor decodificado não existe no buffer de origem e exigiria armazenamento próprio, e o Lexer
não aloca nada.

A decodificação acontece em quem materializa o valor, e por isso em dois lugares diferentes:

- **caractere** → `decode_char_literal`, aqui no Parser: o valor cabe num byte do nó, então não há
  o que alocar;
- **string** → na geração de código, que repassa a fatia crua ao `as` dentro de um `.string` e
  deixa o próprio assembler resolver os escapes. O comprimento gravado no header do objeto, esse
  sim, é contado já decodificado.

---

## Arrays (novo e em desenvolvimento)

> 🚧 **Recurso novo.** Chegou na `0.2.0-alpha` e continua evoluindo. Declarar, inicializar, ler e
> atribuir já funcionam, com índice literal **ou variável**; o que ainda falta está no fim desta
> seção e no [`TODO.md`](../TODO.md).

O tamanho vem **antes** do nome, colado ao tipo — `int[3] v`, e não `int v[3]` como em C:

```tarmac
int[3] v = { 10, 20, 30 };
print(v[0]);
v[1] = 99;

int i = 0;
while i < 3 {
    v[i] = v[i] * 2;    // índice variável, na leitura e na escrita
    i = i + 1;
}
```

Ler a forma de uma vez só é o motivo da escolha. `parse_type` consome a palavra-chave e o `[N]`
juntos, então quando `parse_declaration` chega ao identificador já sabe tudo sobre o valor: a
categoria, o tamanho de um elemento e quantos elementos há. O tipo resultante é um `DataType`
(`types.h`) cujo `type` continua sendo o do **elemento** — `int[3]` guarda `Int`, e o que o
distingue de um `int` é o `is_array`.

O inicializador `{ ... }` é reconhecido por `parse_array_literal`, chamado só quando o `=` de uma
declaração é seguido de `{`. Ele **não** é uma expressão de primeira classe: não entra na cadeia de
precedência, não pode ser passado a uma função nem devolvido de uma. Quem lhe dá tipo é a declaração
à esquerda, e é a análise semântica que confere a contagem e o tipo de cada elemento.

A indexação entra em `parse_postfix`, no mesmo nível da chamada de método. O resultado de `v[i]` é o
elemento: mesmo tipo base, com a forma de array desligada.

### Largura do acesso

Cada elemento é lido e escrito **na largura do seu tipo**, e não em 64 bits como o resto das
expressões. É o que faz um `int[3]` ocupar 12 bytes em vez de sobrepor os elementos: `mov_suffix` e
`reg_a` (codegen.c) escolhem o par instrução/registrador pela `size_of` do elemento, e `mov_load`
escolhe a leitura — que estende o **sinal** até `%rax`, para que um `int` negativo continue negativo
onde o resto da expressão o encontra.

O espaço é reservado nos dois lugares que precisam concordar: `count_slots` soma quantos slots de 8
bytes o array ocupa, dimensionando o `subq` do prólogo, e `tarm_symbol_table_declare` distribui os
offsets com `size_of * array_len`.

### Índice literal e índice variável

Os dois caminhos existem, e a diferença está em **quando** o endereço é conhecido:

- **literal** (`v[2]`) — o endereço é `slot + i * size_of`, resolvido na geração de código e emitido
  como um deslocamento constante. A análise semântica confere a faixa aqui, porque é o único caso em
  que o valor do índice se conhece antes de o programa rodar.
- **variável** (`v[i]`) — entra o modo de endereçamento escalado do x86,
  `offset(%rbp, %rcx, escala)`, que faz a multiplicação em tempo de execução sem instrução extra. A
  escala é o `size_of` do elemento, e o processador só aceita 1, 2, 4 ou 8 — todos os tipos da
  linguagem cabem nesse conjunto.

Na **escrita** com índice variável há um cuidado a mais: o valor a gravar e o índice terminam ambos
em `%rax`, então o valor é empilhado enquanto o índice é avaliado, e volta depois — o mesmo padrão
que uma operação binária usa para não perder o lado esquerdo.

O índice precisa ser do tipo `int`: um `int64` é recusado pela análise semântica, porque não há
conversão implícita que estreite.

### O que ainda não funciona

| Limitação | Efeito |
|---|---|
| **Menos elementos que o declarado** | `int[3] v = {1}` é aceito, e os dois últimos ficam com o que houvesse na stack |
| **Array como parâmetro, retorno ou global** | não reconhecido |

### Faixa conferida nos dois momentos

A checagem acompanha o que dá para saber em cada ponto:

| Índice | Onde a faixa é conferida | Custo em tempo de execução |
|---|---|---|
| literal (`v[2]`) | análise semântica | nenhum — não se emite verificação |
| calculado (`v[i]`, `v[i + 1]`) | código emitido antes do acesso | um `cmpq` e um `jae` |

Com índice calculado, a Codegen emite a comparação contra `array_len` e desvia para `fatal_error_`
(ver [`docs/runtime.md`](runtime.md#aborto-de-execucao)), que escreve em *stderr* e encerra com
código 1. Vale na **leitura e na escrita** — a escrita é a que mais importa, porque um índice fora
da faixa ali não devolve lixo, corrompe o frame.

O salto é `jae`, e não `jge`, de propósito: sendo comparação **sem sinal**, um índice negativo vira
um número enorme e cai no mesmo `>= array_len`. Uma comparação só cobre os dois limites, sem um
segundo teste contra zero.

O que sobra está no [`TODO.md`](../TODO.md).

---

## Erros e recuperação

Todo erro passa por `expect` ou por uma chamada direta a `tarm_error_at`: a mensagem é registrada
no `Diagnostics` com a posição do token que apareceu, e a produção devolve NULL. Quem chamou
propaga o NULL, liberando a `ExprList` temporária no caminho.

**Ainda não há sincronização.** `tarm_parser_program` para na primeira produção que falha, então
uma rodada relata um erro de sintaxe por vez — diferente do Lexer, que atravessa o arquivo inteiro
acumulando. Um passo de recuperação (descartar tokens até o próximo `;` ou `}` e retomar) é a
direção natural, e é o que faria o `Diagnostics` acumulado valer também aqui.

---

## Pendências conhecidas

- **Sem `!=` e sem operadores lógicos** (`&&`, `||`) — não há token para eles no Lexer.
- **Sem checagem de estouro em literal inteiro.** `parse_int_slice` acumula em `int64_t` e dá a
  volta em silêncio; a faixa do tipo declarado só é conhecida na análise semântica.
- **`float` não chega ao fim.** O literal é reconhecido e o tipo existe, mas a geração de código
  recusa: os valores viajam todos em registradores inteiros de 64 bits, e ponto flutuante exige a
  família `%xmm`.
- **`Buffer` não existe neste port.** O tipo, a palavra-chave `buffer` (que continua no `enum` de
  tokens, mas fora de `match_type_kw`) e a nativa `read_buf` do Tarmac em C++ ficaram de fora;
  `String` continua sendo um objeto com header, como lá.
