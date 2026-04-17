#include <stdio.h>      // Standart giriş/çıkış işlemleri için
#include <stdlib.h>     // exit(), malloc() gibi sistem işlevleri için
#include <string.h>     // strcmp(), strcpy() gibi string işlemleri için
#include <ctype.h>      // Karakter kontrol fonksiyonları için (isdigit(), isalpha() vs.)

#define MAX_LEXEME 128             // Token stringlerinin maksimum uzunluğu
#define MAX_NUMBER_LENGTH 100      // Number veri tipi için maksimum 100 basamak (yeni eklendi)
#define MAX_IDENTIFIER_LENGTH 20   // Variable isimleri en fazla 20 karakter (yeni eklendi)

// Token Tipleri        
typedef enum {
    T_KEYWORD,          // Anahtar kelime (number, write vs.)
    T_IDENTIFIER,       // Değişken ismi
    T_INT_CONSTANT,     // Tam sayı sabiti
    T_STRING_CONSTANT,  // Çift tırnaklı sabit
    T_OPERATOR,         // :=, +=, -= gibi işleçler
    T_OPEN_BLOCK,       // { bloğu başlatır
    T_CLOSE_BLOCK,      // } bloğu kapatır
    T_END_OF_LINE       // ; satır sonu
} TokenType;

// Token adlarını string olarak tanımlayan dizi (çıkışta kullanılır)
static const char *TOKEN_NAMES[] = {
    "Keyword", "Identifier", "IntConstant", "StringConstant",
    "Operator", "OpenBlock", "CloseBlock", "EndOfLine"
};

// Token yapısı: her token’ın türü, içeriği (lexeme), bulunduğu satır ve sütun
typedef struct {
    TokenType type;
    char lexeme[MAX_LEXEME];
    int line, col;
} Token;

// Plus++ dilinde geçerli olan anahtar kelimeler
static const char *KEYWORDS[] = {
    "number", "repeat", "times", "write", "and", "newline", NULL
};

// Belirtilen kelime anahtar kelime mi kontrol et
static int is_keyword(const char *w) {
    for (int i = 0; KEYWORDS[i]; i++)
        if (strcmp(w, KEYWORDS[i]) == 0) return 1;
    return 0;
}

// Küresel satır/sütun konumu (hata mesajlarında vs. kullanılır)
static int g_line = 1, g_col = 1;

// Bir karakter oku ve konum sayacını güncelle
static int nextc(FILE *in) {
    int c = fgetc(in);
    if (c == '\n') { g_line++; g_col = 1; }
    else if (c != EOF) { g_col++; }
    return c;
}

// Bir karakteri sadece kontrol et (geri ver)
static int peekc(FILE *in) {
    int c = fgetc(in);
    if (c != EOF) ungetc(c, in);
    return c;
}

// Boşlukları ve *yorumları* atla
static void skip_ws_and_comments(FILE *in) {
    int c;
    while ((c = peekc(in)) != EOF) {
        if (isspace(c)) { nextc(in); continue; } // Boşluksa atla
        if (c == '*') {                          // Yorum başı
            int sl = g_line, sc = g_col;         // Hata durumunda yer bilgisi
            nextc(in);                           // İlk * karakterini al
            while ((c = nextc(in)) != EOF && c != '*') ; // Son *'i arıyoruz
            if (c == EOF) {                      // Eğer * kapanmadıysa
                fprintf(stderr, "Error [%d:%d] Unterminated comment\n", sl, sc);
                exit(EXIT_FAILURE);
            }
            nextc(in); // Kapatıcı '*' karakterini tüket
            continue;
        }
        break;
    }
}

// Bir sonraki token’ı oku ve yapı olarak döndür
static int next_token(FILE *in, Token *tk) {
    skip_ws_and_comments(in);
    int c = peekc(in);
    if (c == EOF) return 0; // Dosya sonu

    tk->line = g_line;      // Token'ın başladığı yer
    tk->col  = g_col;
    c = nextc(in);          // Karakteri al

    // Blok, satır sonu karakterleri
    if (c == '{') { tk->type = T_OPEN_BLOCK;  strcpy(tk->lexeme, "{"); return 1; }
    if (c == '}') { tk->type = T_CLOSE_BLOCK; strcpy(tk->lexeme, "}"); return 1; }
    if (c == ';') { tk->type = T_END_OF_LINE; strcpy(tk->lexeme, ";"); return 1; }

    // Operatörler (:=, +=, -=) 
    if (c == ':' || c == '+' || c == '-') {
        int next = nextc(in);
        if (next == '=') {
            tk->type = T_OPERATOR;
            tk->lexeme[0] = (char)c;
            tk->lexeme[1] = '=';
            tk->lexeme[2] = '\0';
            return 1;
        }
        ungetc(next, in);

        if (c == '-' && isdigit(peekc(in))) { // Negatif sayı kontrolü
            int len = 0;
            char buf[MAX_LEXEME];
            buf[len++] = '-';
            c = nextc(in);
            while (isdigit(c) && len < MAX_NUMBER_LENGTH + 1) {
                buf[len++] = (char)c;
                c = nextc(in);
            }
            ungetc(c, in);
            buf[len] = '\0';

            if (len - 1 > MAX_NUMBER_LENGTH) { // Sayı uzunluğu kontrolü
                fprintf(stderr, "Error [%d:%d] Number too long (> %d digits): %s\n", tk->line, tk->col, MAX_NUMBER_LENGTH, buf);
                exit(EXIT_FAILURE);
            }

            tk->type = T_INT_CONSTANT;
            strcpy(tk->lexeme, buf);
            return 1;
        } else {
            fprintf(stderr, "Error [%d:%d] Unknown character\n", tk->line, tk->col);
            exit(EXIT_FAILURE);
        }
    }

    // Tam sayı sabiti 
    if (isdigit(c)) {
        int len = 0;
        char buf[MAX_LEXEME];
        do {
            buf[len++] = (char)c;
            c = nextc(in);
        } while (isdigit(c) && len < MAX_NUMBER_LENGTH);

        ungetc(c, in);
        buf[len] = '\0';

        if (len > MAX_NUMBER_LENGTH) {
            fprintf(stderr, "Error [%d:%d] Number too long (> %d digits): %s\n", tk->line, tk->col, MAX_NUMBER_LENGTH, buf);
            exit(EXIT_FAILURE);
        }

        tk->type = T_INT_CONSTANT;
        strcpy(tk->lexeme, buf);
        return 1;
    }

    // String sabiti
    if (c == '"') {
        int len = 0;
        char buf[MAX_LEXEME];
        buf[len++] = '"';
        while ((c = nextc(in)) != EOF && c != '"' && len < MAX_LEXEME - 2)
            buf[len++] = (char)c;

        if (c == EOF) {
            fprintf(stderr, "Error [%d:%d] Unterminated string\n", tk->line, tk->col);
            exit(EXIT_FAILURE);
        }
        buf[len++] = '"';
        buf[len] = '\0';

        tk->type = T_STRING_CONSTANT;
        strcpy(tk->lexeme, buf);
        return 1;
    }

    // Identifier veya keyword 
    if (isalpha(c)) {
        int len = 0;
        char buf[MAX_LEXEME];
        buf[len++] = (char)c;
        c = nextc(in);
        while ((isalnum(c) || c == '_') && len < MAX_LEXEME - 1) {
            buf[len++] = (char)c;
            c = nextc(in);
        }
        ungetc(c, in);
        buf[len] = '\0';

        if (!is_keyword(buf) && strlen(buf) > MAX_IDENTIFIER_LENGTH) {
            fprintf(stderr, "Error [%d:%d] Identifier too long (> %d characters): %s\n", tk->line, tk->col, MAX_IDENTIFIER_LENGTH, buf);
            exit(EXIT_FAILURE);
        }

        if (!isalpha(buf[0])) { // İlk karakter harf olmalı
            fprintf(stderr, "Error [%d:%d] Identifier must begin with a letter: %s\n", tk->line, tk->col, buf);
            exit(EXIT_FAILURE);
        }

        if (is_keyword(buf))
            tk->type = T_KEYWORD;
        else
            tk->type = T_IDENTIFIER;

        strcpy(tk->lexeme, buf);
        return 1;
    }

    // Tanımsız karakter hatası
    fprintf(stderr, "Error [%d:%d] Unknown character '%c'\n", tk->line, tk->col, c);
    exit(EXIT_FAILURE);
}

// Token’ı dosyaya yaz
static void write_token(FILE *out, const Token *tk) {
    if (tk->type == T_END_OF_LINE || tk->type == T_OPEN_BLOCK || tk->type == T_CLOSE_BLOCK)
        fprintf(out, "%s\n", TOKEN_NAMES[tk->type]);
    else
        fprintf(out, "%s(%s)\n", TOKEN_NAMES[tk->type], tk->lexeme);
}

// Ana fonksiyon
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: la <scriptName>\n");
        return EXIT_FAILURE;
    }

    // Girdi ve çıktı dosya adları oluşturulur
    char in_name[256], out_name[256];
    snprintf(in_name, 256, "%s.plus", argv[1]);
    snprintf(out_name, 256, "%s.lx", argv[1]);

    FILE *in = fopen(in_name, "r");
    if (!in) { perror(in_name); return EXIT_FAILURE; }
    
    FILE *out = fopen(out_name, "w");
    if (!out) { perror(out_name); return EXIT_FAILURE; }

    // Tokenları sırayla oku ve yaz
    Token tk;
    while (next_token(in, &tk)) {
        write_token(out, &tk);
    }

    // EOF sonrası fazladan karakter kontrolü 
    int extra;
    while ((extra = fgetc(in)) != EOF) {
        if (!isspace(extra)) {
            fprintf(stderr, "Warning: Extra character after EOF at [%d:%d]: '%c'\n", g_line, g_col, extra);
            break;
        }
    }

    fclose(in);
    fclose(out);

    // Başarı lexical analysis mesajı
    printf("Lexical analysis complete. Output -> %s\n", out_name);
    return EXIT_SUCCESS;
}
