%{
#include "assembler.h"
#include "parser.hpp"
#include <cstring> // Za strdup

extern FILE *yyin; // Deklaracija za yyin
int line_num = 1;
%}
%option noinput
%option nounput

%%

".global"           { return GLOBAL; }
".extern"           { return EXTERN; }
".section"          { return SECTION; }
".word"             { return WORD; }
".skip"             { return SKIP; }
".end"              { return END_DIRECTIVE; }

"halt"               { return HALT; }
"int"                { return INT; }
"iret"               { return IRET; }
"call"               { return CALL; }
"ret"                { return RET; }
"jmp"                { return JMP; }
"beq"                { return BEQ; }
"bne"                { return BNE; }
"bgt"                { return BGT; }
"push"               { return PUSH; }
"pop"                { return POP; }
"xchg"               { return XCHG; }
"add"                { return ADD; }
"sub"                { return SUB; }
"mul"                { return MUL; }
"div"                { return DIV; }
"not"                { return NOT; }
"and"                { return AND; }
"or"                 { return OR; }
"xor"                { return XOR; }
"shl"                { return SHL; }
"shr"                { return SHR; }
"ld"                 { return LD; }
"st"                 { return ST; }
"csrrd"              { return CSR_RD; }
"csrwr"              { return CSR_WR; }

[ \t]+               ; // Ignoriši whitespace
"#".*                ; // Ignoriši komentare
\n                   { ++line_num; }

[a-zA-Z_][a-zA-Z0-9_]*: {
                        size_t len = strlen(yytext);
                        char *label = strndup(yytext, len - 1);
                        yylval.sval = label;
                        return LABEL;
                      }

[0-9]+               { yylval.ival = atoi(yytext); return LITERAL; }
0[xX][0-9a-fA-F]+    { yylval.ival = (unsigned int)strtoul(yytext, nullptr, 16); return LITERAL; }
[a-zA-Z_$%\[\]_.][a-zA-Z0-9_$%\[\]_]* {
                        yylval.sval = strdup(yytext);
                        return IDENTIFIER;
                     }


[\(\)\{\}:;=,\[\]\+]   { return yytext[0]; }

.                    ; // Ignoriši sve ostalo
%%

int yywrap() {
    return 1;
}