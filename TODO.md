# TODO — próximas correções e novidades

Lista viva do que vem a seguir. O que já está **feito** vive no
[`CHANGELOG.md`](CHANGELOG.md); o que já **funciona** está na tabela de
[Estado do desenvolvimento](README.md#estado-do-desenvolvimento). Aqui fica só o que ainda não foi
resolvido, em ordem de prioridade.

Convenção das marcas: 🔴 corrige um comportamento errado · 🟡 completa algo que existe pela metade ·
🟢 recurso novo · ⚪ melhoria de infraestrutura ou processo.

---

## Patch — arrays (`0.2.x`)

O suporte a array entrou na `0.2.0-alpha` e a `0.2.1-alpha` fechou a maior parte do que faltava:
largura de acesso por elemento, espaço correto no frame, faixa conferida e atribuição a elemento.
O que resta, na ordem em que faz sentido atacar:

| # | Marca | Item | Onde |
|---|---|---|---|
| 1 | 🔴 | **A escrita não confere o que a leitura confere.** O caso `ExprIndex` de `ExprAssign` não testa se a base é um array, se o índice é literal nem se ele cabe na faixa. `v[i] = 5` com `i` variável lê a variante errada da união e vira um offset arbitrário (`744(%rbp)` num teste); `x[5] = 9` num escalar passa igual. É a mesma checagem que a leitura já faz — falta espelhá-la. | `semantic.c`, `codegen.c` |
| 2 | 🔴 | **`sym` usado sem teste de NULL** no caso `ExprIndex` de `ExprAssign` da Codegen. Chega lá com a semântica limpa, mas é a única leitura da tabela de símbolos sem rede. | `codegen.c` |
| 3 | 🟡 | **Inicializador menor que o declarado** é aceito em silêncio: `int[3] v = {1}` deixa os dois últimos elementos com lixo da stack. Zerar o resto, ou recusar. | `semantic.c`, `codegen.c` |
| 4 | 🟡 | **Índice variável na leitura**, com aritmética de endereço em tempo de execução (`base + i * size_of`). Junto com o item 1, é o que torna array útil dentro de um `while`. | `codegen.c` |
| 5 | 🟡 | **`{}` vazio** produz "token inesperado: '}'": o teste de lista vazia confere `RParen` no lugar de `RBrace`. | `parser.c`, `parse_array_literal` |
| 6 | 🟡 | **`array_len - 1` quando `array_len` é 0**: a conta é em `size_t` e dá a volta. Um `int[0]` só não chega lá porque o `{}` falha antes. | `semantic.c` |
| 7 | 🟡 | **Array global**, em `.data`, com o literal virando uma sequência de `.quad`. | `codegen.c` |
| 8 | 🟢 | **Verificação de faixa em tempo de execução**, para o índice que a checagem estática não alcança. | `codegen.c`, runtime |
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
