#include <config.h>

/*
 * ISRs -- Interrupt Service Routines
 */

.global int_num, err_num
int_num: .long 0
err_num: .long 0

.macro ISR_NOERR v
.global __x86_isr\v
__x86_isr\v:
    cli
#if ARCH_BITS==32
    movl $0x0, (err_num)
    movl $\v, (int_num)
#else
    movl $0x0, err_num(%rip)
    movl $\v, int_num(%rip)
#endif
    jmp isr_handler
.endm

.macro ISR_ERR v
.global __x86_isr\v
__x86_isr\v:
    cli
#if ARCH_BITS==32
    popl (err_num)
    movl $\v, (int_num)
    jmp isr_handler
#else
    push %rax
    push %rbx
    movl -16(%esp), %ebx
    movabs err_num, %rax
    movl %ebx, (%rax)
    movabs int_num, %rax
    movl $\v, (%rax)
    pop  %rbx
    pop  %rax
    jmp isr_handler
#endif
.endm

.macro push_context
#if ARCH_BITS==32
    push %eax
    push %edx
    push %ecx
    push %ebx
    push %ebp
    push %esi
    push %edi
#else
    push %rax
    push %rdx
    push %rcx
    push %rbx
    push %rbp
    push %rsi
    push %rdi
#endif
.endm
    
.macro pop_context
#if ARCH_BITS==32
    pop %edi
    pop %esi
    pop %ebp
    pop %ebx
    pop %ecx
    pop %edx
    pop %eax
#else
    pop %rdi
    pop %rsi
    pop %rbp
    pop %rbx
    pop %rcx
    pop %rdx
    pop %rax
#endif
.endm

/* Refer to 
 * - Intel 64 and IA-32 Architectures Software Developer’s Manual
 * - Volume 3: System Programming Guide
 * - Table 6-1. Protected-Mode Exceptions and Interrupts
 */

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31
ISR_NOERR 128


.extern __x86_isr
isr_handler:
    push_context
#if ARCH_BITS==32
    push %esp
    call __x86_isr
    pop %eax
#else
    push %rsp
    call __x86_isr
    pop %rax
#endif
    pop_context
    iret

//
// IRQs -- external interrupt requists (from PIC)
//

.macro IRQ n, i
.global __x86_irq\n
__x86_irq\n:
#if ARCH_BITS==32
    cli
    //movl %eax, (err_num)
    movl $\i, (int_num)
    jmp irq_stub
#else
    /* TODO */
#endif
.endm

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47


.extern __x86_irq_handler
irq_stub:
#if ARCH_BITS==32
    push_context
    push %esp
    call __x86_irq_handler
    pop %eax
    pop_context
    iret
#else
    /* TODO */
#endif

.global x86_jump_user
x86_jump_user:  /* eax, eip, cs, eflags, esp, ss */
#if ARCH_BITS==32
    pop  %eax   /* Caller return address */
    mov  $0x20 | 0x3, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    pop  %eax   /* eax for sys_fork return */
    iret
#else
    /* TODO */
#endif

.global x86_read_eip
x86_read_eip:
    mov (%esp), %eax
    ret

.global x86_goto
x86_goto:
#if ARCH_BITS==32
    pop %ebx    /* Caller return address */
    pop %ebx    /* eip */
    pop %ebp
    pop %esp
    mov $-1, %eax /* Return -1 -> Done switching */
    jmp *%ebx
#else
    /* TODO */
#endif


.extern internal_arch_sleep
.global x86_sleep
x86_sleep:
    push_context
    call internal_arch_sleep
    pop_context
    ret

.global x86_fork_return
x86_fork_return:
    pop_context
    iret

.global return_from_signal
return_from_signal:
    mov 4(%esp), %edi
    mov %edi, %esp    /* Fix stack pointer */
    pop_context
    iret

// vim: ft=gas:
