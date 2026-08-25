// ================================================================================================
// example.tm — Passeio pelos recursos da linguagem Tarmac reconhecidos pelo compilador em C.
// Compile e execute com: ./build/tarm example.tm && ./example
//
// Destaque desta versão: a palavra-chave `function` SAIU. Uma função agora se declara como em C —
// `int soma(int a, int b) { ... }` —, e o que a distingue de uma variável global é o parêntese
// depois do nome. Ver docs/parser.md#nível-superior.
// ================================================================================================

// Variáveis globais: ficam fora de qualquer função e pedem um literal constante, porque viram dado
// estático no binário — não há onde executar uma expressão antes de o programa começar.
string titulo = "== Tarmac em C: passeio pelos recursos ==\n";
int total = 0;

// Função com parâmetro: tipo de retorno, nome e a lista entre parênteses — o mesmo cabeçalho de
// uma variável global, até o `(` decidir qual das duas é.
int dobro(int n)
{
    return n * 2;
}

// Vários parâmetros: a quantidade e o tipo de cada argumento são conferidos por posição.
int soma(int a, int b)
{
    return a + b;
}

// Sem parâmetros. O nome `x` se repete em outras funções sem conflito: cada uma tem seu escopo.
int resposta()
{
    int x = 42;
    return x;
}

int main()
{
    print(titulo);

    // Cinco dos seis tipos aparecem aqui; `float` ainda não chega à geração de código.
    // Literais inteiros são convertidos implicitamente quando o destino pede.
    int contagem = 3;
    int64 grande = 10;
    char inicial = 65;     // convertido para 'A'
    bool ativo = 1;        // convertido para true
    string nome = "tarmac";

    print("string: ");
    print(nome);
    print("\n");

    print("char e bool: ");
    print(inicial);
    print(" ");
    print(ativo);
    print("\n");

    // Método de tipo: `len()` devolve o comprimento em bytes, lido do header do objeto.
    int64 tamanho = nome.len();
    if tamanho == 6
    {
        print("metodo len(): 6 bytes\n");
    }

    // Chamada aninhada: o valor de cada argumento sobrevive à geração dos seguintes.
    total = soma(dobro(contagem), resposta());
    print("funcoes (soma de dobro com resposta): ");
    print(total);
    print("\n");

    // Rotina nativa: converte texto decimal e para no primeiro byte que não é dígito.
    int lido = atoi("58");
    print("atoi: ");
    print(lido);
    print("\n");

    // Condicional: a condição não exige parênteses e o bloco `else` é opcional.
    if total > 50
    {
        print("condicional: total acima de 50\n");
    }
    else
    {
        print("condicional: total ate 50\n");
    }

    // Laço: a condição é reavaliada ao fim de cada iteração.
    int i = 0;
    while i < contagem
    {
        print("laco: iteracao ");
        print(i);
        print("\n");
        i = i + 1;
    }

    // --- ARRAYS: recurso novo, ainda em desenvolvimento -----------------------------------------
    //
    // O tamanho vem antes do nome (`int[3] v`), colado ao tipo — assim o tipo é lido de uma vez só,
    // com a forma junto. O inicializador `{ ... }` só vale numa declaração.
    //
    // Cada elemento é lido e escrito na largura do seu tipo, então tipos de 1, 4 e 8 bytes convivem
    // sem se sobrepor: um `int[3]` ocupa 12 bytes, um `char[4]` ocupa 4.
    print("array (novo): ");
    int[3] medidas = { 10, 20, 30 };
    print(medidas[0]);
    print(" ");
    print(medidas[1]);
    print(" ");
    print(medidas[2]);
    print("\n");

    // Atribuir a um elemento já funciona, com a mesma coerção implícita de qualquer atribuição.
    medidas[1] = 99;
    print("array (atribuicao): ");
    print(medidas[1]);
    print("\n");

    // O índice também pode ser uma variável, na leitura e na escrita — é o que torna array
    // utilizável dentro de um laço. O endereço sai do modo escalado do x86, sem multiplicação.
    print("array (indice variavel): ");
    int k = 0;
    while k < 3
    {
        medidas[k] = medidas[k] + 1;
        print(medidas[k]);
        print(" ");
        k = k + 1;
    }
    print("\n");

    char[4] iniciais = { 'T', 'a', 'r', 'm' };
    print("array de char: ");
    print(iniciais[0]);
    print(iniciais[3]);
    print("\n");

    // A faixa é conferida nos dois momentos: com índice literal, na compilação (`medidas[3]` seria
    // recusado); com índice calculado, por uma comparação emitida antes do acesso, que aborta o
    // programa em vez de ler ou escrever fora do array.

    // --- MENOS UNÁRIO ---------------------------------------------------------------------------
    //
    // `-x` é açúcar: num literal, o sinal é dobrado no próprio nó; numa variável, vira `0 - x`.
    // Ele fica acima da multiplicação na precedência, então `-contagem * 2` agrupa como
    // `(-contagem) * 2`. A runtime imprime o sinal — antes, um negativo saía como o complemento de
    // dois lido em decimal.
    print("unario: ");
    int negativo = -7;
    print(negativo);
    print(" ");
    print(-contagem);
    print(" ");
    print(-contagem * 2);
    print("\n");

    // --- ATRIBUIÇÕES COMPOSTAS ------------------------------------------------------------------
    //
    // `a += b` é açúcar, desfeito no Parser em `a = a + b`: as regras de tipo, a coerção e o
    // código gerado são os da forma estendida. Funciona em variável e em elemento de array, e o
    // lado direito agrupa inteiro antes do operador: `acum *= 2 + 3` é `acum = acum * (2 + 3)`.
    print("compostas: ");
    int acum = 10;
    acum += 5;      // 15
    acum -= 3;      // 12
    acum *= 2 + 3;  // 60
    acum /= 4;      // 15
    print(acum);
    print(" ");
    medidas[0] += 100;
    print(medidas[0]);
    print("\n");

    // Heap linear via `brk`: a liberação é LIFO, então `brk_free(a)` devolve também o que veio
    // depois de `a`. O `mmap` mapeia regiões independentes, liberadas por ponteiro e tamanho.
    int64 a = brk_alloc(64);
    int64 b = brk_alloc(64);
    brk_free(a);

    int64 regiao = mmap_alloc(4096);
    mmap_free(regiao, 4096);
    print("alocacao: brk e mmap ok\n");

    return 0;
}
