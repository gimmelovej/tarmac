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
     └─ <tipo> ...
         ├─ seguido de "function"  → parse_function_declaration
         └─ seguido de <nome>      → parse_global_declaration

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
`char`, `string`, `bool`) — que é ou o tipo de retorno de uma `function`, ou o tipo de uma variável
global. Uma instrução solta sem esse prefixo (uma chamada direta, por exemplo) é rejeitada com erro
de sintaxe, citando o que apareceu no lugar.

O tipo é consumido por `match_type_kw` **antes** de se saber qual das duas produções vem; as duas o
recuperam com `previous(ps)`. É também de lá que sai a posição (`line`/`col`) do nó, para que o erro
aponte o começo da declaração e não o meio dela.

```tarmac
int function main() { ... }   // declaração de função
int contador = 0;             // variável global
print("oi");                  // ERRO: não começa com um tipo
```

---

## Blocos e o ponto e vírgula

`parse_body_block` reconhece `{ ... }` e devolve a lista pelos parâmetros de saída — um bloco não é
um nó, é o *conteúdo* de um (corpo de função, ramo de `if`, corpo de `while`).

A regra do `;` é a mesma dos dois lados: uma instrução que termina em `}` (um `if`/`while` aninhado,
o corpo de uma função) dispensa o ponto e vírgula, e as demais o exigem. É por isso que tanto o
laço do bloco quanto o do nível superior conferem `previous(ps).kind != RBrace` antes de pedir o
`;`.

> **Ressalva desta versão:** a exigência só é de fato aplicada no nível superior, onde o `;` passa
> por `expect`. Dentro de um bloco ele passa por `match`, então a ausência é aceita em silêncio:
> `int x = 1 int y = 2` é reconhecido sem erro.

---

## Declarações de variável

`<tipo> <nome> [= <inicializador>]`, com o nó `ExprVarDecl` carregando o `FrameType` que diz onde a
variável vive:

| Onde | Produção | `frame` | Destino previsto |
|---|---|---|---|
| Fora de qualquer função | `parse_global_declaration` | `Global` | dado estático em `.data` |
| Dentro de uma função | `parse_declaration` | `Local` | slot na stack |

As duas produções são quase idênticas de propósito: o que muda é só o `frame`, e é ele que a
análise semântica e a geração de código vão consultar para decidir o tratamento. `External` existe
no `enum` (`types.h`) mas ainda não é produzido por ninguém.

Parâmetros de função passam pela mesma `parse_declaration`, e daí saem como nós `ExprVarDecl` —
o que também significa que um parâmetro com inicializador (`int function f(int a = 1)`) é aceito
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
| 1 | `parse_assignment` | `=` | à direita |
| 2 | `parse_equality` | `==` | à esquerda |
| 3 | `parse_relational` | `>` `>=` `<` `<=` | à esquerda |
| 4 | `parse_additive` | `+` `-` | à esquerda |
| 5 | `parse_multiplicative` | `*` `/` | à esquerda |
| 6 | `parse_postfix` | `[ ]` (indexação), `.` (método) | à esquerda |
| 7 | `parse_primary` | literais, identificador, chamada, `( )` | — |

A associatividade à esquerda vem de reaproveitar `left` como acumulador dentro do laço: cada
operador encontrado transforma o que já havia no filho esquerdo do nó novo. A atribuição inverte
isso ao chamar a si mesma para o lado direito, e é por isso que `a = b = c` vira `a = (b = c)`.

**Alvo de atribuição.** O lado esquerdo é reconhecido como expressão qualquer e só depois é
conferido: se não for um `ExprIdentifier` nem um `ExprIndex`, sai erro de sintaxe apontando o início
dele. Isso mantém a produção simples e ainda assim rejeita `1 = 2` com uma mensagem específica, em
vez de um "token inesperado" genérico. (Um `ExprIndex` passa pelo Parser, mas a geração de código
ainda o recusa — ver [Arrays](#arrays-novo-e-em-desenvolvimento).)

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

> 🚧 **Recurso novo.** Chegou na `0.2.0-alpha` e ainda é a parte mais instável da linguagem. Já dá
> para declarar, inicializar e ler; o que **não** funciona está listado no fim desta seção e no
> [`TODO.md`](../TODO.md).

O tamanho vem **antes** do nome, colado ao tipo — `int[3] v`, e não `int v[3]` como em C:

```tarmac
int[3] v = { 10, 20, 30 };
print(v[0]);
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

### O que ainda não funciona

| Limitação | Onde | Efeito |
|---|---|---|
| **Elementos se sobrepõem na memória** | Codegen grava cada elemento com `movq` (8 bytes) num passo de `size_of` (4, num `int`) | `{10, 20, 30}` lê de volta `10 0 30` |
| **Espaço do frame subdimensionado** | `count_slots` conta um slot por declaração, e `declare_local` reserva `SLOT_SIZE` (8 bytes) | um `int[10]` reserva 8 bytes e escreve 40, invadindo o resto do frame |
| **Atribuir a um elemento** | `v[0] = 9` | o Parser aceita o alvo, a Codegen recusa com "alvo de atribuição não suportado" |
| **Índice variável** | `v[i]` com `i` variável | só índice literal é aceito; o resto vira erro |
| **Faixa com `>` no lugar de `>=`** | análise semântica | `v[2]` num `int[2]` passa pela checagem |
| **Sem verificação em tempo de execução** | — | um índice fora da faixa que escape da checagem estática lê memória vizinha |
| **Array como parâmetro, retorno ou global** | — | não reconhecido |
| **`{}` vazio** | `parse_array_literal` confere `RParen` no lugar de `RBrace` | erro confuso: "token inesperado: '}'" |

Enquanto isso não fecha, o uso seguro é o do [`example.tm`](../example.tm): array pequeno, de tipo
de 8 bytes (`int64`), com índice literal.

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

- **Sem menos unário.** `-5` não é reconhecido: `Minus` só existe como operador binário, então um
  literal negativo é erro de sintaxe.
- **Sem `!=` e sem operadores lógicos** (`&&`, `||`) — não há token para eles no Lexer.
- **Inicializador aceita instrução, não só expressão.** `parse_declaration` e
  `parse_global_declaration` chamam `parse_statement` depois do `=`, então `int x = while ...` passa
  pela sintaxe.
- **Sem checagem de estouro em literal inteiro.** `parse_int_slice` acumula em `int64_t` e dá a
  volta em silêncio; a faixa do tipo declarado só é conhecida na análise semântica.
- **`float` não chega ao fim.** O literal é reconhecido e o tipo existe, mas a geração de código
  recusa: os valores viajam todos em registradores inteiros de 64 bits, e ponto flutuante exige a
  família `%xmm`.
- **`Buffer` não existe neste port.** O tipo, a palavra-chave `buffer` (que continua no `enum` de
  tokens, mas fora de `match_type_kw`) e a nativa `read_buf` do Tarmac em C++ ficaram de fora;
  `String` continua sendo um objeto com header, como lá.
