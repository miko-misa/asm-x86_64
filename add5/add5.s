  .text
  .globl  add5
  .type   add5, @function
add5:
  movl %edi, %eax
  addl $5, %eax
  ret
  .globl  main
  .type   main, @function
main:
  pushq %rbp
  movq %rsp, %rbp
  subq $16, %rsp
  movl $12, %edi
  call add5
  movl %eax, -16(%rbp)
  leave
  ret