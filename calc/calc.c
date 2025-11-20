#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

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

// 変数・関数名最大文字数
#define MAX_IDENTIFIER_LEN 32
// 最大変数・関数数
#define MAX_VAR_FUNC 128
// 最大関数定義部分
#define MAX_FUNCTION_CODE_LENGTH (1024 * 1024)
// 最大引数数
#define MAX_ARGUMENTS 16

typedef struct {
  char name[MAX_IDENTIFIER_LEN + 1];
  int arg_count;
  char code[MAX_FUNCTION_CODE_LENGTH];
  size_t code_length;
} FunctionInfo;

char variable_names[MAX_VAR_FUNC][MAX_IDENTIFIER_LEN + 1];
int variable_count = 0;

int is_haste = 1;  // 1: 即時出力モード、0: 遅延出力モード

FunctionInfo functions[MAX_VAR_FUNC];
int function_count = 0;
FunctionInfo* current_function = NULL;

void initialize();
void input_number(char** p);
int input_variable(char** p);
void start_def_func(char** p);
void start_call_func(char** p, int nest_level);
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
bool is_identifier_char(char c);
char peek(char** p);
void ignore_consecutive_operators(char** p);
void ignore_all_sign_inversions(char** p);
void reset_formula(Op* last_op, Sign* sign);
int nesting(char** p, int nest_level, int is_misaligned);
void finish_nesting(int is_misaligned);
void def_builtin_func();
void def_default_func();

/**
 * @brief フォーマット付き出力をグローバル変数 is_haste に応じて遅延させる関数。
 * @param fmt フォーマット文字列。
 * @return 本来書きたかった文字数。
 *
 * 可変長引数は通常の printf と同じ取り扱いで受け取り、必要に応じて
 * FunctionInfo に書き出す。
 */
int mprintf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  if (is_haste) {
    // ふつうに mprintf
    int ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
  } else {
    if (!current_function) {
      va_end(ap);
      return 0;
    }
    FunctionInfo* t = current_function;
    size_t capacity = sizeof(t->code);
    if (t->code_length >= capacity) {
      // もうこれ以上は入らない
      va_end(ap);
      return 0;
    }

    size_t remaining = capacity - t->code_length;
    char* dest = t->code + t->code_length;

    int written = vsnprintf(dest, remaining, fmt, ap);
    va_end(ap);

    if (written < 0) {
      // フォーマットエラー
      return written;
    }

    if ((size_t)written >= remaining) {
      // バッファが足りない
      t->code_length = capacity - 1;
    } else {
      t->code_length += (size_t)written;
    }
    return written;
  }
}

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
 * @param is_misaligned 現在のスタックが 16 バイト境界からずれていたかどうか。
 * @return 成功時0、入力が不正な場合は1などのエラーコード。
 */
