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
  call mul32
  movl %eax,%esi
  xorl %eax,%eax
  leaq L_fmt(%rip), %rdi
  call printf
  xorl %eax, %eax
  leave
  ret
  .globl mul32
mul32:
  push %rbp
  movq %rsp, %rbp
  push %rdi
  push %rsi
  call abs32
  movq %rax, %r8
  movq %rsi, %rdi
  call abs32
  movq %rax, %r9

  xorq %rax, %rax
  movl $32, %ecx
.L_mul32_loop:
  clc
  rcrl %r9d
  jnc .L_mul32_skip
  addq %r8, %rax
.L_mul32_skip:
  shlq $1, %r8
  decl %ecx
  jnz .L_mul32_loop

  popq %rsi
  popq %rdi
  xorl %edi, %esi
  testl $0x80000000, %esi
  jz .L_mul32_end
  negl %eax
.L_mul32_end:
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

