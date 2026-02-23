# Tarmac compiler

Tarmac compiler é um projeto pessoal de um compilador em c++ para a linguagem Tarmac, também criada por mim (Algo completamente inicial).

## Como configurar

Por hora, o uso é extremamente simples. O compilador foi criado utilizando cmake

```bash
mkdir build
cmake -S . -B build
cmake --build build
```

## Como usar

O executável é criado dentro da pasta ./build no seu projeto. O arquivo de argumento, como o exemplo de "test.tarmac", pode ser adaptado de acordo com a documentação fornecida em ./docs.

```bash
./build/tar test.tarmac 
```



## License

[MIT](https://choosealicense.com/licenses/mit/)