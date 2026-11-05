// SPDX-License-Identifier: MIT
.text
.globl _metal_lifetime_replay
_metal_lifetime_replay:
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %r14
    movq %rdi, %r14
    callq *%rsi
    popq %r14
    popq %rbx
    popq %rbp
    retq
