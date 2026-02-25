#pragma once

#include <string>
#include <vector>
#include <optional>

enum class TokenType
{
    exit,
    int_lit,
    semi,
    open_paren,
    close_paren
};

struct Token
{
    TokenType type;
    std::optional<std::string> value{};
};

class Tokenizer
{

public:
    explicit Tokenizer(std::string src)
        : m_src(std::move(src)),m_index(0)
    {
    }

    std::vector<Token> tokenize()
    {
        std::vector<Token> tokens;
        std::string buf;

        while (peak().has_value()){
            if (std::isalpha(peak().value())){
                buf.push_back(consume());
                while (peak().has_value() && isalnum(peak().value()))
                {
                    buf.push_back(consume());
                }

                if (buf == "exit")
                {
                    tokens.push_back({.type = TokenType::exit});
                    buf.clear();
                    continue;
                }
                else
                {
                    std::cerr << "Erro" << std::endl;
                    exit(EXIT_FAILURE);
                }
            }
            else if (std::isdigit(peak().value()))
            {
                buf.push_back(consume());
                while (peak().has_value() && std::isdigit(peak().value()))
                {
                    buf.push_back(consume());
                }
                tokens.push_back({.type = TokenType::int_lit, .value = buf});
                buf.clear();
                continue;
            }
            else if (peak().value() == '('){
                consume();
                tokens.push_back({.type = TokenType::open_paren});
            }
            else if (peak().value() == ')'){
                consume();
                tokens.push_back({.type = TokenType::close_paren});
            }
            else if (peak().value() == ';')
            {
                consume();
                tokens.push_back({.type = TokenType::semi});
                continue;
            }
            else if (isspace(peak().value()))
            {
                consume();
                continue;
            }
            else
            {
                std::cerr << "Erro" << std::endl;
                exit(EXIT_FAILURE);
            }
        }
        m_index = 0;
        return tokens;
    }

private:
    std::optional<char> peak(int ahead = 0) const
    {
        if (m_index + ahead >= m_src.length())
        {
            return {};
        }
        return m_src.at(m_index + ahead);
    }

    char consume()
    {
        return m_src.at(m_index++);
    }

    const std::string m_src;
    int m_index;
};