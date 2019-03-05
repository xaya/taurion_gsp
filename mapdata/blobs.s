# Assembly file that includes all binary blobs we want to embed in code
# and defines symbols for them.

.section .rodata

.global blob_obstacles_start
.global blob_obstacles_end
.align 1
blob_obstacles_start: .incbin "obstacles.bin"
blob_obstacles_end:

.global blob_region_xcoord_start
.global blob_region_xcoord_end
.align 2
blob_region_xcoord_start: .incbin "regionxcoord.bin"
blob_region_xcoord_end:

.global blob_region_ids_start
.global blob_region_ids_end
.align 1
blob_region_ids_start: .incbin "regionids.bin"
blob_region_ids_end:
