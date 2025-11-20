#include <stdbool.h>
#include <stdio.h>

#if defined(TARGET_SYSTEM_LINUX)
#define ASM_GLOBAL_MAIN "main"
#define ASM_EXTERN_PRINTF "printf"
#define ASM_EXTERN_EXIT "exit"
#define ASM_CSTRING_SECTION ".section .rodata"
#elif defined(TARGET_SYSTEM_MAC) || defined(__APPLE__)
#define ASM_GLOBAL_MAIN "_main"
#define ASM_EXTERN_PRINTF "_printf"
#define ASM_EXTERN_EXIT "_exit"
#define ASM_CSTRING_SECTION ".section __TEXT,__cstring"
#else
#define ASM_GLOBAL_MAIN "_main"
#define ASM_EXTERN_PRINTF "_printf"
#define ASM_EXTERN_EXIT "_exit"
#define ASM_CSTRING_SECTION ".section __TEXT,__cstring"
#endif
#define ASM_TEXT_SECTION ".text"

typedef enum {
  PLUS = '+',
  MINUS = '-',
  MUL = '*',
  DIV = '/',
} Op;

typedef enum {
  S_PLUS,
  S_MINUS,
} Sign;

void initialize();
void input_number(char** p);
void apply_last_op(Op last_op, Sign sign);
void finalize();
bool is_digit(char c);
bool is_operator(char c);
bool is_sign_inversion(char c);
bool is_memory_clear(char c);
bool is_memory_recall(char c);
bool is_memory_add(char c);
bool is_memory_sub(char c);
char peek(char** p);
void ignore_consecutive_operators(char** p);
void ignore_all_sign_inversions(char** p);
void reset_formula(Op* last_op, Sign* sign);

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
  Sign sign = S_PLUS;
  Op last_op = PLUS;
  initialize();
  while (**p) {
    if (is_digit(**p)) {
      // 数字を構成する
      input_number(p);
    } else if (is_operator(**p)) {
      // 演算子を適用する
      // 最後の演算子以外を読み飛ばす
      ignore_consecutive_operators(p);
      // 現在の項を適用する
      apply_last_op(last_op, sign);
      reset_formula(&last_op, &sign);
      // 次の演算子を設定する（enum に文字リテラルを割り当てたので直接代入）
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
      // 計算結果を出力して終了する
      finalize();
      return 0;
    } else if (is_memory_clear(**p)) {
      // メモリをクリアする
      printf("xorl %%edx, %%edx\n");
      printf("movl %%edx, (%%rsp)\n");
      reset_formula(&last_op, &sign);
      (*p)++;
    } else if (is_memory_recall(**p)) {
      // メモリを呼び出す
      printf("movl (%%rsp), %%edx\n");
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
      printf("movl (%%rsp), %%eax\n");
      // 加算してメモリに戻す
      printf("addl %%edx, %%eax\n");
      printf("jo L_overflow\n");
      printf("movl %%eax, (%%rsp)\n");
      // 計算結果はクリアする
      printf("xorl %%edx, %%edx\n");
      reset_formula(&last_op, &sign);
      (*p)++;
    } else if (is_memory_sub(**p)) {
      // 現在の項を計算する
      apply_last_op(last_op, sign);
      // メモリから取り出す
      printf("movl (%%rsp), %%eax\n");
      // 減算してメモリに戻す
      printf("subl %%edx, %%eax\n");
      printf("jo L_overflow\n");
      printf("movl %%eax, (%%rsp)\n");
      // 計算結果はクリアする
      printf("xorl %%edx, %%edx\n");
      reset_formula(&last_op, &sign);
      (*p)++;
    } else {
      fprintf(stderr, "Invalid character: %c\n", **p);
      return 1;
    }
  }
  return 0;
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
  };
  emit_lines(lines, sizeof(lines) / sizeof(lines[0]));
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
 * @brief 入力ポインタが指す連続した数字を読み取り、%eax
 * に10進整数値を構築する。
 * @param p 入力文字列へのポインタを示すポインタ。読み取った桁数だけ進む。
 */
void input_number(char** p) {
  while (is_digit(**p)) {
    printf("movl $%d, %%edi\n", **p - '0');
    printf("imull $10, %%eax, %%eax\n");
    printf("jo L_overflow\n");
    printf("addl %%edi, %%eax\n");
    printf("jo L_overflow\n");
    (*p)++;
  }
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
      printf("imull %%esi, %%edx\n");
      printf("jo L_overflow\n");
      break;
    case DIV:
      printf("movl %%esi, %%ecx\n");
      printf("testl %%ecx, %%ecx\n");
      printf("je L_overflow\n");
      printf("movl %%edx, %%eax\n");
      printf("cltd\n");
      printf("idivl %%ecx\n");
      printf("movl %%eax, %%edx\n");
      break;
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
  };
  emit_lines(lines, sizeof(lines) / sizeof(lines[0]));
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
  return c == PLUS || c == MINUS || c == MUL || c == DIV;
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
