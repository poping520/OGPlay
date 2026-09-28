.syntax unified
.arm
.text
.global entry
entry:
 push {r4,lr}
 mov r4,r0
 add r1,r4,#192
 str r1,[r4,#28]
 ldr r3,[r4]
 adr r2,callbacks
 mov r1,#0
1:
 str r2,[r3,r1,lsl #2]
 add r2,r2,#8
 add r1,r1,#1
 cmp r1,#16
 bne 1b
 mov r1,#0
 str r1,[r3,#8] // no save-state callback: Java must return null
 ldr r0,[r4,#8]
 ldr r1,[r4,#12]
 mov r2,r1
 ldr r3,[r0]
 ldr r3,[r3,#96] // IsSameObject slot 24
 blx r3
 str r0,[r4,#108]
 pop {r4,pc}
callbacks:
 .set i,0
 .rept 16
 mov r2,#i
 b record
 .set i,i+1
 .endr
record:
 ldr r3,[r0,#28]
 ldr r12,[r3]
 add r12,r12,#1
 str r12,[r3]
 str r2,[r3,#4]
 str r1,[r3,#8]
 bx lr

.org 256
save_fixture_state:
 ldr r3,[r0,#28]
 ldr r0,[r3,#12]
 ldr r3,[r3,#16]
 str r3,[r1]
 bx lr
free_fixture_state:
 mov r1,#0
 str r1,[r0]
 bx lr
