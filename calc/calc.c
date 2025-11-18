#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if defined(TARGET_SYSTEM_LINUX)
#define ASM_GLOBAL_MAIN "main"
#define ASM_EXTERN_PRINTF "printf"
#define ASM_EXTERN_EXIT "exit"
#define ASM_CSTRING_SECTION ".section .rodata"
#define ASM_DATA_SECTION ".section .data"
#elif defined(TARGET_SYSTEM_MAC) || defined(__APPLE__)
#define ASM_GLOBAL_MAIN "_main"
#define ASM_EXTERN_PRINTF "_printf"
#define ASM_EXTERN_EXIT "_exit"
#define ASM_CSTRING_SECTION ".section __TEXT,__cstring"
#define ASM_DATA_SECTION ".section __DATA,__data"
#else
#define ASM_GLOBAL_MAIN "_main"
#define ASM_EXTERN_PRINTF "_printf"
#define ASM_EXTERN_EXIT "_exit"
#define ASM_CSTRING_SECTION ".section __TEXT,__cstring"
#define ASM_DATA_SECTION ".section __DATA,__data"
#endif
#define ASM_TEXT_SECTION ".text"

// 最大入れ子数
#define MAX_EXPR_NESTING 128
// 変数名最大文字数
#define MAX_VAR_NAME_LENGTH 16
// 最大変数数
#define MAX_VARIABLES 128

char variable_names[MAX_VARIABLES][MAX_VAR_NAME_LENGTH + 1];
int variable_count = 0;

typedef enum {
  PLUS = '+',
  MINUS = '-',
  MUL = '*',
  DIV = '/',
  // 剰余
  MOD = '%',
} Op;

typedef enum {
  S_PLUS,
  S_MINUS,
} Sign;

typedef enum {
  RADEX_BIN = 2,
  RADEX_OCT = 8,
  RADEX_DEC = 10,
  RADEX_HEX = 16,
} Radex;

void initialize();
void input_number(char** p);
int input_variable(char** p);
void apply_last_op(Op last_op, Sign sign);
void set_variable(char** p);
void finalize();
bool is_digit(char c);
bool is_operator(char c);
bool is_sign_inversion(char c);
bool is_memory_clear(char c);
bool is_memory_recall(char c);
bool is_memory_add(char c);
bool is_memory_sub(char c);
bool is_variable_char(char c);
char peek(char** p);
void ignore_consecutive_operators(char** p);
void ignore_all_sign_inversions(char** p);
void reset_formula(Op* last_op, Sign* sign);
int nesting(char** p, int nest_level);
void finish_nesting();

/**
 * @brief 出力アセンブリの各行をまとめて出力する。
 * @param lines 出力する各行の配列。
 * @param count 配列の要素数。
 */
static void emit_lines(const char* const* lines, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    fputs(lines[i], stdout);
  }
}

/**
 * @brief
 * 電卓式を解析し、演算・メモリ操作に対応するアセンブリを生成する。
 * @param p 入力文字列ポインタへのポインタ。
 * @param nest_level 現在の入れ子レベル。
 * @return 成功時0、入力が不正な場合は1などのエラーコード。
 */
