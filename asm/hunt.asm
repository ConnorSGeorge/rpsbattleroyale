.const X 0x9000
.const Y 0x9001
.const DETECT_PRED 0x9010
.const DETECT_PREY 0x9011
.const DETECT_SELF 0x9012
.const MOVE_N 0x9020
.const MOVE_E 0x9021
.const MOVE_S 0x9022
.const MOVE_W 0x9023

!main_loop

    lod rA, [DETECT_PREY]  ; Load detection data for nearest prey
    set rB, rA

    and rB, 1             ; Extract bit 0 to check if prey is nearby
    cmp rB, 1             ; Compare bit 0 of rA to 1 (prey nearby)
    jne !main_loop        ; If no prey, continue looping

    shr rA, 1             ; Extract latitude direction (bit 1)
    set rB, rA
    and rB, 1             ; 1 = North, 0 = South
    cmp rB, 0             ; Compare latitude direction to 0 (South)
    je !move_south        ; Jump to move_south if equal

!move_north
    str [MOVE_N], rZ      ; Move North
    shr rA, 1             ; Extract horizontal direction (bit 2)
    set rB, rA
    and rB, 1             ; 1 = East, 0 = West
    cmp rB, 0             ; Compare horizontal direction to 0 (West)
    je !move_west         ; Jump to move_west if equal
    jmp !move_east        ; Otherwise, move East

!move_south
    str [MOVE_S], rZ      ; Move South
    shr rA, 1             ; Extract horizontal direction (bit 2)
    set rB, rA
    and rB, 1             ; 1 = East, 0 = West
    cmp rB, 0             ; Compare horizontal direction to 0 (West)
    je !move_west         ; Jump to move_west if equal
    jmp !move_east        ; Otherwise, move East

!move_east
    str [MOVE_E], rZ      ; Move East
    jmp !main_loop

!move_west
    str [MOVE_W], rZ      ; Move West
    jmp !main_loop
