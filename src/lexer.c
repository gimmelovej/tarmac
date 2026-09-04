// ================================================================================================
// File: lexer.c
// Author: Gimmelovej
// Created in: 2026
// ================================================================================================
#include "lexer.h"

void tarm_lexer_init(Lexer *lx, const char *source, size_t len, Diagnostics *diag) {
    lx->src      = source;
    lx->len      = len;
    lx->pos      = 0;
    lx->start    = 0;
    lx->line     = 1;
    lx->col      = 1;
    lx->tok_line = 1;
    lx->tok_col  = 1;
    lx->diag     = diag;
}

// Crescimento por dobra de capacidade: o número de tokens só é conhecido no fim da varredura, e
// dobrar mantém o custo total de realocação linear no total de tokens.
//
// A falha de `realloc` é tratada aqui e não abaixo, no laço, porque `realloc` devolvendo NULL não
// libera o bloco antigo — devolver `false` com `list->data` intacto deixa a lista consistente para
// quem for liberá-la.
static bool tokens_push(TokenList *list, Token tok) {
    if (list->count == list->capacity) {
        size_t new_capacity = (list->capacity == 0) ? 4 : list->capacity * 2;
        Token *buffer       = realloc(list->data, new_capacity * sizeof *list->data);

        if (buffer == NULL) {
            tarm_system_error("Não foi possível realocar a memoria vetorial");
            return false;
        }
        list->capacity = new_capacity;
        list->data     = buffer;
    }

    list->data[list->count++] = tok;
    return true;
}

// ------------------------------------------------------------------------------------------------
// Cursor
// ------------------------------------------------------------------------------------------------

static bool isAtEnd(const Lexer *lx) {
    return lx->pos >= lx->len;
}
static char peek(const Lexer *lx) {
    return (lx->pos < lx->len) ? lx->src[lx->pos] : '\0';
}
static char next(const Lexer *lx) {
    return ((lx->pos + 1) < lx->len) ? lx->src[lx->pos + 1] : '\0';
}

// Único ponto que avança o cursor, e por isso o único que mexe em linha/coluna — qualquer scanner
// que consumisse bytes por conta própria faria a posição dos erros derivar.
static char consume(Lexer *lx) {
    char c = peek(lx);
    if (c == '\n') {
        lx->line++;
        lx->col = 1;
    } else
        lx->col++;
    lx->pos++;
    return c;
}

// ------------------------------------------------------------------------------------------------
// Trivialidade
// ------------------------------------------------------------------------------------------------

static bool is_line_comment(const Lexer *lx) {
    return peek(lx) == '/' && next(lx) == '/';
}

// Atravessa tudo que não vira token, antes de cada scanner: espaço em branco e comentário de linha.
// O `//` é consumido até a quebra de linha, que fica para a volta do laço tratar como espaço em
// branco — assim a contagem de linha/coluna continua a cargo de `consume`.
static void skipTrivia(Lexer *lx) {
    while (!isAtEnd(lx)) {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            consume(lx);
        else if (is_line_comment(lx)) {
            while (!isAtEnd(lx) && peek(lx) != '\n')
                consume(lx);
        } else
            break;
    }
}

// ------------------------------------------------------------------------------------------------
// Palavras-chave
// ------------------------------------------------------------------------------------------------

// Compara a fatia `[text, text+len)` com uma palavra terminada em '\0'. O lexema não é terminado
// em '\0' (é uma fatia do buffer de origem), então `strcmp` não serve: o comprimento entra na
// comparação.
static bool slice_eq(const char *text, size_t len, const char *word) {
    size_t n = strlen(word);
    return len == n && memcmp(text, word, n) == 0;
}

// Busca linear sobre uma tabela pequena e fixa: com uma dúzia de palavras-chave, uma tabela hash
// custaria mais em código e em cache do que economiza em comparações. Palavra desconhecida é, por
// definição, um `Identifier`.
static TokenKind check_keyword(const char *text, size_t len) {
    static const struct {
        const char *word;
        TokenKind   kind;
    } kws[] = {
        // `true`/`false` são palavras-chave, não identificadores: reconhecê-las aqui poupa o
        // Parser de comparar texto para descobrir que um identificador era um literal booleano.
        {"buffer", KwBuffer}, {"char", KwChar},   {"int", KwInt},       {"float", KwFloat},
        {"bool", KwBool},     {"int64", KwInt64}, {"string", KwString}, {"if", KwIf},
        {"elsif", KwElsif},   {"while", KwWhile}, {"else", KwElse},     {"return", KwReturn},
        {"true", KwTrue},     {"false", KwFalse},
    };

    for (size_t i = 0; i < sizeof kws / sizeof *kws; i++)
        if (slice_eq(text, len, kws[i].word)) return kws[i].kind;

    return Identifier;
}

// ------------------------------------------------------------------------------------------------
// Construção de token
// ------------------------------------------------------------------------------------------------

// Fecha o token com a fatia `[start, pos)` do buffer de origem — sem copiar texto (ver `Token`,
// em types.h). Linha e coluna vêm do par congelado em `qualify_next_token`, não do cursor: é o
// início do lexema que interessa numa mensagem de erro.
static Token make_token(const Lexer *lx, TokenKind tokKind) {
    Token t;
    t.kind  = tokKind;
    t.start = lx->src + lx->start;
    t.len   = (uint32_t)(lx->pos - lx->start);
    t.line  = lx->tok_line;
    t.col   = lx->tok_col;
    return t;
}

// Fecha o token com uma fatia explícita, para os casos em que o lexema não é `[start, pos)`: um
// literal de string guarda só o conteúdo, sem as aspas que o delimitam.
static Token make_token_slice(const Lexer *lx, TokenKind tokKind, size_t from, size_t to) {
    Token t;
    t.kind  = tokKind;
    t.start = lx->src + from;
    t.len   = (uint32_t)(to - from);
    t.line  = lx->tok_line;
    t.col   = lx->tok_col;
    return t;
}

// ------------------------------------------------------------------------------------------------
// Scanners
// ------------------------------------------------------------------------------------------------

// Pontuação e operadores. Os de dois caracteres (`==`, `>=`, `<=` e os compostos `+=`, `-=`,
// `*=`, `/=`) são resolvidos olhando o caractere seguinte antes de fechar o token de um só.
static Token scan_symbol(Lexer *lx) {
    switch (consume(lx)) {
        case '.': return make_token(lx, Dot);
        case ',': return make_token(lx, Comma);
        case ';': return make_token(lx, Semicolon);
        case '(': return make_token(lx, LParen);
        case ')': return make_token(lx, RParen);
        case '{': return make_token(lx, LBrace);
        case '}': return make_token(lx, RBrace);
        case '+':
            if (peek(lx) == '=') {
                consume(lx);
                return make_token(lx, PlusEqual);
            }
            return make_token(lx, Plus);
        case '-':
            if (peek(lx) == '=') {
                consume(lx);
                return make_token(lx, MinusEqual);
            }
            return make_token(lx, Minus);
        case '*':
            if (peek(lx) == '=') {
                consume(lx);
                return make_token(lx, StarEqual);
            }
            return make_token(lx, Star);
        case '/':
            if (peek(lx) == '=') {
                consume(lx);
                return make_token(lx, SlashEqual);
            }
            return make_token(lx, Slash);
        case '[': return make_token(lx, LBracket);
        case ']': return make_token(lx, RBracket);

        case '=':
            if (peek(lx) == '=') {
                consume(lx);
                return make_token(lx, EqualEqual);
            }
            return make_token(lx, Equal);

        case '>':
            if (peek(lx) == '=') {
                consume(lx);
                return make_token(lx, GreaterEqual);
            }
            return make_token(lx, Greater);

        case '<':
            if (peek(lx) == '=') {
                consume(lx);
                return make_token(lx, LessEqual);
            }
            return make_token(lx, Less);

        default:
            // O erro é acumulado e a varredura continua: o token `Invalid` mantém a lista alinhada
            // com o texto de origem, em vez de abortar no primeiro caractere estranho.
            tarm_error_at(lx->diag, lx->tok_line, lx->tok_col, "caractere não reconhecido: '%.*s'",
                          (int)(lx->pos - lx->start), lx->src + lx->start);
            return make_token(lx, Invalid);
    }
}

// Identificador ou palavra-chave: começa por letra ou `_`, segue com letras, dígitos e `_`.
static Token scan_identifier(Lexer *lx) {
    while (!isAtEnd(lx) && (isalnum((unsigned char)peek(lx)) || peek(lx) == '_'))
        consume(lx);

    const char *text = lx->src + lx->start;
    size_t      len  = lx->pos - lx->start;

    return make_token(lx, check_keyword(text, len));
}

// Literal numérico. O ponto só entra no lexema se houver dígito depois dele — assim `1.f()` (se um
// dia houver método sobre número) não engole o `.` que pertence ao operador de acesso.
static Token scan_number(Lexer *lx) {
    while (!isAtEnd(lx) && isdigit((unsigned char)peek(lx)))
        consume(lx);

    if (peek(lx) == '.' && isdigit((unsigned char)next(lx))) {
        consume(lx); // o ponto
        while (!isAtEnd(lx) && isdigit((unsigned char)peek(lx)))
            consume(lx);

        return make_token(lx, LiteralFloat);
    }

    return make_token(lx, LiteralInteger);
}

// Sequências de escape aceitas. A decodificação **não** acontece aqui: o token guarda a fatia crua
// (com a barra), e quem materializar o valor decide o formato. Isso mantém o Lexer sem alocação
// nenhuma — um valor decodificado não existe no buffer de origem e precisaria de armazenamento
// próprio.
static bool is_valid_escape(char c) {
    switch (c) {
        case 'n':
        case 't':
        case 'r':
        case '0':
        case '\\':
        case '"':
        case '\'': return true;
        default:   return false;
    }
}

// Literal de string. O token guarda apenas o **conteúdo**, sem as aspas: quem lê o lexema quer o
// texto, não os delimitadores.
static Token scan_string(Lexer *lx) {
    consume(lx); // aspa de abertura
    size_t content_start = lx->pos;

    while (!isAtEnd(lx) && peek(lx) != '"') {
        if (peek(lx) == '\n') break; // string não fecha atravessando linha; o erro sai abaixo

        if (peek(lx) == '\\') {
            consume(lx);
            char e = peek(lx);
            if (!is_valid_escape(e))
                tarm_error_at(lx->diag, lx->line, lx->col, "sequência de escape inválida: '\\%c'",
                              e);
            consume(lx);
            continue;
        }
        consume(lx);
    }

    if (isAtEnd(lx) || peek(lx) != '"') {
        tarm_error_at(lx->diag, lx->tok_line, lx->tok_col,
                      "literal de string não terminado (faltou fechar com '\"')");
        return make_token_slice(lx, Invalid, content_start, lx->pos);
    }

    size_t content_end = lx->pos;
    consume(lx); // aspa de fechamento

    return make_token_slice(lx, LiteralString, content_start, content_end);
}

// Literal de caractere: exatamente um caractere, ou uma sequência de escape, entre aspas simples.
static Token scan_char(Lexer *lx) {
    consume(lx); // aspa de abertura
    size_t content_start = lx->pos;

    if (isAtEnd(lx)) {
        tarm_error_at(lx->diag, lx->tok_line, lx->tok_col,
                      "literal de caractere não terminado (faltou fechar com \"'\")");
        return make_token_slice(lx, Invalid, content_start, lx->pos);
    }

    if (peek(lx) == '\\') {
        consume(lx);
        char e = peek(lx);
        if (!is_valid_escape(e))
            tarm_error_at(lx->diag, lx->line, lx->col, "sequência de escape inválida: '\\%c'", e);
        consume(lx);
    } else
        consume(lx);

    size_t content_end = lx->pos;

    if (peek(lx) != '\'') {
        tarm_error_at(lx->diag, lx->tok_line, lx->tok_col,
                      "literal de caractere deve conter exatamente um caractere "
                      "entre aspas simples");

        // Anda até o fechamento (ou o fim da linha) para que o próximo token comece num ponto
        // consistente, em vez de reprocessar o miolo do literal como símbolos soltos.
        while (!isAtEnd(lx) && peek(lx) != '\'' && peek(lx) != '\n')
            consume(lx);
        if (peek(lx) == '\'') consume(lx);

        return make_token_slice(lx, Invalid, content_start, content_end);
    }

    consume(lx); // aspa de fechamento
    return make_token_slice(lx, LiteralChar, content_start, content_end);
}

// ------------------------------------------------------------------------------------------------
// Despacho
// ------------------------------------------------------------------------------------------------

// Reconhece o próximo token. A trivialidade é pulada *antes* de marcar o início do lexema, de modo
// que `start`/`tok_line`/`tok_col` apontem para o primeiro caractere que de fato pertence ao token
// — é o que faz a mensagem de erro cair no lugar certo.
static Token qualify_next_token(Lexer *lx) {
    skipTrivia(lx);

    lx->start    = lx->pos;
    lx->tok_col  = lx->col;
    lx->tok_line = lx->line;

    if (isAtEnd(lx)) return make_token(lx, EndOfFile);

    char c = peek(lx);

    // Letra e dígito são testados separadamente: `isalnum` cobre os dois, e usá-lo no primeiro
    // ramo faria todo número entrar pelo caminho do identificador.
    if (isalpha((unsigned char)c) || c == '_') return scan_identifier(lx);
    if (isdigit((unsigned char)c)) return scan_number(lx);
    if (c == '"') return scan_string(lx);
    if (c == '\'') return scan_char(lx);

    return scan_symbol(lx);
}

void tarm_lexer_tokens_free(TokenList *list) {
    free(list->data);
    list->data     = NULL;
    list->count    = 0;
    list->capacity = 0;
}

// A lista é montada numa variável local e só é publicada em `*out` no sucesso: se a alocação
// falhar no meio, o que já havia é liberado aqui e o chamador não recebe uma lista pela metade.
//
// O valor de retorno reflete o `Diagnostics`, não o fim da varredura: chegar ao `EndOfFile` com
// tokens `Invalid` pelo caminho é uma varredura completa e malsucedida.
bool tarm_lexer_tokenize(Lexer *lx, TokenList *out) {
    TokenList tokens = {0};

    for (;;) {
        Token tok = qualify_next_token(lx);

        if (!tokens_push(&tokens, tok)) {
            tarm_lexer_tokens_free(&tokens);
            tarm_error(lx->diag, "não foi possível executar a realocação da lista.");
            return false;
        }

        if (tok.kind == EndOfFile) break;
    }

    *out = tokens;
    return !tarm_diag_has_errors(lx->diag);
}

// ------------------------------------------------------------------------------------------------
// Depuração
// ------------------------------------------------------------------------------------------------

const char *tarm_token_kind_name(TokenKind kind) {
    switch (kind) {
        case LiteralInteger: return "LiteralInteger";
        case LiteralFloat:   return "LiteralFloat";
        case LiteralString:  return "LiteralString";
        case LiteralChar:    return "LiteralChar";
        case Identifier:     return "Identifier";
        case KwReturn:       return "KwReturn";
        case KwBuffer:       return "KwBuffer";
        case KwChar:         return "KwChar";
        case KwInt:          return "KwInt";
        case KwInt64:        return "KwInt64";
        case KwFloat:        return "KwFloat";
        case KwBool:         return "KwBool";
        case KwString:       return "KwString";
        case KwIf:           return "KwIf";
        case KwElsif:        return "KwElsif";
        case KwElse:         return "KwElse";
        case KwWhile:        return "KwWhile";
        case KwDeclaration:  return "KwDeclaration";
        case KwTrue:         return "KwTrue";
        case KwFalse:        return "KwFalse";
        case Comma:          return "Comma";
        case Plus:           return "Plus";
        case PlusEqual:      return "PlusEqual";
        case Minus:          return "Minus";
        case MinusEqual:     return "MinusEqual";
        case Star:           return "Star";
        case StarEqual:      return "StarEqual";
        case Slash:          return "Slash";
        case SlashEqual:     return "SlashEqual";
        case Equal:          return "Equal";
        case EqualEqual:     return "EqualEqual";
        case LParen:         return "LParen";
        case RParen:         return "RParen";
        case LBrace:         return "LBrace";
        case RBrace:         return "RBrace";
        case Semicolon:      return "Semicolon";
        case EndOfFile:      return "EndOfFile";
        case Invalid:        return "Invalid";
        case LBracket:       return "LBracket";
        case RBracket:       return "RBracket";
        case Greater:        return "Greater";
        case Less:           return "Less";
        case GreaterEqual:   return "GreaterEqual";
        case LessEqual:      return "LessEqual";
        case Dot:            return "Dot";
    }
    return "?";
}

void tarm_lexer_dump_tokens(FILE *out, const TokenList *list) {
    for (size_t i = 0; i < list->count; i++) {
        Token t = list->data[i];
        fprintf(out, "%3zu  %-16s %2u:%-3u '%.*s'\n", i, tarm_token_kind_name(t.kind), t.line,
                t.col, (int)t.len, t.start);
    }
}
