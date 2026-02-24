#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <filesystem>

#include "tokenazation.hpp"
#include "generation.hpp"


int main(int argc, char* argv[]){

    /*
        Verifcando uso de agumento (arquivo)
    */
    if(argc != 2){
        std::cerr << "Uso incorreto" << std::endl;
        std::cerr << "Tarmac <input.tm>" << std::endl;
        return EXIT_FAILURE;
    }

    /*
        Verifcando extensão de arquivo
        .tarmac
    */
    std::filesystem::path filePath = argv[1];
    if(filePath.extension() != ".tm"){
        std::cerr << "A extensão do arquivo deve ser <.tm>: " << filePath << std::endl;
        return EXIT_FAILURE;
    }

    std::string contents;
    { 
        std::stringstream contents_stream;
        std::fstream input(argv[1], std::ios::in);
        contents_stream << input.rdbuf();
        contents = contents_stream.str();
    }

    Tokenizer tokenizer(std::move(contents));
    std::vector<Token> tokens = tokenizer.tokenize();

    Parser parser(std::move(tokens));
    std::optional<NodeExit> tree = parser.parse();

    if(!tree.has_value()){
        std::cerr << "No exit statement found" << std::endl;
        exit(EXIT_FAILURE);
    }

    Generator generator(tree.value());
    {
        std::fstream file("./.process/out.asm",std::ios::out);
        file << generator.generate();
    }


    try
    {
        std::filesystem::create_directory(".process");
    }
    catch(const std::exception& e)
    {
        std::cerr << "Não foi possivel criar diretorio" << '\n';

        std::cerr << e.what() << '\n';
    }
    
    system("nasm -felf64 .process/out.asm"); 
    system("ld -o ./out ./.process/out.o");

    return EXIT_SUCCESS;
}   