int parser(char** p, int nest_level, int is_misaligned) {
  Sign sign = S_PLUS;
  Op last_op = PLUS;
  while (**p) {
    if (is_digit(**p)) {
      // 数字を構成する
      input_number(p);
    } else if (**p == '!') {
      (*p)++;
      start_def_func(p);
      if (!peek(p)) {
        finalize();
        return 0;
      }
    } else if (**p == '@') {
      (*p)++;
      start_call_func(p, nest_level);
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
      if (current_function == NULL) {
        mprintf("xorl %%edx, %%edx\n");
      }
      reset_formula(&last_op, &sign);
      // 関数定義についてもリセット
      is_haste = 1;
      current_function = NULL;
      (*p)++;
    } else if (is_memory_clear(**p)) {
      // メモリをクリアする
      mprintf("xorl %%edx, %%edx\n");
      mprintf("movl %%edx,  %%r11d\n");
      /* also keep memory register initialized in prologue; r11 used as memory
       */
      reset_formula(&last_op, &sign);
      (*p)++;
    } else if (is_memory_recall(**p)) {
      // メモリを呼び出す
      mprintf("movl %%r11d, %%edx\n");
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
      mprintf("movl %%r11d, %%eax\n");
      // 加算してメモリに戻す
      mprintf("addl %%edx, %%eax\n");
      mprintf("jo L_overflow\n");
      mprintf("movl %%eax, %%r11d\n");
      // 計算結果はクリアする
      mprintf("xorl %%edx, %%edx\n");
      reset_formula(&last_op, &sign);
      (*p)++;
    } else if (is_memory_sub(**p)) {
      // 現在の項を計算する
      apply_last_op(last_op, sign);
      // メモリから取り出す
      mprintf("movl %%r11d, %%eax\n");
      // 減算してメモリに戻す
      mprintf("subl %%edx, %%eax\n");
      mprintf("jo L_overflow\n");
      mprintf("movl %%eax, %%r11d\n");
      // 計算結果はクリアする
      mprintf("xorl %%edx, %%edx\n");
      reset_formula(&last_op, &sign);
      (*p)++;
    } else if (**p == '(') {
      // 入れ子開始
      (*p)++;
      int result = nesting(p, nest_level, 0);
      if (result != 0) {
        return result;
      }
    } else if (**p == ',') {
      // 引数区切り
      apply_last_op(last_op, sign);
      finish_nesting(is_misaligned);
      return 0;
    } else if (**p == ')') {
      // 入れ子終了
      (*p)++;
      // 現在の項を適用する
      apply_last_op(last_op, sign);
      finish_nesting(is_misaligned);
      return 0;
    } else if (is_identifier_char(**p)) {
      // 変数名を構成する（未実装）
      int result = input_variable(p);
      if (result != 0) {
        return 1;
      }
    } else if (**p == '#' && current_function != NULL) {
      // 関数内引数参照
      (*p)++;
      int arg_index = 0;
      while (is_digit(**p)) {
        arg_index = arg_index * 10 + (**p - '0');
        (*p)++;
      }
      arg_index = current_function->arg_count - arg_index;
      int offset = 16 + arg_index * 8;
      mprintf("movl %d(%%rbp), %%eax\n", offset);
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
  def_default_func();
  return parser(p, 0, 0);
}

/**
 * @brief デフォルト関数定義を追加する。
 *
 * ビルドインではなくソース側で記述された関数テンプレートを、parser
 * を使って事前に読み込んでおく。
 */
void def_default_func() {
  is_haste = 0;
  static const char* codes[] = {
    "!sgn[1]:@step(#1)-@step(#1S);",
    "!abs[1]:@sgn(#1)*#1;",
    "!gt[2]:@step(#1-#2);",
    "!ge[2]:1-@step(#2-#1);",
    "!eq[2]:1-@step(@abs(#1-#2));",
    "!ne[2]:1-@eq(#1,#2);",
    "!min[2]:#1+#2-@abs(#1-#2)/2;",
    "!max[2]:#1+#2+@abs(#1-#2)/2;",
    "!if[3]:@abs(@sgn(#1))*(#2-#3)+#3;",
  };
  for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
    char* p = (char*)codes[i];
    parser(&p, 0, 0);
  }
  is_haste = 1;
}

/**
 * @brief
 * 出力アセンブリのプロローグを生成し、累積レジスタとメモリ領域を初期化する。
 * ビルドイン関数も定義する。
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
      "xorl %edx, %edx\n",
      "xorl %r11d, %r11d\n",
  };
  emit_lines(lines, sizeof(lines) / sizeof(lines[0]));
  def_builtin_func();
}

/**
 * @brief 関数情報を初期化する。
 * @param f 初期化する関数情報へのポインタ。
 */
void clear_func_code(FunctionInfo* f) {
  f->code_length = 0;
  f->code[0] = '\0';
}

/**
 * @brief FunctionInfo テーブルにビルトイン関数を登録し、遅延出力バッファにコードを蓄積する。
 *
 * 現在は step 関数のみをハードコードしているが、同じ仕組みで追加の
 * ビルトインを組み込める。
 */
void def_builtin_func() {
  is_haste = 0;
  // step function
  FunctionInfo* f = &functions[function_count++];
  clear_func_code(f);
  strcpy(f->name, "step");
  f->arg_count = 1;
  static const char* const lines[] = {
      " # Built-in function: step\n",
      " # Argument in %edi\n",
      "movl 16(%rbp), %edx\n",
      "testl %edx, %edx\n",
      "jg .Lpositive\n",
      "xorl %edx, %edx\n",
      "jmp .Ldone\n",
      ".Lpositive:\n",
      "movl $1, %edx\n",
      ".Ldone:\n",
  };
  for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
    strcat(f->code, lines[i]);
  }
  is_haste = 1;
}

