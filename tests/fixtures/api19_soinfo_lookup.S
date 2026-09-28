// Freestanding ARMv7 test payload. r0=soinfo*, r1=symbol name; returns the
// callable guest address or zero. Uses only the API19 ARM32 SysV view.
// Assemble: clang -target armv7-none-linux-gnueabi -c this-file -o lookup.o
// Extract .text using llvm-objcopy --only-section=.text -O binary, then encode
// its little-endian uint32 words into the companion api19_soinfo_lookup.inc.
.syntax unified
.arm
.text
.global api19_soinfo_lookup
.type api19_soinfo_lookup,%function
api19_soinfo_lookup:
    push {r4-r10, lr}
    mov r4, r0
    mov r5, r1
    mov r0, #0
1:  ldrb r2, [r1], #1
    cmp r2, #0
    beq 2f
    add r0, r2, r0, lsl #4
    and r3, r0, #0xf0000000
    eor r0, r0, r3, lsr #24
    bic r0, r0, r3
    b 1b
2:  ldr r6, [r4, #0xb4]
    cmp r6, #0
    beq 9f
    // Unsigned remainder, without an ARM hardware division requirement.
    mov r7, #0
    mov r8, #32
3:  lsls r0, r0, #1
    adc r7, r7, r7
    cmp r7, r6
    subcs r7, r7, r6
    subs r8, r8, #1
    bne 3b
    ldr r8, [r4, #0xbc]
    ldr r9, [r8, r7, lsl #2]
    ldr r10, [r4, #0xb8]
    mov r6, r10
4:  cmp r9, #0
    beq 9f
    cmp r9, r6
    bhs 9f
    ldr r7, [r4, #0xb0]
    add r7, r7, r9, lsl #4
    ldr r1, [r7]
    ldr r2, [r4, #0xac]
    add r1, r1, r2
    mov r2, r5
5:  ldrb r0, [r1], #1
    ldrb r3, [r2], #1
    cmp r0, r3
    bne 8f
    cmp r0, #0
    bne 5b
    ldrb r0, [r7, #12]
    lsr r0, r0, #4
    cmp r0, #1
    beq 6f
    cmp r0, #2
    bne 8f
6:  ldrh r0, [r7, #14]
    cmp r0, #0
    beq 8f
    ldr r0, [r7, #4]
    ldr r1, [r4, #0x11c]
    add r0, r0, r1
    pop {r4-r10, pc}
8:  ldr r8, [r4, #0xc0]
    ldr r9, [r8, r9, lsl #2]
    subs r10, r10, #1
    bne 4b
9:  mov r0, #0
    pop {r4-r10, pc}
.size api19_soinfo_lookup, .-api19_soinfo_lookup