int parser(char** p, int nest_level) {
  Sign sign = S_PLUS;
  Op last_op = PLUS;
  while (**p) {
    if (is_digit(**p)) {
      // 数字を構成する
      input_number(p);
    } else if (**p == '-' && peek(p) == '>') {
      (*p) += 2;
      apply_last_op(last_op, sign);
      set_variable(p);
    } else if (is_operator(**p)) {
      // 演算子を適用する
      // 最後の演算子以外を読み飛ばす
      ignore_consecutive_operators(p);
      // 現在の項を適用する
      apply_last_op(last_op, sign);
      reset_formula(&last_op, &sign);
      // 次の演算子を設定する（enum
      // にリテラルを割り当てたので、文字コードを直接代入）
      last_op = (Op) * *p;
      (*p)++;
      // 直後の符号反転トークンをすべて読み飛ばす
      ignore_all_sign_inversions(p);
    } else if (**p == ' ') {
      // 空白を読み飛ばす（仕様上はありえない）
      (*p)++;
    } else if (is_sign_inversion(**p)) {
      // 符号反転トークンを適用する
      sign = (sign == S_PLUS) ? S_MINUS : S_PLUS;
      (*p)++;
    } else if (**p == '=') {
      // 現在の項を適用する
      apply_last_op(last_op, sign);
      // 変数定義の場合
      // 計算結果を出力して終了する
      if (nest_level == 0) {
        finalize();
      }
      return 0;
    } else if (**p == ';') {
      // 式の区切り
      // 現在の項を適用する
      apply_last_op(last_op, sign);
      // 計算結果をリセット
      printf("xorl %%edx, %%edx\n");
      reset_formula(&last_op, &sign);
      (*p)++;
    } else if (is_memory_clear(**p)) {
      // メモリをクリアする
      printf("xorl %%edx, %%edx\n");
      printf("movl %%edx,  %%r11d\n");
      /* also keep memory register initialized in prologue; r11 used as memory
       */
      reset_formula(&last_op, &sign);
      (*p)++;
    } else if (is_memory_recall(**p)) {
      // メモリを呼び出す
      printf("movl %%r11d, %%edx\n");
      if (!peek(p)) {
        finalize();
        return 0;
      }
      reset_formula(&last_op, &sign);
      (*p)++;
    } else if (is_memory_add(**p)) {
      // 現在の項を計算する
      apply_last_op(last_op, sign);
      // メモリから取り出す
      printf("movl %%r11d, %%eax\n");
      // 加算してメモリに戻す
      printf("addl %%edx, %%eax\n");
      printf("jo L_overflow\n");
      printf("movl %%eax, %%r11d\n");
      // 計算結果はクリアする
      printf("xorl %%edx, %%edx\n");
      reset_formula(&last_op, &sign);
      (*p)++;
    } else if (is_memory_sub(**p)) {
      // 現在の項を計算する
      apply_last_op(last_op, sign);
      // メモリから取り出す
      printf("movl %%r11d, %%eax\n");
      // 減算してメモリに戻す
      printf("subl %%edx, %%eax\n");
      printf("jo L_overflow\n");
      printf("movl %%eax, %%r11d\n");
      // 計算結果はクリアする
      printf("xorl %%edx, %%edx\n");
      reset_formula(&last_op, &sign);
      (*p)++;
    } else if (**p == '(') {
      // 入れ子開始
      (*p)++;
      int result = nesting(p, nest_level);
      if (result != 0) {
        return result;
      }
    } else if (**p == ')') {
      // 入れ子終了
      (*p)++;
      // 現在の項を適用する
      apply_last_op(last_op, sign);
      finish_nesting();
      return 0;
    } else if (is_variable_char(**p)) {
      // 変数名を構成する（未実装）
      int result = input_variable(p);
      if (result != 0) {
        return 1;
      }
    } else {
      fprintf(stderr, "Invalid character: %c\n", **p);
      return 1;
    }
  }
  return 0;
}

/**
 * @brief
 * コマンドライン引数の電卓式を解析し、演算・メモリ操作に対応するアセンブリを生成するエントリポイント。
 * @param argc 引数の数。
 * @param argv 引数ベクタ。argv[1] に電卓式を受け取る。
 * @return 成功時0、入力が不正な場合は1などのエラーコード。
 */
int main(int argc, char* argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <calc_literal>\n", argv[0]);
    return 1;
  }
  char* input = argv[1];
  char** p = &input;
  initialize();
  return parser(p, 0);
}

/**
 * @brief
 * 出力アセンブリのプロローグを生成し、累積レジスタとメモリ領域を初期化する。
 */
void initialize() {
  static const char* const lines[] = {
      ".att_syntax prefix\n",
      ".extern " ASM_EXTERN_PRINTF "\n",
      ".extern " ASM_EXTERN_EXIT "\n",
      ASM_CSTRING_SECTION "\n",
      "L_fmt:\n",
      ".asciz \"%d\\n\"\n",
      "L_err:\n",
      ".asciz \"E\\n\"\n",
      ASM_TEXT_SECTION "\n",
      ".globl " ASM_GLOBAL_MAIN "\n",
      ASM_GLOBAL_MAIN ":\n",
      "pushq %rbp\n",
      "movq %rsp, %rbp\n",
      "xorl %eax, %eax\n",
      "pushq $0\n",
      "xorl %edx, %edx\n",
      "xorl %r11d, %r11d\n",
  };
  emit_lines(lines, sizeof(lines) / sizeof(lines[0]));
}

/**
 * @brief 今から入れ子の解析を始めるための準備を行う。
 * @param p 入力文字列ポインタへのポインタ。
 * @param nest_level 現在の入れ子の深さ。
 */
int nesting(char** p, int nest_level) {
  printf("pushq %%rdx\n");          // 現在の計算結果を保存
  printf("xorl %%edx, %%edx\n");    // 新しい計算用にクリア
  return parser(p, nest_level + 1);
}

/**
 * @brief 入れ子計算の終了処理を行う。
 */
void finish_nesting() {
  printf("movl %%edx, %%eax\n");   // 括弧内の計算結果を %eax に移す
  printf("popq %%rdx\n");          // 計算結果を復元
}

/**
 * @brief 次の項の解析に備えて状態をリセットする。
 * @param last_op 直前の演算子を保持する変数へのポインタ。PLUS に初期化される。
 * @param sign 現在の符号フラグへのポインタ。1 に初期化される。
 */
void reset_formula(Op* last_op, Sign* sign) {
  printf("xorl %%eax, %%eax\n");
  *sign = S_PLUS;
  *last_op = PLUS;
}

/**
 * @brief 入力ポインタが指す連続した数字を読み取り、%eax に10進整数値を構築する。
 * @param p 入力文字列へのポインタを示すポインタ。読み取った桁数だけ進む。
 */
void input_number(char** p) {
  Radex radex = RADEX_DEC;
  if (**p == '0') {
    (*p)++;
    if (**p == 'x') {
      // 16進数
      radex = RADEX_HEX;
      (*p)++;
    } else if (**p >= '0' && **p <= '7') {
      // 8進数
      radex = RADEX_OCT;
      // 次の桁から数字
    } else if (**p == 'b') {
      // 2進数
      radex = RADEX_BIN;
      (*p)++;
    } else {
      // 0 単独
      printf("xorl %%eax, %%eax\n");
      return;
    }
  }
  while ((**p >= '0' && **p <= '9') || (**p >= 'a' && **p <= 'f')) {
    printf("movl %%eax, %%edi\n");
    printf("movl $%d, %%esi\n", radex);
    printf("pushq %%r11\n");
    printf("callq mul32\n");
    printf("popq %%r11\n");
    if (**p >= '0' && **p <= '9') {
      printf("addl $%d, %%eax\n", **p - '0');
    } else if (**p >= 'a' && **p <= 'f') {
      printf("addl $%d, %%eax\n", **p - 'a' + 10);
    }
    printf("jo L_overflow\n");
    (*p)++;
  }
}

/**
 * @brief 変数名を読み取り、対応する値を %eax に構築する。
 * @param p 入力文字列へのポインタを示すポインタ。
 */
void read_variable(char** p, char* out_var_name) {
  char var_name[MAX_VAR_NAME_LENGTH];
  int length = 0;
  // 変数名を読み取る
  while (is_variable_char(**p) && length < MAX_VAR_NAME_LENGTH) {
    var_name[length++] = **p;
    (*p)++;
  }
  var_name[length] = '\0';
  strcpy(out_var_name, var_name);
}

/**
 * @brief 変数名を読み取り、対応する値を %eax に構築する。
 * @param p 入力文字列へのポインタを示すポインタ。変数名分だけ進む。
 * @return 成功時0、未定義変数の場合は1。
 */
int input_variable(char** p) {
  char var_name[MAX_VAR_NAME_LENGTH];
  read_variable(p, var_name);
  // 変数名が登録されているか確認する
  for (int i = 0; i < variable_count; i++) {
    if (strcmp(variable_names[i], var_name) == 0) {
      // 変数が見つかった場合、その値を %eax にロードする
      printf("movl var_%s(%%rip), %%eax\n", var_name);
      return 0;
    }
  }
  // 変数が見つからなかった場合、エラーを出力する
  fprintf(stderr, "Undefined variable: %s\n", var_name);
  return 1;
}

/**
 * @brief 直前の演算子と符号に基づき、%edx の累積結果に現在の項を適用する。
 * @param last_op 適用すべき演算子。
 * @param sign 項に掛ける符号。-1 の場合は項を反転してから演算する。
 */
