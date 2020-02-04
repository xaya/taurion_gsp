# Assembly file that includes the roconfig binary proto file embedded
# into code.

.section .rodata

.global blob_roconfig_start
.global blob_roconfig_end
.align 1
blob_roconfig_start: .incbin "roconfig.pb"
blob_roconfig_end:
