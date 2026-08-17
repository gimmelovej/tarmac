# Contribuindo com o Tarmac em C

Obrigado pelo interesse! O Tarmac é um projeto pessoal/educacional, e este repositório — a
releitura em C do [compilador original em C++](https://github.com/gimmelovej/tarmac-cpp) — está em
**desenvolvimento**: as cinco etapas do pipeline existem e o Driver as encadeia até montar e linkar
o binário, mas o executável ainda não sai correto. Veja a tabela [Estado do
desenvolvimento](README.md#estado-do-desenvolvimento) antes de reportar algo, para checar se já é
uma limitação conhecida.

## Fluxo de branches e Pull Requests

O projeto usa um fluxo simples inspirado no *git-flow*:

- **`main`** — linha estável; recebe apenas *merges* de release (cada release ganha uma tag e um
  GitHub Release). Não se faz *commit* direto aqui.
- **`development`** — linha de integração; é a base das mudanças do dia a dia.
- ***feature branches*** — toda mudança sai de `development` em um branch próprio
  (`feat/...`, `fix/...`, `docs/...`, `build/...`, `chore/...`) e volta via **Pull Request** para
  `development`. Nada de *commit* direto em `development`/`main`.

Ao abrir o PR, descreva o *porquê* da mudança e como você verificou (ver
[Compilando e testando](#compilando-e-testando-localmente)). O [CI](#integração-contínua-ci) roda
automaticamente.

## Compilando e testando localmente

```bash
cmake -S . -B build          # configura (Debug por padrão: -g3 -O0 + ASan/UBSan)
cmake --build build          # compila o próprio Tarmac (executável build/tarm)
./build/tarm arquivo.tm      # roda o pipeline sobre um arquivo .tm
```

Ainda não há suíte de testes — a verificação padrão é escrever um `.tm` mínimo que exercite o caso
em questão e observar o **comportamento real do binário gerado**, não só a leitura do código. Ao
propor uma mudança em qualquer etapa do pipeline, compile e rode um exemplo antes de descrever o
resultado no PR.

## Integração Contínua (CI)

Cada push/PR para `main` e `development` dispara o workflow
[`.github/workflows/ci.yml`](.github/workflows/ci.yml), que compila o Tarmac em **Debug e Release**,
roda um *smoke test* de ponta a ponta e compila e executa o `example.tm`. Rode o equivalente
localmente antes de abrir o PR:

```bash
cmake --build build
printf 'int function main(){ print("ci ok\\n"); return 0; }\n' > /tmp/smoke.tm
./build/tarm /tmp/smoke.tm && /tmp/smoke     # deve imprimir: ci ok
./build/tarm example.tm && ./example
```

A build de Debug já vem com ASan e UBSan e `-fno-sanitize-recover=all`, então ela **aborta no
primeiro** *out-of-bounds*, *use-after-free* ou *overflow* com sinal. Rode sempre em Debug ao
mexer em alocação, ponteiros ou aritmética de índice — é a rede que, em C, substitui boa parte do
que o C++ garantia sozinho.

## Reportando bugs

Ao abrir uma *issue*, inclua o arquivo `.tm` mínimo que reproduz o problema e a saída obtida
(mensagem de erro, comportamento do executável, relatório do sanitizer ou *segfault*). Para áreas
já sinalizadas como 🟡/🔴 na tabela de [Estado do
desenvolvimento](README.md#estado-do-desenvolvimento), não é necessário abrir uma issue nova — só é
útil se você tiver uma pista adicional (ex.: um caso não coberto pela limitação já descrita).

## Padrão de documentação

### Cabeçalho de arquivo

Todo `.h` e `.c` abre com o mesmo bloco de identificação; nos cabeçalhos, ele é seguido de um
`@file`/`@brief` que diz o papel do módulo no pipeline:

```c
// ================================================================================================
// File: lexer.h
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
/// @file
/// @brief Primeira etapa do pipeline: transforma código-fonte bruto numa sequência de `Token`.
/// @see docs/architecture.md#pipeline
```

### C (Doxygen)

- Funções e structs **públicas** (as declaradas em `include/*.h`): bloco Doxygen completo —
  `@brief`, `@param`, `@return`, `@note`/`@warning` quando o recurso é incompleto/experimental,
  `@see docs/architecture.md#secao` quando há raciocínio de design que vale a pena aprofundar.
- Campos de struct ganham `///<` quando o nome não basta (`start`, `tok_col`, `capacity`); os
  óbvios ficam sem.
- Funções **internas** (`static`, nos `.c`): comentário `//` acima explicando o *porquê* da
  escolha, ou nenhum, se autoexplicativa. Nada de repetir a assinatura em prosa.
- Decisões de arquitetura e o "porquê" por trás de um mecanismo **não** ficam em blocos de prosa
  acima das funções — vivem em [`docs/architecture.md`](docs/architecture.md) ou, para a gramática,
  em [`docs/parser.md`](docs/parser.md), referenciadas via `@see`.

```c
/// @brief Percorre todo o código-fonte e preenche `tokens` com a sequência reconhecida.
/// @param tokens Lista de saída; recebe uma lista nova, sempre terminada por `EndOfFile`.
/// @return `true` se a varredura terminou sem nenhum erro registrado no `Diagnostics`.
/// @note Um lexema não reconhecido não interrompe a varredura: vira um token `Invalid`, o erro é
/// acumulado e a leitura segue.
bool tarm_lexer_tokenize(Lexer *lx, TokenList *tokens);
```

### Duas coisas que o C não diz sozinho

O sistema de tipos do C não expressa nem posse de memória nem contrato de erro, então isso fica
por conta da documentação — e é o que mais falta faz quando o código é lido meses depois:

- **Quem libera o quê.** Toda função que aloca ou devolve memória diz, em `@note`/`@warning`, de
  quem é o recurso e com o quê ele é liberado; todo ponteiro guardado por uma struct diz de quem
  ele é emprestado e por quanto tempo precisa viver.
- **O que o retorno significa.** Como não há exceção, o `bool` de retorno é o canal de erro:
  `@return` diz exatamente o que `true`/`false` significam ali (ex.: "terminou sem erros no
  `Diagnostics`", que não é o mesmo que "chegou ao fim da entrada").

### Recursos incompletos/experimentais

Se sua mudança mexe numa área ainda instável, documente com `@note`/`@warning` de forma **breve** —
o que não funciona ainda e o que já é coberto — em vez de uma descrição forense do bug. Isso é
intencional: o projeto está em desenvolvimento ativo e detalhes profundos de um bug em uma área que
ainda vai mudar tendem a ficar desatualizados rápido. O lugar do detalhe é a tabela de [Estado do
desenvolvimento](README.md#estado-do-desenvolvimento).

### Assembly / runtime (`.s`)

Cabeçalhos enxutos, focados em interface (`In`/`Out`/`Clobbers` e a *syscall* usada); raciocínio
extenso vive em [`docs/runtime.md`](docs/runtime.md), referenciado via `Reference:`.

```
# brk_free — Libera memória movendo o program break para %rdi (syscall brk, #12).
# Reference: docs/runtime.md#heap-brk-linear
#
# In:       %rdi = endereço do break desejado
# Out:      %rax = break antigo, ou 0 em caso de falha
# Clobbers: %rax, %rdi, %rdx
```

## Convenções de código

- `tarm_` prefixa tudo que é público; `static` marca o que é interno ao `.c`.
- `tarm_<modulo>_init` é o construtor de cada struct de estado — a struct é declarada pelo
  chamador (normalmente na pilha) e inicializada por essa chamada.
- C11 sem extensões GNU: a build usa `-std=c11` (`CMAKE_C_EXTENSIONS OFF`), então `typeof`,
  *statement expressions* e aritmética em `void*` viram erro aqui, e não meses depois em outra
  máquina.
- Cabeçalho autossuficiente: cada `.h` inclui o que os seus próprios tipos exigem
  (`<stdint.h>`, `<stddef.h>`, ...), de modo que a ordem de inclusão no `.c` não importe.

## Commits

Mensagens em português, no estilo [Conventional Commits](https://www.conventionalcommits.org/)
(`feat:`, `fix:`, `docs:`, `chore:`, ...), descrevendo o *porquê* da mudança, não só o *o quê*.

## Licença

Ao contribuir, você concorda que sua contribuição será distribuída sob a mesma licença do projeto
([MIT](LICENSE)).
