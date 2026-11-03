    .set noreorder
    .section .text.h1_bda_entry,"ax",@progbits
    .align 2
    .globl h1_bda_main
    .type h1_bda_main, @function
h1_bda_main:
    addiu $sp,$sp,-32
    sw $ra,28($sp)
    sw $gp,24($sp)
    move $t0,$sp
    lui $sp,%hi(pvz_runtime_stack+131072)
    addiu $sp,$sp,%lo(pvz_runtime_stack+131072)
    addiu $sp,$sp,-32
    sw $t0,24($sp)
    jal pvz_runtime_main
    nop
    lw $sp,24($sp)
    lw $gp,24($sp)
    lw $ra,28($sp)
    jr $ra
    addiu $sp,$sp,32
    .size h1_bda_main,.-h1_bda_main
