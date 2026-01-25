.syntax unified
    .arm
    .text
    .align 2

// -----------------------------------------------------------------------------
// Unsigned division: r0 = numerator, r1 = denominator
// Returns quotient in r0
.global __aeabi_uidiv
.type __aeabi_uidiv, %function
__aeabi_uidiv:
    push    {r2, r3, r4, lr}
    cmp     r1, #0
    beq     __div0_handler

    mov     r2, #0          // quotient
    mov     r3, #1          // bit = 1

    // align r1 (denominator) with highest bit of r0
1:
    cmp     r1, r0
    lslle   r1, r1, #1
    lslle   r3, r3, #1
    ble     1b

2:
    cmp     r0, r1
    subhs   r0, r0, r1
    orrhs   r2, r2, r3
    lsr     r1, r1, #1
    lsr     r3, r3, #1
    cmp     r3, #0
    bne     2b

    mov     r0, r2
    pop     {r2, r3, r4, pc}
.size __aeabi_uidiv, . - __aeabi_uidiv

// -----------------------------------------------------------------------------
// Signed division: r0 = numerator, r1 = denominator
// Returns quotient in r0
.global __aeabi_idiv
.type __aeabi_idiv, %function
__aeabi_idiv:
    push    {r2, r3, r4, lr}

    cmp     r1, #0
    beq     __div0_handler

    mov     r2, r0
    mov     r3, r1

    // Take signs
    eor     r4, r0, r1       // result sign in r4
    cmp     r0, #0
    rsblt   r0, r0, #0
    cmp     r1, #0
    rsblt   r1, r1, #0

    bl      __aeabi_uidiv
    cmp     r4, #0
    rsblt   r0, r0, #0

    pop     {r2, r3, r4, pc}
.size __aeabi_idiv, . - __aeabi_idiv

// -----------------------------------------------------------------------------
// Unsigned divmod: r0 = numerator, r1 = denominator
// Returns quotient in r0, remainder in r1
.global __aeabi_uidivmod
.type __aeabi_uidivmod, %function
__aeabi_uidivmod:
    push    {r2, r3, r4, lr}
    cmp     r1, #0
    beq     __div0_handler

    mov     r2, #0          // quotient
    mov     r3, #1          // bit = 1

    mov     r4, r1          // copy denominator
1:
    cmp     r4, r0
    lslle   r4, r4, #1
    lslle   r3, r3, #1
    ble     1b

2:
    cmp     r0, r4
    subhs   r0, r0, r4
    orrhs   r2, r2, r3
    lsr     r4, r4, #1
    lsr     r3, r3, #1
    cmp     r3, #0
    bne     2b

    mov     r1, r0      // remainder
    mov     r0, r2      // quotient

    pop     {r2, r3, r4, pc}
.size __aeabi_uidivmod, . - __aeabi_uidivmod

// -----------------------------------------------------------------------------
// Signed divmod: r0 = numerator, r1 = denominator
// Returns quotient in r0, remainder in r1
.global __aeabi_idivmod
.type __aeabi_idivmod, %function
__aeabi_idivmod:
    push    {r2, r3, r4, r5, lr}

    cmp     r1, #0
    beq     __div0_handler

    mov     r2, r0      // save numerator
    mov     r3, r1      // save denominator

    eor     r4, r0, r1  // result sign
    cmp     r0, #0
    rsblt   r0, r0, #0
    cmp     r1, #0
    rsblt   r1, r1, #0

    bl      __aeabi_uidivmod

    // r0 = quotient, r1 = remainder
    cmp     r4, #0
    rsblt   r0, r0, #0          // correct sign of quotient
    cmp     r2, #0
    rsblt   r1, r1, #0          // correct sign of remainder

    pop     {r2, r3, r4, r5, pc}
.size __aeabi_idivmod, . - __aeabi_idivmod

// -----------------------------------------------------------------------------
// Default divide-by-zero handler
.global __div0_handler
.type __div0_handler, %function
__div0_handler:
    mov     r0, #0
    bx      lr
.size __div0_handler, . - __div0_handler
