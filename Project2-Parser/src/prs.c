// used libraries
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

// max limits
#define MAX_VAR_NAME 21
#define MAX_VARS     100
#define MAX_LINE_LEN 256
#define MAX_DIGITS   100

// big integer struct
typedef struct {
    int digits[MAX_DIGITS + 2];
    int length;
    int negative;
} BigInt;

// enumeration for the parse tree
typedef enum {
    ND_VAR_DECL, ND_ASSIGN, ND_INC, ND_DEC,
    ND_WRITE, ND_LOOP, ND_BLOCK,
    ND_VAR_REF, ND_CONST, ND_STRING, ND_NEWLINE
} NodeType;

// Node struct
typedef struct Node {
    NodeType type;
    struct Node *next;        
    int var_index;   
    BigInt const_val;   
    char *string_val;  
    struct Node *expr;        
    struct Node *elements;    
    struct Node *count;       
    struct Node *body;      
    struct Node *statements;  
} Node;

// variable struct, symbol table
typedef struct {
    char name[MAX_VAR_NAME];
    BigInt value;
    int initialized;
} Variable;

static Variable vars[MAX_VARS];
static int var_count = 0;
static char last_line_content[MAX_LINE_LEN] = "";

// function declarations
static Node *parse_statement(char **cursor,int *line_num,FILE *file);
static Node *parse_block(int *line_num,FILE *file,char *initial);
static Node *parse_int_value(char **cursor,int line_num);
static Node *parse_output_list(char *str,int line_num);
static void execute_node(Node *node);
static void free_node(Node *node);

// error function
static void error(const char *msg, int line) {
    if (strcmp(msg, "Unrecognized statement.") == 0) {
        char trimmed[MAX_LINE_LEN];
        strcpy(trimmed, last_line_content);

        char *p = trimmed;
        while (isspace((unsigned char)*p)) ++p;

        char *e = p + strlen(p);
        while (e > p && isspace((unsigned char)*(e - 1))) --e;
        *e = '\0';

        if (strcmp(p, "}") == 0) return;
    }

    fprintf(stderr, "Error line %d: %s\n", line, msg);
    exit(1);
}

// BigInt functions
static int cmpMag(const BigInt *a, const BigInt *b) {
    if (a->length > b->length) return 1;
    if (a->length < b->length) return -1;
    for (int i = a->length - 1; i >= 0; --i) {
        if (a->digits[i] > b->digits[i]) return 1;
        if (a->digits[i] < b->digits[i]) return -1;
    }
    return 0;
}

static BigInt addMag(const BigInt *a, const BigInt *b) {
    BigInt r = { .negative = 0 };
    int carry = 0, max = a->length > b->length ? a->length : b->length, i;
    for (i = 0; i < max; ++i) {
        int sum = (i < a->length ? a->digits[i] : 0)
                + (i < b->length ? b->digits[i] : 0)
                + carry;
        r.digits[i] = sum % 10;
        carry = sum / 10;
    }
    if (carry) r.digits[i++] = carry;
    r.length = i ? i : 1;
    return r;
}

static BigInt subMag(const BigInt *a, const BigInt *b) {
    BigInt r = { .negative = 0 };
    int borrow = 0;
    for (int i = 0; i < a->length; ++i) {
        int diff = a->digits[i] - (i < b->length ? b->digits[i] : 0) - borrow;
        if (diff < 0) {
            diff += 10;
            borrow = 1;
        } else {
            borrow = 0;
        }
        r.digits[i] = diff;
    }
    int l = a->length;
    while (l > 1 && r.digits[l - 1] == 0) --l;
    r.length = l;
    return r;
}

static BigInt bigAdd(BigInt x, BigInt y) {
    BigInt r;
    if (x.negative == y.negative) {
        r = addMag(&x, &y);
        r.negative = x.negative;
    } else {
        int c = cmpMag(&x, &y);
        if (c == 0) {
            r.length = 1;
            r.digits[0] = 0;
            r.negative = 0;
        } else if (c > 0) {
            r = subMag(&x, &y);
            r.negative = x.negative;
        } else {
            r = subMag(&y, &x);
            r.negative = y.negative;
        }
    }
    if (r.length == 1 && r.digits[0] == 0) r.negative = 0;
    return r;
}

static BigInt bigSub(BigInt a, BigInt b) {
    if (b.length == 1 && b.digits[0] == 0)
        b.negative = 0;
    else
        b.negative = !b.negative;
    return bigAdd(a, b);
}

// BigInt parse
static BigInt parseBigInt(const char *s, int line) {
    BigInt r;
    char buf[512];
    strncpy(buf, s, 511);
    buf[511] = '\0';

    char *p = buf;
    while (isspace((unsigned char)*p)) ++p;

    char *e = p + strlen(p);
    while (e > p && isspace((unsigned char)*(e - 1))) --e;
    *e = '\0';

    if (!*p) error("Invalid integer constant.", line);

    int neg = 0;
    if (*p == '-') {
        neg = 1;
        ++p;
        while (isspace((unsigned char)*p)) ++p;
    }

    if (*p == '+' || !isdigit((unsigned char)*p))
        error("Invalid integer constant.", line);

    int dcnt = 0;
    for (char *q = p; *q; ++q) {
        if (!isdigit((unsigned char)*q)) error("Invalid integer constant.", line);
        if (++dcnt > MAX_DIGITS) error("Integer constant too large.", line);
    }

    int nz = 0;
    while (nz < dcnt && p[nz] == '0') ++nz;

    if (nz == dcnt) {
        r.length = 1;
        r.digits[0] = 0;
        r.negative = 0;
        return r;
    }

    r.length = dcnt - nz;
    for (int i = 0; i < r.length; ++i)
        r.digits[i] = p[dcnt - 1 - i] - '0';

    while (r.length > 1 && r.digits[r.length - 1] == 0) --r.length;

    r.negative = neg;
    if (r.length == 1 && r.digits[0] == 0) r.negative = 0;

    return r;
}

// parse int value
static Node* parse_int_value(char **cur, int line) {
    while (isspace((unsigned char)**cur)) ++*cur;
    if (!**cur) error("Invalid integer constant.", line);

    int neg = 0;
    if (**cur == '-') {
        neg = 1;
        ++*cur;
        while (isspace((unsigned char)**cur)) ++*cur;
    }

    char tok[256];
    int i = 0;
    while (**cur && (isalnum((unsigned char)**cur) || **cur == '_'))
        tok[i++] = **cur, ++*cur;
    tok[i] = '\0';

    if (!i) error("Invalid integer constant.", line);

    Node *n = calloc(1, sizeof(Node));
    if (isalpha((unsigned char)tok[0])) {
        int id = -1;
        for (int k = 0; k < var_count; ++k) {
            if (strcmp(vars[k].name, tok) == 0) {
                id = k;
                break;
            }
        }
        if (id == -1) error("Variable not declared.", line);
        n->type = ND_VAR_REF;
        n->var_index = id;
    } else {
        char nb[260];
        if (neg) {
            nb[0] = '-';
            strcpy(nb + 1, tok);
        } else {
            strcpy(nb, tok);
        }
        n->type = ND_CONST;
        n->const_val = parseBigInt(nb, line);
    }

    return n;
}

// write output list
static Node* parse_output_list(char *str, int line) {
    Node *head = NULL, *tail = NULL;
    char *p = str;

    while (1) {
        while (isspace((unsigned char)*p)) ++p;
        if (!*p) break;

        Node *el = NULL;

        if (*p == '"') {
            ++p;
            char buf[MAX_LINE_LEN];
            int bi = 0;

            while (*p && *p != '"') {
                if (bi < MAX_LINE_LEN - 1)
                    buf[bi++] = *p;
                ++p;
            }

            if (*p != '"')
                error("Invalid string literal.", line);

            buf[bi] = '\0';
            ++p;

            el = calloc(1, sizeof(Node));
            el->type = ND_STRING;
            el->string_val = strdup(buf);
        }
        else if (strncmp(p, "newline", 7) == 0 && !isalnum((unsigned char)p[7])) {
            p += 7;
            el = calloc(1, sizeof(Node));
            el->type = ND_NEWLINE;
        }
        else {
            el = parse_int_value(&p, line);
        }

        if (!head)
            head = el;
        else
            tail->next = el;

        tail = el;

        while (isspace((unsigned char)*p)) ++p;

        if (strncmp(p, "and", 3) == 0 && !isalnum((unsigned char)p[3]))
            p += 3;
        else
            break;
    }

    return head;
}

/* ───────────────────────── Parse statement───────────────────────── */
static Node* parse_statement(char **cursor, int *line_num, FILE *file) {
    while (isspace((unsigned char)**cursor)) ++*cursor;
    if (!**cursor) return NULL;
    if (**cursor == '}') { ++*cursor; return NULL; }

    /* -------- Variable declaration -------- */
    if (strncmp(*cursor, "number", 6) == 0 && !isalnum((unsigned char)(*cursor)[6])) {
        *cursor += 6;
        while (isspace((unsigned char)**cursor)) ++*cursor;

        char *sc = strchr(*cursor, ';');
        if (!sc) error("Variable declaration must end with ';'.", *line_num);

        *sc = '\0';
        char *vn = *cursor;
        char *e = vn + strlen(vn);
        while (e > vn && isspace((unsigned char)*(e - 1))) --e;
        *e = '\0';

        if (!isalpha((unsigned char)vn[0]) || strlen(vn) > 20)
            error("Invalid variable name.", *line_num);
        for (char *c = vn; *c; ++c)
            if (!isalnum((unsigned char)*c) && *c != '_')
                error("Invalid variable name.", *line_num);
        for (int i = 0; i < var_count; ++i)
            if (strcmp(vars[i].name, vn) == 0)
                error("Variable already declared.", *line_num);
        if (var_count >= MAX_VARS)
            error("Too many variables.", *line_num);

        strncpy(vars[var_count].name, vn, MAX_VAR_NAME - 1);
        vars[var_count].name[MAX_VAR_NAME - 1] = '\0';
        vars[var_count].value.length = 1;
        vars[var_count].value.digits[0] = 0;
        vars[var_count].value.negative = 0;
        vars[var_count].initialized = 1;

        int id = var_count++;
        *cursor = sc + 1;

        Node *n = calloc(1, sizeof(Node));
        n->type = ND_VAR_DECL;
        n->var_index = id;
        return n;
    }

    /* -------- repeat ... times loop -------- */
    if (strncmp(*cursor, "repeat", 6) == 0 && !isalnum((unsigned char)(*cursor)[6])) {
        *cursor += 6;
        while (isspace((unsigned char)**cursor)) ++*cursor;

        char *kw = strstr(*cursor, "times");
        if (!kw || (kw != *cursor && *(kw - 1) != ' '))
            error("Bad repeat syntax.", *line_num);

        *kw = '\0';
        char *cnt_str = *cursor;
        char *endcnt = kw;
        while (endcnt > cnt_str && isspace((unsigned char)*(endcnt - 1))) --endcnt;
        *endcnt = '\0';

        kw += 5;
        while (isspace((unsigned char)*kw)) ++kw;

        Node *cnt_node;
        int vid = -1;
        for (int i = 0; i < var_count; ++i)
            if (strcmp(vars[i].name, cnt_str) == 0) {
                vid = i;
                break;
            }

        if (vid != -1) {
            cnt_node = calloc(1, sizeof(Node));
            cnt_node->type = ND_VAR_REF;
            cnt_node->var_index = vid;
        } else {
            cnt_node = calloc(1, sizeof(Node));
            cnt_node->type = ND_CONST;
            cnt_node->const_val = parseBigInt(cnt_str, *line_num);

            if (cnt_node->const_val.negative)
                error("Loop count negative.", *line_num);
            if (cnt_node->const_val.length > 19)
                error("Loop count too large.", *line_num);
            if (cnt_node->const_val.length == 19) {
                char buf[24];
                int j = 0;
                for (int i = 18; i >= 0; --i)
                    buf[j++] = '0' + cnt_node->const_val.digits[i];
                buf[j] = '\0';
                if (strcmp(buf, "9223372036854775807") > 0)
                    error("Loop count too large.", *line_num);
            }
        }

        Node *loop = calloc(1, sizeof(Node));
        loop->type = ND_LOOP;
        loop->count = cnt_node;

        if (*kw == '\0') {  /* body starts next line */
            char nxt[MAX_LINE_LEN];
            if (!fgets(nxt, sizeof(nxt), file))
                error("Bad repeat syntax.", *line_num);
            (*line_num)++;
            nxt[strcspn(nxt, "\n")] = '\0';

            char *bl = nxt;
            while (isspace((unsigned char)*bl)) ++bl;
            if (*bl != '{') error("Bad repeat syntax.", *line_num);
            ++bl;
            while (isspace((unsigned char)*bl)) ++bl;
            char *end = bl + strlen(bl);
            while (end > bl && isspace((unsigned char)*(end - 1))) --end;
            *end = '\0';

            loop->body = parse_block(line_num, file, bl);
            *cursor += strlen(*cursor);
        } else if (*kw == '{') {  /* body starts same line */
            ++kw;
            while (isspace((unsigned char)*kw)) ++kw;
            char *end = kw + strlen(kw);
            while (end > kw && isspace((unsigned char)*(end - 1))) --end;
            *end = '\0';

            loop->body = parse_block(line_num, file, kw);
            *cursor += strlen(*cursor);
        } else {  /* single-line loop */
            char *semi = strchr(kw, ';');
            if (!semi) error("Repeat single-line body must end with ';'.", *line_num);
            size_t len = semi - kw + 1;
            char body[MAX_LINE_LEN];
            strncpy(body, kw, len);
            body[len] = '\0';

            *cursor += (kw - *cursor) + len;
            char *bp = body;
            loop->body = parse_statement(&bp, line_num, file);
        }

        return loop;
    }

    /* -------- write -------- */
    if (strncmp(*cursor, "write", 5) == 0 && !isalnum((unsigned char)(*cursor)[5])) {
        *cursor += 5;
        while (isspace((unsigned char)**cursor)) ++*cursor;

        int n = strlen(*cursor);
        if (n == 0 || (*cursor)[n - 1] != ';')
            error("Write must end with ';'.", *line_num);

        (*cursor)[n - 1] = '\0';
        char *out = *cursor;
        char *e = out + strlen(out);
        while (e > out && isspace((unsigned char)*(e - 1))) --e;
        *e = '\0';

        *cursor += n;

        Node *w = calloc(1, sizeof(Node));
        w->type = ND_WRITE;
        w->elements = parse_output_list(out, *line_num);
        return w;
    }

    /* -------- assignment / inc / dec -------- */
    if (!isalpha((unsigned char)**cursor))
        error("Unrecognized statement.", *line_num);

    char lhs[MAX_VAR_NAME];
    int li = 0;
    while (**cursor && (isalnum((unsigned char)**cursor) || **cursor == '_')) {
        if (li < MAX_VAR_NAME - 1)
            lhs[li++] = **cursor;
        ++*cursor;
    }
    lhs[li] = '\0';

    while (isspace((unsigned char)**cursor)) ++*cursor;

    int op = -1;
    if ((*cursor)[0] == ':' && (*cursor)[1] == '=') op = 0;
    else if ((*cursor)[0] == '+' && (*cursor)[1] == '=') op = 1;
    else if ((*cursor)[0] == '-' && (*cursor)[1] == '=') op = 2;
    if (op == -1) error("Unrecognized statement.", *line_num);

    *cursor += 2;
    while (isspace((unsigned char)**cursor)) ++*cursor;

    int id = -1;
    for (int i = 0; i < var_count; ++i)
        if (strcmp(vars[i].name, lhs) == 0) {
            id = i;
            break;
        }
    if (id == -1) error("Variable not declared.", *line_num);

    Node *rhs = parse_int_value(cursor, *line_num);
    while (isspace((unsigned char)**cursor)) ++*cursor;
    if (**cursor != ';') error("Missing ';'.", *line_num);
    ++*cursor;

    Node *n = calloc(1, sizeof(Node));
    n->type = (op == 0 ? ND_ASSIGN : (op == 1 ? ND_INC : ND_DEC));
    n->var_index = id;
    n->expr = rhs;
    vars[id].initialized = 1;

    return n;
}