/**
 * @brief 今から入れ子の解析を始めるための準備を行う。
 * @param p 入力文字列ポインタへのポインタ。
 * @param nest_level 現在の入れ子の深さ。
 * @param is_misaligned 呼び出し元でスタック整列が崩れている場合は 1。
 * @return 入れ子内の parser 実行結果。
 */
int nesting(char** p, int nest_level, int is_misaligned) {
  mprintf(" # Entering nesting level %d\n", nest_level);
  if (!is_misaligned) {
    mprintf("subq $8, %%rsp\n");     // スタック調整
  }
  mprintf("pushq %%rdx\n");        // 現在の計算結果を保存
  mprintf("xorl %%edx, %%edx\n");  // 新しい計算用にクリア
  mprintf("xorl %%eax, %%eax\n");
  mprintf(" # Starting parser at nesting level %d\n", nest_level);
  return parser(p, nest_level + 1, is_misaligned);
}

/**
 * @brief 入れ子計算の終了処理を行う。
 * @param is_misaligned 退避時にスタック調整を行わなかったかどうか。
 */
void finish_nesting(int is_misaligned) {
  mprintf(" # Exiting nesting level\n");
  mprintf("movl %%edx, %%eax\n");  // 括弧内の計算結果を %eax に移す
  mprintf("popq %%rdx\n");         // 計算結果を復元
  if (!is_misaligned) {
    mprintf("addq $8, %%rsp\n");     // スタック調整を戻す
  }
  mprintf(" # Finished nesting level\n");
}
/**
 * @brief 次の項の解析に備えて状態をリセットする。
 * @param last_op 直前の演算子を保持する変数へのポインタ。PLUS に初期化される。
 * @param sign 現在の符号フラグへのポインタ。1 に初期化される。
 */
void reset_formula(Op* last_op, Sign* sign) {
  mprintf("xorl %%eax, %%eax\n");
  *sign = S_PLUS;
  *last_op = PLUS;
}

/**
 * @brief 入力ポインタが指す連続した数字を読み取り、%eax
 * に10進整数値を構築する。
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
      mprintf("xorl %%eax, %%eax\n");
      return;
    }
  }
  while ((is_digit(**p)) || (**p >= 'a' && **p <= 'f')) {
    mprintf("movl %%eax, %%edi\n");
    mprintf("movl $%d, %%esi\n", radex);
    mprintf("subq $8, %%rsp\n");
    mprintf("pushq %%r11\n");
    mprintf("callq mul32\n");
    mprintf("popq %%r11\n");
    mprintf("addq $8, %%rsp\n");
    if (is_digit(**p)) {
      mprintf("addl $%d, %%eax\n", **p - '0');
    } else if (**p >= 'a' && **p <= 'f') {
      mprintf("addl $%d, %%eax\n", **p - 'a' + 10);
    }
    mprintf("jo L_overflow\n");
    (*p)++;
  }
}

/**
 * @brief 識別子を読み取り、out_var_name に格納する。
 * @param p 入力文字列へのポインタを示すポインタ。
 * @param out_var_name 読み取った識別子を書き込むバッファ。
 */
void read_identifier(char** p, char* out_var_name) {
  while (**p == ' ') {
    (*p)++;
  }
  char var_name[MAX_IDENTIFIER_LEN];
  int length = 0;
  // 変数名を読み取る
  while (is_identifier_char(**p) && length < MAX_IDENTIFIER_LEN) {
    var_name[length++] = **p;
    (*p)++;
  }
  var_name[length] = '\0';
  strcpy(out_var_name, var_name);
  // 空白を読み飛ばす
  while (**p == ' ') {
    (*p)++;
  }
}

/**
 * @brief 変数名を読み取り、対応する値を %eax に構築する。
 * @param p 入力文字列へのポインタを示すポインタ。変数名分だけ進む。
 * @return 成功時0、未定義変数の場合は1。
 */
int input_variable(char** p) {
  char var_name[MAX_IDENTIFIER_LEN];
  read_identifier(p, var_name);
  // 変数名が登録されているか確認する
  for (int i = 0; i < variable_count; i++) {
    if (strcmp(variable_names[i], var_name) == 0) {
      // 変数が見つかった場合、その値を %eax にロードする
      mprintf("movl var_%s(%%rip), %%eax\n", var_name);
      return 0;
    }
  }
  // 変数が見つからなかった場合、エラーを出力する
  fprintf(stderr, "Undefined variable: %s\n", var_name);
  return 1;
}

/**
 * @brief 関数定義の開始処理を行う。
 * @param p 入力文字列へのポインタを示すポインタ。
 *
 * 関数名・引数数を解析し、current_function を遅延出力モードで切り替える。
 */
void start_def_func(char** p) {
  char func_name[MAX_IDENTIFIER_LEN];
  read_identifier(p, func_name);
  if (**p != '[') {
    fprintf(stderr, "Expected '[' after function name: %s\n", func_name);
    return;
  }
  (*p)++;  // '[' をスキップ
  // 引数の数を読む
  int arg_count = 0;
  while (is_digit(**p)) {
    arg_count = arg_count * 10 + (**p - '0');
    (*p)++;
  }
  if (**p != ']' || peek(p) != ':') {
    fprintf(stderr, "Expected \"]:\" after argument count: %s\n", func_name);
    return;
  }
  (*p)++;  // ']' をスキップ
  (*p)++;  // ':' をスキップ
  // 既存の同盟名関数があるか確認する
  int found = -1;
  for (int i = 0; i < function_count; i++) {
    if (strcmp(functions[i].name, func_name) == 0) {
      found = i;
      break;
    }
  }
  // 関数情報を作成する
  if (function_count < MAX_VAR_FUNC) {
    current_function = &functions[found < 0 ? function_count++ : found];
    strcpy(current_function->name, func_name);
    current_function->arg_count = arg_count;
    current_function->code_length = 0;
    current_function->code[0] = '\0';
    is_haste = 0;  // 遅延出力モードに切り替え
  }
}

/**
 * @brief 関数呼び出しの処理を行う。
 * @param p 入力文字列へのポインタを示すポインタ。
 * @param nest_level 呼び出し元の入れ子深度。
 */
void start_call_func(char** p, int nest_level) {
  char func_name[MAX_IDENTIFIER_LEN];
  read_identifier(p, func_name);
  // 既存の同盟名関数があるか確認する
  int found = -1;
  for (int i = 0; i < function_count; i++) {
    if (strcmp(functions[i].name, func_name) == 0) {
      found = i;
      break;
    }
  }
  if (found < 0) {
    fprintf(stderr, "Undefined function: %s\n", func_name);
    return;
  }
  if (**p != '(') {
    fprintf(stderr, "Expected '(' after function name: %s\n", func_name);
    return;
  }
  (*p)++;  // '(' をスキップ
  FunctionInfo* f = &functions[found];
  int align = 0;
  // まずは現在の計算結果をスタックに保存する
  if (f->arg_count % 2 == 0) {
    mprintf("subq $8, %%rsp\n");  // スタック調整（引数が偶数個の場合）
    align += 8;
  }

  align += 8;
  // 現在の計算結果を保存する
  mprintf("pushq %%rdx\n");
  // 引数をスタックに積む
  mprintf("  # Calling function %s with %d arguments\n", func_name, f->arg_count);
  for (int i = 0; i < f->arg_count; i++) {
    mprintf("  # Argument %d:\n", i + 1);
    nesting(p, nest_level, align % 16 == 0 ? 0 : 1);
    if (i < f->arg_count - 1 && **p == ',') {
      (*p)++;  // ',' をスキップ
    } else if (i == f->arg_count - 1) {
      // 最後の引数の後の ')' はスキップされてる
    } else {
      fprintf(stderr, "Expected ',' or ')' after argument %d of function %s\n", i + 1, func_name);
      return;
    }
    align += 8;
    mprintf("pushq %%rax\n");  // 引数をスタックに積む
    mprintf("  # Result of argument %d in %%eax\n", i + 1);
  }
  if (f->arg_count == 0) {
    if (**p != ')') {
      fprintf(stderr, "Expected ')' to close call to function %s\n", func_name);
      return;
    }
    (*p)++;
  }
  mprintf("callq func_%s\n", func_name);
  // スタックを引数分だけ戻す
  if (f->arg_count > 0) {
    mprintf("addq $%d, %%rsp\n", f->arg_count * 8);
  }
  // 返り値は %eax にあるからOK
  // 保存していた計算結果を復元する
  mprintf("popq %%rdx\n");
  if (f->arg_count % 2 == 0) {
    mprintf("addq $8, %%rsp\n");  // スタック調整を戻す（引数が偶数個の場合）
  }
}



/**
 * @brief 直前の演算子と符号に基づき、%edx の累積結果に現在の項を適用する。
 * @param last_op 適用すべき演算子。
 * @param sign 項に掛ける符号。-1 の場合は項を反転してから演算する。
 */
void apply_last_op(Op last_op, Sign sign) {
  // 構築済みの数字が %eax にあるので %esi に移す
  mprintf("movl %%eax, %%esi\n");
  // 符号を反転する場合は %esi を neg する
  if (sign == S_MINUS) {
    mprintf("negl %%esi\n");
    mprintf("jo L_overflow\n");
  }
  switch (last_op) {
    case PLUS:
      mprintf("addl %%esi, %%edx\n");
      mprintf("jo L_overflow\n");
      break;
    case MINUS:
      mprintf("subl %%esi, %%edx\n");
      mprintf("jo L_overflow\n");
      break;
    case MUL:
      mprintf("movl %%edx, %%edi\n");
      mprintf("subq $8, %%rsp\n");
      mprintf("pushq %%r11\n");
      mprintf("callq mul32\n");
      mprintf("popq %%r11\n");
      mprintf("addq $8, %%rsp\n");
      mprintf("movq %%rax, %%rcx\n");
      mprintf("movslq %%eax, %%rdx\n");
      mprintf("cmpq %%rdx, %%rcx\n");
      mprintf("jne L_overflow\n");
      mprintf("movl %%eax, %%edx\n");
      break;
    case DIV:
      mprintf("movl %%edx, %%edi\n");
      mprintf("subq $8, %%rsp\n");
      mprintf("pushq %%r11\n");
      mprintf("callq div32\n");
      mprintf("popq %%r11\n");
      mprintf("addq $8, %%rsp\n");
      mprintf("movl %%eax, %%edx\n");
      break;
    case MOD:
      mprintf("movl %%edx, %%edi\n");
      mprintf("subq $8, %%rsp\n");
      mprintf("pushq %%r11\n");
      mprintf("callq div32\n");
      mprintf("popq %%r11\n");
      mprintf("addq $8, %%rsp\n");
      // mprintf("movl %%edx, %%edx\n");
      break;
  }
}

