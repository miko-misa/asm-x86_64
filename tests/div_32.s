  .att_syntax prefix
  .extern printf
  .extern exit
.section .rodata
L_fmt:
  .asciz "%d\n"
L_err:
  .asciz "E\n"
  .text
  .globl main
main:
  push %rbp
  movq %rsp, %rbp
  movl $5, %edi
  movl $2, %esi
  call div32
  movl %eax,%esi
  xorl %eax,%eax
  leaq L_fmt(%rip), %rdi
  call printf
  xorl %eax, %eax
  leave
  ret
  .globl div32
div32:
  pushq %rbp
  movq %rsp, %rbp
  pushq %rdi
  pushq %rsi
  call abs32
  movq %rax, %r8 # dividend
  movq %rsi, %rdi
  call abs32
  movq %rax, %r9 # divisor

  xorq %rax, %rax # quotient
  xorq %rdx, %rdx # remainder
  movl $32, %ecx
.L_div32_loop:
  shll $1, %eax
  shll $1, %r8d
  rcll %edx
  cmpl %edx, %r9d
  jg .L_div32_skip
  addl $1, %eax
  subl %r9d, %edx
.L_div32_skip:
  decl %ecx
  jnz .L_div32_loop

  popq %rsi
  popq %rdi
  testl $0x80000000, %edi
  jz .L_div32_quotient_neg
  negl %edx
.L_div32_quotient_neg:
  xorl %edi, %esi
  testl $0x80000000, %esi
  jz .L_div32_end
  negl %eax
.L_div32_end:
  leave
  ret
  .globl abs32
abs32:
  pushq %rbp
  movq %rsp, %rbp
  movl %edi, %eax
  sarl $31, %edi
  xorl %edi, %eax
  subl %edi, %eax
  leave
  ret