/* ───────────────────────── Parse block ───────────────────────── */
static Node* parse_block(int *line_num, FILE *file, char *initial) {
    Node *blk = calloc(1, sizeof(Node));
    blk->type = ND_BLOCK;
    Node *tail = NULL;

    if (initial && *initial) {
        char *p = initial;
        while (1) {
            while (isspace((unsigned char)*p)) ++p;
            if (!*p) break;

            if (*p == '}') {
                ++p;
                while (isspace((unsigned char)*p)) ++p;
                if (*p) strncpy(last_line_content, p, MAX_LINE_LEN - 1);
                return blk;
            }

            Node *st = parse_statement(&p, line_num, file);
            if (!st) break;

            if (!blk->statements) blk->statements = st;
            else tail->next = st;

            tail = st;
            while (tail->next) tail = tail->next;

            while (isspace((unsigned char)*p)) ++p;
            if (!*p) break;

            if (*p == '}') {
                ++p;
                while (isspace((unsigned char)*p)) ++p;
                if (*p) strncpy(last_line_content, p, MAX_LINE_LEN - 1);
                return blk;
            }
        }
    }

    char buf[MAX_LINE_LEN];
    while (fgets(buf, sizeof(buf), file)) {
        (*line_num)++;
        buf[strcspn(buf, "\n")] = '\0';

        char *ln = buf;
        while (isspace((unsigned char)*ln)) ++ln;

        char *e = ln + strlen(ln);
        while (e > ln && isspace((unsigned char)*(e - 1))) --e;
        *e = '\0';

        if (!*ln) continue;

        if (*ln == '}') {
            ++ln;
            while (isspace((unsigned char)*ln)) ++ln;
            if (*ln) strncpy(last_line_content, ln, MAX_LINE_LEN - 1);
            return blk;
        }

        char *p = ln;
        while (1) {
            while (isspace((unsigned char)*p)) ++p;
            if (!*p) break;

            if (*p == '}') {
                ++p;
                while (isspace((unsigned char)*p)) ++p;
                if (*p) strncpy(last_line_content, p, MAX_LINE_LEN - 1);
                return blk;
            }

            Node *st = parse_statement(&p, line_num, file);
            if (!st) break;

            if (!blk->statements) blk->statements = st;
            else tail->next = st;

            tail = st;
            while (tail->next) tail = tail->next;

            if (st->type == ND_LOOP) {
                while (*p && *p != '\0') ++p;
                break; // consume rest if loop
            }

            while (isspace((unsigned char)*p)) ++p;
            if (!*p) break;

            if (*p == '}') {
                ++p;
                while (isspace((unsigned char)*p)) ++p;
                if (*p) strncpy(last_line_content, p, MAX_LINE_LEN - 1);
                return blk;
            }
        }
    }

    error("Missing closing brace for repeat block.", *line_num);
    return NULL;
}

// BigInt print
static void printBig(const BigInt *v) {
    if (v->negative) putchar('-');
    for (int i = v->length - 1; i >= 0; --i)
        printf("%d", v->digits[i]);
}

static void execute_node(Node *n) {
    if (!n) return;
    switch (n->type) {
        case ND_BLOCK:
            for (Node *s = n->statements; s; s = s->next)
                execute_node(s);
            break;
        case ND_VAR_DECL:
            break;
        case ND_ASSIGN: {
            BigInt v = (n->expr->type == ND_VAR_REF) ? vars[n->expr->var_index].value : n->expr->const_val;
            vars[n->var_index].value = v;
            break;
        }
        case ND_INC: {
            BigInt v = (n->expr->type == ND_VAR_REF) ? vars[n->expr->var_index].value : n->expr->const_val;
            vars[n->var_index].value = bigAdd(vars[n->var_index].value, v);
            break;
        }
        case ND_DEC: {
            BigInt v = (n->expr->type == ND_VAR_REF) ? vars[n->expr->var_index].value : n->expr->const_val;
            vars[n->var_index].value = bigSub(vars[n->var_index].value, v);
            break;
        }
        case ND_WRITE: {
            for (Node *e = n->elements; e; e = e->next) {
                if (e->type == ND_STRING)
                    printf("%s", e->string_val);
                else if (e->type == ND_NEWLINE)
                    putchar('\n');
                else if (e->type == ND_VAR_REF)
                    printBig(&vars[e->var_index].value);
                else if (e->type == ND_CONST)
                    printBig(&e->const_val);
            }
            break;
        }
        case ND_LOOP: {
            BigInt cnt = (n->count->type == ND_VAR_REF) ? vars[n->count->var_index].value : n->count->const_val;
            long long c = 0;
            for (int i = cnt.length - 1; i >= 0; --i)
                c = c * 10 + cnt.digits[i];

            int lvid = (n->count->type == ND_VAR_REF) ? n->count->var_index : -1;

            for (long long cur = c; cur > 0; --cur) {
                if (lvid != -1) {
                    long long t = cur;
                    BigInt b = {.length = 0, .negative = 0};
                    if (t == 0) {
                        b.length = 1;
                        b.digits[0] = 0;
                    }
                    while (t) {
                        b.digits[b.length++] = t % 10;
                        t /= 10;
                    }
                    vars[lvid].value = b;
                }

                if (n->body->type == ND_BLOCK) {
                    for (Node *s = n->body->statements; s; s = s->next)
                        execute_node(s);
                } else {
                    execute_node(n->body);
                }
            }

            if (lvid != -1) {
                vars[lvid].value.length = 1;
                vars[lvid].value.digits[0] = 0;
                vars[lvid].value.negative = 0;
            }

            break;
        }

        default:
            break;
    }
}

/* ───────────────────────── Free tree ───────────────────────── */
static void free_node(Node *n) {
    if (!n) return;

    if (n->type == ND_BLOCK) {
        Node *s = n->statements, *nx;
        while (s) {
            nx = s->next;
            free_node(s);
            s = nx;
        }
    }
    else if (n->type == ND_WRITE) {
        Node *e = n->elements, *nx;
        while (e) {
            nx = e->next;
            free_node(e);
            e = nx;
        }
    }
    else if (n->type == ND_LOOP) {
        free_node(n->count);
        free_node(n->body);
    }
    else if (n->type == ND_ASSIGN || n->type == ND_INC || n->type == ND_DEC) {
        free_node(n->expr);
    }
    else if (n->type == ND_STRING) {
        free(n->string_val);
    }

    free(n);
}

// main including comment 
int main(int argc, char **argv) {
    char fname[128];
    if (argc >= 2) {
        strncpy(fname, argv[1], 127);
        fname[127] = '\0';
    }
    else {
        printf("Source file (without .ppp): ");
        if (scanf("%127s", fname) != 1) return 1;
    }
    strcat(fname, ".ppp");
    FILE *fp = fopen(fname, "r");
    if (!fp) {
        perror(fname);
        return 1;
    }

    Node *program = calloc(1, sizeof(Node));
    program->type = ND_BLOCK;
    Node *tail = NULL;

    char line[MAX_LINE_LEN];
    int ln = 0, in_comment = 0;

    while (fgets(line, sizeof(line), fp)) {
        ln++;
        line[strcspn(line, "\n")] = '\0';

        /* strip *...* comments (may span lines) */
        char res[MAX_LINE_LEN] = "";
        char *src = line, *dst = res;

        while (*src) {
            if (!in_comment) {
                if (*src == '*') {
                    in_comment = 1;
                    ++src;
                    continue;
                }
                else {
                    *dst++ = *src++;
                }
            }
            else {
                char *end = strchr(src, '*');
                if (end) {
                    src = end + 1;
                    in_comment = 0;
                }
                else {
                    src += strlen(src);
                }
            }
        }
        *dst = '\0';
        strcpy(line, res);
        if (in_comment) continue;

        char *lnptr = line;
        while (isspace((unsigned char)*lnptr)) ++lnptr;

        char *end = lnptr + strlen(lnptr);
        while (end > lnptr && isspace((unsigned char)*(end - 1))) --end;
        *end = '\0';

        if (!*lnptr) continue;
        if (*lnptr == '}') continue;

        strncpy(last_line_content, lnptr, MAX_LINE_LEN - 1);

        char *p = lnptr;
        while (1) {
            while (isspace((unsigned char)*p)) ++p;
            if (!*p) break;

            Node *st = parse_statement(&p, &ln, fp);
            if (!st) break;

            if (!program->statements)
                program->statements = st;
            else
                tail->next = st;

            tail = st;
            while (tail->next) tail = tail->next;

            if (st->type == ND_LOOP) {
                while (*p && *p != '\0') ++p;
                break; /* skip rest of line */
            }

            while (isspace((unsigned char)*p)) ++p;
            if (!*p) break;
        }
    }

    fclose(fp);

    execute_node(program);
    free_node(program);

    return 0;
}