/**
 * @brief 変数名を読み取り、現在の計算結果を対応する変数に保存する。
 * @param p 入力文字列へのポインタを示すポインタ。変数名分だけ進む。
 */
void set_variable(char** p) {
  char var_name[MAX_IDENTIFIER_LEN];
  read_identifier(p, var_name);
  // 現在の計算結果を変数に保存する
  mprintf("movl %%edx, var_%s(%%rip)\n", var_name);
  // 変数名が既に登録されているか確認する
  for (int i = 0; i < variable_count; i++) {
    if (strcmp(variable_names[i], var_name) == 0) {
      return;
    }
  }
  // 新しい変数名を登録する
  if (variable_count < MAX_VAR_FUNC) {
    strcpy(variable_names[variable_count++], var_name);
  }
}

/**
 * @brief 変数定義用のデータセクションを出力する。
 *
 * parser 中に `->` で登録された全変数について .data/.rodata を発行する。
 */
void finalize_variables() {
  for (int i = 0; i < variable_count; i++) {
    printf(ASM_DATA_SECTION "\n");
    printf("var_%s:\n .long 0\n", variable_names[i]);
  }
}

/**
 * @brief 関数定義部分を出力する。
 *
 * 遅延出力された各 FunctionInfo の本体を .text として展開し、簡易 prologue/epilogue
 * を付与して再利用できるようにする。
 */
void finalize_functions() {
  for (int i = 0; i < function_count; i++) {
    FunctionInfo* f = &functions[i];
    printf(ASM_TEXT_SECTION "\n");
    printf(".globl func_%s\n", f->name);
    printf("func_%s:\n", f->name);
    printf("pushq %%rbp\n");
    printf("movq %%rsp, %%rbp\n");
    printf("xorl %%eax, %%eax\n");
    printf("xorl %%edx, %%edx\n");
    // 関数本体コードを出力する
    fputs(f->code, stdout);
    // 関数終了処理
    printf("movl %%edx, %%eax\n");
    printf("leave\n");
    printf("ret\n");
  }
}

/**
 * @brief 計算結果およびエラー表示、サポート関数定義まで含めた終端コードを生成する。
 */
void finalize() {
  static const char* const lines[] = {
      "movl %edx, %esi\n",
      "leaq L_fmt(%rip), %rdi\n",
      "movl $0, %eax\n",
      "callq " ASM_EXTERN_PRINTF "\n",
      "xorl %eax, %eax\n",
      "leave\n",
      "ret\n",
      "L_overflow:\n",
      "leaq L_err(%rip), %rdi\n",
      "movl $0, %eax\n",
      "callq " ASM_EXTERN_PRINTF "\n",
      "leave\n",
      "movl $1, %edi\n",
      "callq " ASM_EXTERN_EXIT "\n",
      ".globl div32\n",
      "div32:\n",
      "pushq %rbp\n",
      "movq %rsp, %rbp\n",
      "pushq %rdi\n",
      "pushq %rsi\n", // 割る数
      "testl %esi, %esi\n",
      "je L_overflow\n",
      "cmpl $0x80000000, %edi\n",
      "jne .L_div32_safe\n",
      "cmpl $-1, %esi\n",
      "je L_overflow\n",
      ".L_div32_safe:\n",
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
      "negq %rdx\n",
      ".L_div32_quotient_neg:\n",
      "xorl %edi, %esi\n",
      "testl $0x80000000, %esi\n",
      "jz .L_div32_end\n",
      "negq %rax\n",
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
      "negq %rax\n",
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
  finalize_functions();
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
bool is_identifier_char(char c) { return (c >= 'a' && c <= 'z') || (c == '_'); }
