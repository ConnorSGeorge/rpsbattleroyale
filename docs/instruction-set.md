# RPS Assembly Instruction Set

This guide explains the assembly language used by uploaded bot files.

Use it to write your own .asm bots, compile them with TNASM, and upload the compiled result into RPS.

## Source file basics

A bot source file is a plain text .asm file.

Common syntax features:

- `!label` for jump targets
- `.const NAME value` for named constants
- `;` style comments are not used here, but any text ignored by TNASM can be left as documentation in your source as long as it does not break parsing
- Registers are named `rA`, `rB`, `rC`, `rD`, `rE`, and `rZ`
- `rZ` is always zero

## Memory model

Bots run in a 16-bit address space with registers and memory-mapped I/O.

### General registers

- `rA`, `rB`, `rC`, `rD`, `rE`: general-purpose registers
- `rZ`: constant zero register

### Useful bot I/O addresses

These are the addresses most often used by RPS bots:

#### Reads

- `X` = current X position
- `Y` = current Y position
- `DETECT_PREY` = find the nearest bot this bot can beat
- `DETECT_PRED` = find the nearest bot that can beat this bot
- `DETECT_SELF` = find the nearest bot of the same type

#### Writes

- `MOVE_N` = move north
- `MOVE_E` = move east
- `MOVE_S` = move south
- `MOVE_W` = move west

## How bots usually work

A typical bot loop does this:

1. Read its position or nearby bot information.
2. Decide whether to move.
3. Write to one of the movement addresses.
4. Jump back to the top of the loop.

## Official instruction reference

RPS uses the TeenyAT instruction set. Instead of duplicating the full instruction catalog here, use the official docs:

- Main instruction docs index: https://github.com/miniat-amos/TeenyAT/tree/main/docs
- Per-instruction pages: https://github.com/miniat-amos/TeenyAT/tree/main/docs/instructions

Use those pages for opcode behavior, flag effects, and instruction syntax details.

## Flags

Many instructions update the CPU flags.

The most important flags for bot logic are:

- equal
- less than
- greater than

Use `cmp` before conditional jumps when you need a clear branch decision.

## Writing a simple movement bot

The following bot moves east forever:

```asm
.const MOVE_E 0x9021

!start
    str [MOVE_E], rZ
    jmp !start
```

A bot can also inspect its own position before deciding where to go:

```asm
.const X 0x9000
.const Y 0x9001
.const MOVE_E 0x9021

!start
    lod rA, [X]
    lod rB, [Y]
    str [MOVE_E], rZ
    jmp !start
```

## Tips for players

- Keep the main loop small.
- Use `lod` to read the bot’s state.
- Use `cmp` and the jump instructions to make decisions.
- Use `str` to move.
- Test one bot at a time before uploading more complex logic.

## Further reference

For the authoritative instruction documentation, use the TeenyAT docs repository pages above.
