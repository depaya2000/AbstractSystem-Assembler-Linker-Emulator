%{
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <cstring> // Za strdup
#include "assembler.h" 

using namespace std;

extern int yylex();
extern int yyparse();
extern FILE *yyin;
extern int line_num;

void yyerror(const char *s); // Deklaracija funkcije yyerror

%}

%union {
    int ival;
    char *sval;
    Initializer* initializer;
    std::vector<char*>* symbolList;
    std::vector<Initializer>* initializerList;
}

%token <ival> LITERAL
%token <sval> IDENTIFIER
%token <sval> LABEL
%token GLOBAL EXTERN SECTION WORD SKIP END_DIRECTIVE
%token HALT INT IRET CALL RET JMP BEQ BNE BGT PUSH POP XCHG ADD SUB MUL DIV NOT AND OR XOR SHL SHR LD ST CSR_RD CSR_WR
%token ':' ',' '+' 

%type <sval> register
%type <sval> label_definition
%type <sval> instruction
%type <sval> operand
%type <sval> symbol
%type <initializer> initializer
%type <symbolList> symbol_list
%type <initializerList> initializer_list

%%

assembly_file:
    lines
    ;

lines:
    line
    | lines line
    ;

line:
    /* empty */
    | label_definition
    | instruction
    | directive
    ;


instruction:
    HALT {
        handleHaltInstruction();
    }
    | INT {
        handleIntInstruction();
    }
    | IRET {
        handleIretInstruction();
    }
    | CALL operand {
        handleCallInstruction($2);
        free($2); // Oslobađanje memorije
    }
    | RET {
        handleRetInstruction();
    }
    | JMP operand {
        handleJumpInstruction($2);
        free($2); // Oslobađanje memorije
    }
    | BEQ register ',' register ',' operand {
        handleBranchInstruction($2, $4, $6, "beq");
        free($2); 
        free($4); 
        free($6);
    }
    | BNE register ',' register ',' operand {
        handleBranchInstruction($2, $4, $6, "bne");
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
        free($6); // Oslobađanje memorije
    }
    | BGT register ',' register ',' operand {
        handleBranchInstruction($2, $4, $6, "bgt");
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
        free($6); // Oslobađanje memorije
    }
    | PUSH register {
        handlePushInstruction($2);
        free($2); // Oslobađanje memorije
    }
    | POP register {
        handlePopInstruction($2);
        free($2); // Oslobađanje memorije
    }
    | XCHG register ',' register {
        handleXchgInstruction($2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | ADD register ',' register {
        addArithmeticInstruction("add", $2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | SUB register ',' register {
        addArithmeticInstruction("sub", $2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | MUL register ',' register {
        addArithmeticInstruction("mul", $2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | DIV register ',' register {
        addArithmeticInstruction("div", $2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | NOT register {
        addLogicalInstruction("not", $2, nullptr); 
        free($2); // Oslobađanje memorije
    }
    | AND register ',' register {
        addLogicalInstruction("and", $2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | OR register ',' register {
        addLogicalInstruction("or", $2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | XOR register ',' register {
        addLogicalInstruction("xor", $2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | SHL register ',' register {
        addShiftInstruction("shl", $2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | SHR register ',' register {
        addShiftInstruction("shr", $2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | LD operand ',' register {
        handleLoadInstruction($2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | ST register ',' operand {
        handleStoreInstruction($2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | CSR_RD register ',' register {
        handleCsrrdInstruction($2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    | CSR_WR register ',' register {
        handleCsrwrInstruction($2, $4);
        free($2); // Oslobađanje memorije
        free($4); // Oslobađanje memorije
    }
    ;

directive:
    GLOBAL symbol_list {
        for (auto symbol : *$2) {
            handleGlobalDirective(symbol); 
            //free((void*)symbol); // Oslobađanje memorije nakon obrade
        }
        //delete $2; // Oslobađanje memorije za listu simbola
    }
    | EXTERN symbol_list {
        for (auto symbol : *$2) {
            handleExternDirective(symbol); 
            //free((void*)symbol); // Oslobađanje memorije nakon obrade
        }
       // delete $2; // Oslobađanje memorije za listu simbola
    }
    | SECTION IDENTIFIER {
        handleSectionDirective($2);
        //free($2); // Oslobađanje memorije ako je $2 dinamički alociran
    }
    |  WORD initializer_list {
        for (auto init : *$2) {
            if (init.symbol != nullptr) {
                handleWordDirectiveWithSymbol(init.symbol);
                free(init.symbol);
            } else {
                handleWordDirectiveWithLiteral(init.literal);
            }
        }
        //delete $2;
    }
    | SKIP LITERAL {
        handleSkipDirective($2);
    }
    | END_DIRECTIVE {
        handleEndDirective();
    }
    ;

label_definition:
    LABEL {
        // Dodaj labelu u tabelu simbola
        addSymbol($1, true, 0);
        free($1); // Oslobađanje memorije
    }
    ;

symbol_list:
    /* Pravilo za inicijalizaciju liste */
    symbol {
        $<symbolList>$ = new std::vector<char*>(); // Kreiramo novu listu
        $<symbolList>$->push_back($1); // Dodajemo simbol u listu
    }
    /* Pravilo za dodavanje simbola u postojeću listu */
    | symbol_list ',' symbol {
        $1->push_back($3); // Dodajemo novi simbol u postojeću listu
        $<symbolList>$ = $1; // Vraćamo ažuriranu listu
    }
    ;

symbol:
    IDENTIFIER {
        $<sval>$ = strdup($1); // IDENTIFIER vraća string (char*)
    }
    ;

initializer_list:
    initializer {
        $<initializerList>$ = new std::vector<Initializer>();
        $<initializerList>$->push_back(*$1);  // Dodaj pojedinačni initializer u listu
        delete $1;
    }
    | initializer_list ',' initializer {
        $1->push_back(*$3);  // Dodaj novi initializer u postojeću listu
        $<initializerList>$ = $1;            // Vraćaš ažuriranu listu
        delete $3;
    }
;

initializer:
    LITERAL {
        $<initializer>$ = new Initializer($1); // Kreiraš pojedinačni literalni inicijalizator
    }
    | IDENTIFIER {
        $<initializer>$ = new Initializer($1); // Kreiraš pojedinačni simbolički inicijalizator
    }
;

operand:
    IDENTIFIER {
        $<sval>$ = strdup($1); // Pretpostavka da je operand simbol
    }
    | LITERAL {
        std::string str = std::to_string($1);
        (yyval.sval) = strdup(str.c_str());
    }
    ;

register:
    IDENTIFIER {
        $<sval>$ = strdup($1); // Pretpostavka da je registr simbol
    }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Greška: %s na liniji %d\n", s, line_num);
}