void apply_last_op(Op last_op, Sign sign) {
  // 構築済みの数字が %eax にあるので %esi に移す
  printf("movl %%eax, %%esi\n");
  // 符号を反転する場合は %esi を neg する
  if (sign == S_MINUS) {
    printf("negl %%esi\n");
    printf("jo L_overflow\n");
  }
  switch (last_op) {
    case PLUS:
      printf("addl %%esi, %%edx\n");
      printf("jo L_overflow\n");
      break;
    case MINUS:
      printf("subl %%esi, %%edx\n");
      printf("jo L_overflow\n");
      break;
    case MUL:
      printf("movl %%edx, %%edi\n");
      printf("pushq %%r11\n");
      printf("callq mul32\n");
      printf("popq %%r11\n");
      printf("movq %%rax, %%rcx\n");
      printf("movslq %%eax, %%rdx\n");
      printf("cmpq %%rdx, %%rcx\n");
      printf("jne L_overflow\n");
      printf("movl %%eax, %%edx\n");
      break;
    case DIV:
      printf("testl %%esi, %%esi\n");
      printf("je L_overflow\n");
      printf("movl %%edx, %%edi\n");
      printf("pushq %%r11\n");
      printf("callq div32\n");
      printf("popq %%r11\n");
      printf("movl %%eax, %%edx\n");
      break;
    case MOD:
      printf("testl %%esi, %%esi\n");
      printf("je L_overflow\n");
      printf("movl %%edx, %%edi\n");
      printf("pushq %%r11\n");
      printf("callq div32\n");
      printf("popq %%r11\n");
      // printf("movl %%edx, %%edx\n");
      break;
  }
}

/**
 * @brief 変数名を読み取り、現在の計算結果を対応する変数に保存する。
 * @param p 入力文字列へのポインタを示すポインタ。変数名分だけ進む。
 */
void set_variable(char** p) {
  char var_name[MAX_VAR_NAME_LENGTH];
  read_variable(p, var_name);
  // 現在の計算結果を変数に保存する
  printf("movl %%edx, var_%s(%%rip)\n", var_name);
  // 変数名が既に登録されているか確認する
  for (int i = 0; i < variable_count; i++) {
    if (strcmp(variable_names[i], var_name) == 0) {
      return;
    }
  }
  // 新しい変数名を登録する
  if (variable_count < MAX_VARIABLES) {
    strcpy(variable_names[variable_count++], var_name);
  }
}

/**
 * @brief 変数定義用のデータセクションを出力する。
 */
void finalize_variables() {
  for (int i = 0; i < variable_count; i++) {
    printf(ASM_DATA_SECTION "\n");
    printf("var_%s:\n .long 0\n", variable_names[i]);
  }
}

/**
 * @brief 計算結果の表示とスタック後始末を行うアセンブリを生成する。
 */
void finalize() {
  static const char* const lines[] = {
      "subq $8, %rsp\n",
      "movl %edx, %esi\n",
      "leaq L_fmt(%rip), %rdi\n",
      "movl $0, %eax\n",
      "callq " ASM_EXTERN_PRINTF "\n",
      "addq $8, %rsp\n",
      "addq $8, %rsp\n",
      "xorl %eax, %eax\n",
      "leave\n",
      "ret\n",
      "L_overflow:\n",
      "subq $8, %rsp\n",
      "leaq L_err(%rip), %rdi\n",
      "movl $0, %eax\n",
      "callq " ASM_EXTERN_PRINTF "\n",
      "addq $8, %rsp\n",
      "addq $8, %rsp\n",
      "leave\n",
      "subq $8, %rsp\n",
      "movl $1, %edi\n",
      "callq " ASM_EXTERN_EXIT "\n",
      ".globl div32\n",
      "div32:\n",
      "pushq %rbp\n",
      "movq %rsp, %rbp\n",
      "pushq %rdi\n",
      "pushq %rsi\n",
      "callq abs32\n",
      "movq %rax, %r8\n",
      "movq %rsi, %rdi\n",
      "callq abs32\n",
      "movq %rax, %r9\n",
      "xorq %rax, %rax\n",
      "xorq %rdx, %rdx\n",
      "movl $32, %ecx\n",
      ".L_div32_loop:\n",
      "shll $1, %eax\n",
      "shll $1, %r8d\n",
      "rcll %edx\n",
      "cmpl %edx, %r9d\n",
      "jg .L_div32_skip\n",
      "addl $1, %eax\n",
      "subl %r9d, %edx\n",
      ".L_div32_skip:\n",
      "decl %ecx\n",
      "jnz .L_div32_loop\n",
      "popq %rsi\n",
      "popq %rdi\n",
      "testl $0x80000000, %edi\n",
      "jz .L_div32_quotient_neg\n",
      "negl %edx\n",
      ".L_div32_quotient_neg:\n",
      "xorl %edi, %esi\n",
      "testl $0x80000000, %esi\n",
      "jz .L_div32_end\n",
      "negl %eax\n",
      ".L_div32_end:\n",
      "leave\n",
      "ret\n",
      ".globl mul32\n",
      "mul32:\n",
      "pushq %rbp\n",
      "movq %rsp, %rbp\n",
      "pushq %rdi\n",
      "pushq %rsi\n",
      "callq abs32\n",
      "movq %rax, %r8\n",
      "movq %rsi, %rdi\n",
      "callq abs32\n",
      "movq %rax, %r9\n",
      "xorq %rax, %rax\n",
      "movl $32, %ecx\n",
      ".L_mul32_loop:\n",
      "clc\n",
      "rcrl %r9d\n",
      "jnc .L_mul32_skip\n",
      "addq %r8, %rax\n",
      ".L_mul32_skip:\n",
      "shlq $1, %r8\n",
      "decl %ecx\n",
      "jnz .L_mul32_loop\n",
      "popq %rsi\n",
      "popq %rdi\n",
      "xorl %edi, %esi\n",
      "testl $0x80000000, %esi\n",
      "jz .L_mul32_end\n",
      "negl %eax\n",
      ".L_mul32_end:\n",
      "leave\n",
      "ret\n",
      ".globl abs32\n",
      "abs32:\n",
      "pushq %rbp\n",
      "movq %rsp, %rbp\n",
      "movl %edi, %eax\n",
      "sarl $31, %edi\n",
      "xorl %edi, %eax\n",
      "subl %edi, %eax\n",
      "leave\n",
      "ret\n",
  };
  emit_lines(lines, sizeof(lines) / sizeof(lines[0]));
  finalize_variables();
}

