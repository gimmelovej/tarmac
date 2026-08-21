# TODO — próximas correções e novidades

Lista viva do que vem a seguir. O que já está **feito** vive no
[`CHANGELOG.md`](CHANGELOG.md); o que já **funciona** está na tabela de
[Estado do desenvolvimento](README.md#estado-do-desenvolvimento). Aqui fica só o que ainda não foi
resolvido, em ordem de prioridade.

Convenção das marcas: 🔴 corrige um comportamento errado · 🟡 completa algo que existe pela metade ·
🟢 recurso novo · ⚪ melhoria de infraestrutura ou processo.

---

## Patch — arrays (`0.2.x`)

O suporte a array entrou na `0.2.0-alpha` e vem fechando desde então: largura de acesso por
elemento, espaço correto no frame, faixa conferida, atribuição a elemento e índice variável em
leitura e escrita. O que resta, na ordem em que faz sentido atacar:

| # | Marca | Item | Onde |
|---|---|---|---|
| 1 | 🔴 | **Verificação de faixa em tempo de execução.** Com índice variável, nada garante que ele caia dentro do array: `v[i]` com `i` fora da faixa lê memória vizinha, e `v[i] = x` a corrompe. A checagem estática só alcança o índice literal, e ficou insuficiente no momento em que o índice variável passou a ser aceito. Emitir a comparação antes do acesso, com um desvio para uma rotina de aborto no runtime. | `codegen.c`, runtime |
| 2 | 🟡 | **Índice como expressão** (`v[i + 1]`): hoje só literal ou variável simples são reconhecidos, e o resto vira "tipo não tratado em array". O caminho geral é avaliar a expressão do índice para um registrador e usá-lo no modo escalado, que já está lá. | `codegen.c` |
| 3 | 🟡 | **Inicializador menor que o declarado** é aceito em silêncio: `int[3] v = {1}` deixa os dois últimos elementos com o que houvesse na stack. Zerar o resto, ou recusar. | `semantic.c`, `codegen.c` |
| 4 | 🟡 | **Array global**, em `.data`, com o literal virando uma sequência de `.quad`. | `codegen.c` |
| 5 | 🟢 | **Array como parâmetro e retorno** de função — passa a exigir uma decisão sobre passagem por referência. | todas as etapas |
| 6 | 🟢 | **`len()` sobre array**, hoje só disponível para `string`. | `function_table.c` |

## Patch — geral

| Marca | Item | Onde |
|---|---|---|
| 🟡 | **`ast_list_commit` não distingue** lista vazia de falha de alocação: devolve NULL nos dois casos, com `*out_count` já preenchido. | `ast.c` |
| 🟡 | **Alinhamento da arena**: o tamanho é arredondado para 16, mas o cabeçalho de 24 bytes desloca `data`, então os ponteiros saem alinhados a 8. | `arena.c` |
| 🟡 | **Estouro em literal inteiro**: `parse_int_slice` acumula em `int64_t` e dá a volta em silêncio. | `parser.c` |

## Novidades da linguagem

| Marca | Item | Nota |
|---|---|---|
| 🟢 | **Ponto flutuante na geração de código** | o tipo e o literal existem; falta a família `%xmm` no gerador. É a maior ausência em relação ao [Tarmac em C++](https://github.com/gimmelovej/tarmac-cpp) |
| 🟢 | **Nó dedicado para o unário** (`ExprUnary`) | hoje `-x` é açúcar, desfeito no Parser em `0 - x`; um nó próprio abre caminho para `!` e `~`, e evita que a semântica veja um `OpSub` que o usuário não escreveu |
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
