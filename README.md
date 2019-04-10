# brainfuck-jit

Quick and dirty brainfuck jit compiler for x64 linux.

Machine code is built in memory from the brainfuck program. The memory region is made exectuable by a call to `mprotect` and is the code is run.

|Brainfuck|C|Machine code|
|---|---|---|
|`>`|`ptr++;`|`lea bx,[rbx+1]`|
|`<`|`ptr--;`|`lea bx,[rbx-1]`|
|`+`|`tape[ptr]++;`|`add BYTE PTR [r12+rbx],1`|
|`-`|`tape[ptr]--;`|`sub BYTE PTR [r12+rbx],1`|
|`.`|`putchar(tape[ptr]);`|`syscall`|
|`[`|`while(tape[ptr]){`|`cmp BYTE PTR [r12+rbx],0x0; jz loop_end`|
|`]`|`}`|`cmp BYTE PTR [r12+rbx],0x0; jnz loop_start`|
|`#`|`breakpoint`|`int 3`|

## Optimizations
- Adjacent `>`, `<`, `+` or `-` are combined. For example `>>>` would become `lea bx,[rbx+3]`
- The cmp part of `[` and `]` is skipped if the zero flag is still valid from a `+` or `-`
- The tape is 65536 (2^16) bytes long and the tape pointer is stored in `bx` (16 bits), enabling quick wrapping
- `[-]` and `[+]` emit `mov BYTE PTR [r12+rbx],0x0`