/**
 * @brief 文字が数字かどうかを判定する。
 * @param c 判定対象の文字。
 * @return 数字であれば true。
 */
bool is_digit(char c) { return c >= '0' && c <= '9'; }
/**
 * @brief 文字が四則演算子かどうかを判定する。
 * @param c 判定対象の文字。
 * @return 四則演算子であれば true。
 */
bool is_operator(char c) {
  /* enum `Op` のリテラル値を活用することで定義を一元化 */
  return c == PLUS || c == MINUS || c == MUL || c == DIV || c == MOD;
}
/**
 * @brief 文字が符号反転トークンかどうかを判定する。
 * @param c 判定対象の文字。
 * @return 'S' であれば true。
 */
bool is_sign_inversion(char c) { return c == 'S'; }
/**
 * @brief 現在位置の次の文字を参照する。
 * @param p 入力文字列ポインタへのポインタ。
 * @return 次の文字。終端に到達している場合の動作は呼び出し側が保証する。
 */
char peek(char** p) { return *(*p + 1); }
/**
 * @brief 演算子が連続した場合に余分な演算子を読み飛ばす。
 * @param p 入力文字列ポインタへのポインタ。読み飛ばした分だけ進む。
 */
void ignore_consecutive_operators(char** p) {
  while (is_operator(peek(p))) {
    (*p)++;
  }
}
/**
 * @brief 符号反転トークンを連続して読み飛ばす。
 * @param p 入力文字列ポインタへのポインタ。読み飛ばした分だけ進む。
 */
void ignore_all_sign_inversions(char** p) {
  while (is_sign_inversion(**p)) {
    (*p)++;
  }
}

/**
 * @brief メモリクリア命令かどうかを判定する。
 * @param c 判定対象の文字。
 * @return 'C' であれば true。
 */
bool is_memory_clear(char c) { return c == 'C'; }
/**
 * @brief メモリリコール命令かどうかを判定する。
 * @param c 判定対象の文字。
 * @return 'R' であれば true。
 */
bool is_memory_recall(char c) { return c == 'R'; }
/**
 * @brief メモリ加算命令かどうかを判定する。
 * @param c 判定対象の文字。
 * @return 'P' であれば true。
 */
bool is_memory_add(char c) { return c == 'P'; }
/**
 * @brief メモリ減算命令かどうかを判定する。
 * @param c 判定対象の文字。
 * @return 'M' であれば true。
 */
bool is_memory_sub(char c) { return c == 'M'; }
/**
 * @brief 文字が変数名に使用可能かどうかを判定する。
 * @param c 判定対象の文字。
 * @return 変数名に使用可能な文字であれば true。
 */
bool is_variable_char(char c) {
  return (c >= 'a' && c <= 'z') || (c == '_');
}
