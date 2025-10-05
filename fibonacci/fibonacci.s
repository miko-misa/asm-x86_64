  .section .rodata
fmt: .asciz "fibonacci( %ld ) = %ld\n"
  .bss
  .p2align 4
  .globl fib
fib:
  .skip 8000 # 8byte x 1000
  .text
# ========= fibonacci =========
  .globl fibonacci
  .type fibonacci, @function
fibonacci:
  pushq %rbp
  movq %rsp, %rbp

  cmpq $1, %rdi
  jle .L_start

  leaq fib(%rip), %rax
  cmpq $0, (%rax, %rdi, 8)
  je .L_fi_else
  movq (%rax, %rdi, 8), %rax

  popq %rbp
  ret

.L_fi_else:
  # callee-save
  pushq %rbx
  pushq %r12

  movq %rdi, %rbx
  decq %rdi
  call fibonacci
  movq %rax, %r12
  movq %rbx, %rdi
  subq $2, %rdi
  call fibonacci
  addq %r12, %rax
  leaq fib(%rip), %rdi
  movq %rax, (%rdi, %rbx, 8)

  # callee-save
  popq %r12
  popq %rbx
  leave
  ret
.L_start:
  movq %rdi, %rax
  leave
  ret
# ========= main =========
  .globl main
  .type main, @function
main:
  pushq %rbp
  movq %rsp, %rbp
  movq $50, %rbx
  movq %rbx, %rdi
  call fibonacci

  movq %rbx, %rsi
  movq %rax, %rdx
  leaq fmt(%rip), %rdi
  xor %eax, %eax
  call printf
  
  movl $0, %eax
  leave
  ret
