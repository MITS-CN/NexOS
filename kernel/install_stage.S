.section .rodata
.global stage1_data_start
.global stage1_data_end
.global stage2_data_start
.global stage2_data_end

.align 4
stage1_data_start:
    .incbin "build/stage1.bin"
stage1_data_end:

.align 4
stage2_data_start:
    .incbin "build/stage2.bin"
stage2_data_end: