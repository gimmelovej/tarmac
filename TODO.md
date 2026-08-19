# TODO — próximas correções e novidades

Lista viva do que vem a seguir. O que já está **feito** vive no
[`CHANGELOG.md`](CHANGELOG.md); o que já **funciona** está na tabela de
[Estado do desenvolvimento](README.md#estado-do-desenvolvimento). Aqui fica só o que ainda não foi
resolvido, em ordem de prioridade.

Convenção das marcas: 🔴 corrige um comportamento errado · 🟡 completa algo que existe pela metade ·
🟢 recurso novo · ⚪ melhoria de infraestrutura ou processo.

---

## Patch — arrays (`0.2.x`)

O suporte a array entrou na `0.2.0-alpha` e é a parte mais instável da linguagem. Estes são os itens
que fecham o recurso, na ordem em que fazem sentido.

| # | Marca | Item | Onde |
|---|---|---|---|
| 1 | 🔴 | **Elementos se sobrepõem.** O inicializador grava cada elemento com `movq` (8 bytes) num passo de `size_of` (4 bytes num `int`), então `{10, 20, 30}` lê de volta `10 0 30`. Emitir a instrução com a largura do elemento, ou padronizar o slot em 8 bytes. | `codegen.c`, caso `ExprVarDecl` |
| 2 | 🔴 | **Frame subdimensionado.** `count_slots` conta um slot por declaração e `declare_local` reserva `SLOT_SIZE`; um `int[10]` reserva 8 bytes e escreve 40, invadindo o resto do frame. Contar `array_len` slots e reservar `size_of * array_len`. | `codegen.c`, `symbol_table.c` |
| 3 | 🔴 | **Faixa com `>` no lugar de `>=`.** `v[2]` num `int[2]` passa pela checagem estática. | `semantic.c`, caso `ExprIndex` |
| 4 | 🔴 | **Índice não literal lê `as.integer`.** Um `v[i]` com `i` variável lê a variante errada da união antes de chegar à Codegen. Conferir `kind == ExprInteger` antes. | `semantic.c`, caso `ExprIndex` |
| 5 | 🟡 | **Atribuir a um elemento** (`v[0] = 9`): o Parser aceita o alvo, a Codegen recusa. Falta calcular o endereço do slot no caso `ExprIndex` do `ExprAssign`. | `codegen.c` |
| 6 | 🟡 | **Índice variável**, com aritmética de endereço em tempo de execução (`base + i * size_of`). | `codegen.c` |
| 7 | 🟡 | **`{}` vazio** produz "token inesperado: '}'": o teste de lista vazia confere `RParen` no lugar de `RBrace`. | `parser.c`, `parse_array_literal` |
| 8 | 🟡 | **Array global**, em `.data`, com o literal virando uma sequência de `.quad`. | `codegen.c` |
| 9 | 🟢 | **Array como parâmetro e retorno** de função — passa a exigir uma decisão sobre passagem por referência. | todas as etapas |
| 10 | 🟢 | **`len()` sobre array**, hoje só disponível para `string`. | `function_table.c` |

## Patch — geral

| Marca | Item | Onde |
|---|---|---|
| 🟡 | **`;` frouxo dentro de bloco**: exigido no nível superior (`expect`), apenas consumido dentro de um bloco (`match`), então `int x = 1 int y = 2` passa. | `parser.c`, `parse_body_block` |
| 🟡 | **`ast_list_commit` não distingue** lista vazia de falha de alocação: devolve NULL nos dois casos, com `*out_count` já preenchido. | `ast.c` |
| 🟡 | **Alinhamento da arena**: o tamanho é arredondado para 16, mas o cabeçalho de 24 bytes desloca `data`, então os ponteiros saem alinhados a 8. | `arena.c` |
| 🟡 | **Estouro em literal inteiro**: `parse_int_slice` acumula em `int64_t` e dá a volta em silêncio. | `parser.c` |
| 🟡 | **Inicializador aceita instrução**, não só expressão: `int x = while ...` passa pela sintaxe. | `parser.c` |

## Novidades da linguagem

| Marca | Item | Nota |
|---|---|---|
| 🟢 | **Ponto flutuante na geração de código** | o tipo e o literal existem; falta a família `%xmm` no gerador. É a maior ausência em relação ao [Tarmac em C++](https://github.com/gimmelovej/tarmac-cpp) |
| 🟢 | **Menos unário** (`-5`) | hoje `Minus` só existe como operador binário, e um literal negativo é erro de sintaxe |
| 🟢 | **`!=` e operadores lógicos** (`&&`, `||`) | não há token para eles no Lexer |
| 🟢 | **Recuperação de erro no Parser** | descartar tokens até o próximo `;` ou `}` e retomar, para relatar mais de um erro de sintaxe por rodada |
| 🟢 | **Mais de 6 argumentos ou parâmetros** | passagem pela stack, além dos registradores da ABI |
| 🟢 | **Tipo `buffer` e `read_buf`** | existem no compilador em C++; `tarm_read_buf` já está na runtime, sem quem a chame |
| 🟢 | **Laço `for`** | não iniciado |

## Infraestrutura

| Marca | Item |
|---|---|
| ⚪ | **Suíte de testes de verdade**, no lugar do smoke test do CI: um diretório de `.tm` com saída esperada, comparada automaticamente |
| ⚪ | **Proteção do branch `main`** no GitHub, para que a regra do `CONTRIBUTING.md` (só merge de release) deixe de ser só convenção |
| ⚪ | **`tarm_symbol_table_total_bytes` sem chamador**: serve à abordagem de gerar o corpo num buffer e montar o prólogo depois, como fazia o Tarmac em C++ |
| ⚪ | **Instalação** (`cmake --install`): hoje `TARM_RUNTIME_DIR` é um caminho absoluto gravado no binário, e o compilador quebra se o repositório for movido |
