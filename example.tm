// ================================================================================================
// example.tm — Passeio pelos recursos da linguagem Tarmac reconhecidos pelo compilador em C.
// Compile e execute com: ./build/tarm example.tm && ./example
//
// Destaque desta versão: ARRAYS (novo, em desenvolvimento) — ver o bloco no fim de main() e as
// limitações conhecidas em docs/parser.md#arrays-novo-e-em-desenvolvimento.
// ================================================================================================

// Variáveis globais: ficam fora de qualquer função e pedem um literal constante, porque viram dado
// estático no binário — não há onde executar uma expressão antes de o programa começar.
string titulo = "== Tarmac em C: passeio pelos recursos ==\n";
int total = 0;

// Função com parâmetro: o tipo de retorno vem antes da palavra-chave `function`.
int function dobro(int n)
{
    return n * 2;
}

// Vários parâmetros: a quantidade e o tipo de cada argumento são conferidos por posição.
int function soma(int a, int b)
{
    return a + b;
}

// Sem parâmetros. O nome `x` se repete em outras funções sem conflito: cada uma tem seu escopo.
int function resposta()
{
    int x = 42;
    return x;
}

int function main()
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
    // O tamanho vem antes do nome (`int64[3] v`), colado ao tipo — assim o tipo é lido de uma vez
    // só, com a forma junto. O inicializador `{ ... }` só vale numa declaração.
    //
    // Este exemplo usa `int64` de propósito: com elementos de 8 bytes, o passo que a geração de
    // código emite bate com o tamanho do elemento. Num `int[3]` (4 bytes) os elementos ainda se
    // sobrepõem — é a primeira pendência do TODO.md.
    print("array (novo): ");
    int64[3] medidas = { 10, 20, 30 };
    print(medidas[0]);
    print(" ");
    print(medidas[1]);
    print(" ");
    print(medidas[2]);
    print("\n");

    // O índice precisa ser literal por enquanto, e a faixa é conferida em tempo de compilação:
    // `medidas[9]` seria recusado pela análise semântica. Atribuir a um elemento
    // (`medidas[0] = 5;`) ainda não é gerado.

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
