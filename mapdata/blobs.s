# Assembly file that includes all binary blobs we want to embed in code
# and defines symbols for them.

.section .rodata

.global blob_obstacles_start
.global blob_obstacles_end
blob_obstacles_start: .incbin "obstacles.bin"
blob_obstacles_end:
