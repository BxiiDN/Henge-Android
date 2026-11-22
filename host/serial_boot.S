/* Test firmware authored for this project. No Apple boot assets. */
.section .rodata
.balign 16
.global hv_serial_rom
.global hv_serial_rom_end
.global hv_serial_rom_stop
/* These addresses belong to this blob, never to an ELF PLT/GOT interposer. */
.hidden hv_serial_rom
.hidden hv_serial_rom_end
.hidden hv_serial_rom_stop
hv_serial_rom:
    mov x0, #0x70000000
    mov x1, #0x1234
    str x1, [x0]
    ldr x2, [x0]
    cmp x1, x2
    b.ne failed
    /* Ask the real boot disk device to read sector 0 into guest RAM. */
    mov x3, #0x70000000
    add x3, x3, #0x1000
    adr x4, request
    ldp x5, x6, [x4]
    stp x5, x6, [x3]
    ldp x5, x6, [x4, #16]
    stp x5, x6, [x3, #16]
    ldp x5, x6, [x4, #32]
    stp x5, x6, [x3, #32]
    mov x0, #0x70000000
    add x0, x0, #0x2000
    stp xzr, xzr, [x0]
    mov x0, #0x70000000
    add x0, x0, #0x4000
    mov w1, #255
    strb w1, [x0]
    mov x0, #0x30000000
    str x3, [x0, #0x408]
    mov x0, #0x70000000
    add x0, x0, #0x4000
    ldrb w1, [x0]
    cbnz w1, failed
    mov x0, #0x70000000
    add x0, x0, #0x3000
    ldr w1, [x0]
    mov w2, #0x4548
    movk w2, #0x474e, lsl #16 /* HENG */
    cmp w1, w2
    b.ne failed
    adr x1, passed_msg
    b print
failed:
    adr x1, failed_msg
print:
    mov x0, #0x20010000
1:  ldrb w2, [x1], #1
    cbz w2, .Lserial_stop
    str w2, [x0]
    b 1b
hv_serial_rom_stop:
.Lserial_stop:
    b .Lserial_stop
.balign 8
request:
    .quad 0x70002000
    .word 16, 0
    .quad 0x70003000
    .word 512, 0x00030001
    .quad 0x70004000
    .word 1, 0
passed_msg:
    .asciz "HENGE: RAM + DISK + UART PASS\n"
failed_msg:
    .asciz "HENGE: SELFTEST FAIL\n"
hv_serial_rom_end:
.section .note.GNU-stack,"",%progbits
