/* Program entry that binds __libc_start_main@GLIBC_2.17 instead of the
 * GLIBC_2.34 version newer crt1.o uses, so portmsg runs on older CFWs. */
    .symver __libc_start_main, __libc_start_main@GLIBC_2.17
    .text
    .globl _start
    .type _start, %function
_start:
    mov x29, #0
    mov x30, #0
    mov x5, x0              /* rtld_fini */
    ldr x1, [sp]            /* argc */
    add x2, sp, #8          /* argv */
    mov x6, sp              /* stack_end */
    adrp x0, main
    add x0, x0, :lo12:main
    mov x3, #0              /* init */
    mov x4, #0              /* fini */
    bl __libc_start_main
    brk #0
    .size _start, . - _start
    .section .note.GNU-stack, "", %progbits
