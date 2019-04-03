#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/mman.h>

#define MAX_STACK 1000

#define emit(x) program[pc++] = x;

#define die(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); exit(EXIT_FAILURE); } while (0)

int main(int argc, char *argv[]) {
    if (argc != 2) {
        die("Usage: %s <file>", argv[0]);
    }

    char *filename;
    FILE *file;
    if (strcmp(argv[1], "-") == 0) {
        filename = "<stdin>";
        file = stdin;
    } else {
        filename = argv[1];
        file = fopen(filename, "rb");
    }

    char *source = NULL;
    size_t source_len = 0;
    size_t read;
    do {
        source = source ? realloc(source, source_len + 1024) : malloc(1024);
        if (source == NULL) {
            die("Error allocating memory: %s.", strerror(errno));
        }
        read = fread(source + source_len, 1, 1024, file);
        source_len += read;
    } while (read == 1024);

    if (ferror(file)) {
        die("Error reading file %s.", filename);
    }

    if (file != stdin) {
        fclose(file);
    }

    size_t pagesize = (size_t) sysconf(_SC_PAGESIZE);
    size_t program_len = pagesize;
    char *program = memalign(pagesize, program_len);
    if (program == NULL) {
        die("Error allocating aligned memory: %s.", strerror(errno));
    }
    size_t pc = 0;

    size_t stack[MAX_STACK];
    size_t sp = 0;

    // stack setup
    // push rbp
    emit(0x55);
    // mov rbp,rsp
    emit(0x48);
    emit(0x89);
    emit(0xe5);
    // sub rsp,0x10000
    emit(0x48);
    emit(0x81);
    emit(0xec);
    emit(0x00);
    emit(0x00);
    emit(0x01);
    emit(0x00);

    // rbx will be tape pointer (16bit)
    // xor rbx,rbx
    emit(0x48);
    emit(0x31);
    emit(0xdb);

    // r12 will be tape start
    // lea r12,[rbp-0x10000]
    emit(0x4c);
    emit(0x8d);
    emit(0xa5);
    emit(0x00);
    emit(0x00);
    emit(0xff);
    emit(0xff);

    // memset tape to 0
    // mov rdi,r12
    emit(0x4c);
    emit(0x89);
    emit(0xe7);
    // mov rcx,0x10000
    emit(0x48);
    emit(0xc7);
    emit(0xc1);
    emit(0x00);
    emit(0x00);
    emit(0x01);
    emit(0x00);
    // xor al,al
    emit(0x30);
    emit(0xc0);
    // rep stosb
    emit(0xf3);
    emit(0xaa);

    // can skip cmp if add/sub already set zf
    int need_cmp = 1;

    for (size_t i = 0; i < source_len; i++) {
        // if less than 100 bytes remain in program buffer, double it
        if (program_len - pc < 100) {
            char *new_program = memalign(pagesize, program_len * 2);
            if (new_program == NULL) {
                die("Error allocating aligned memory: %s.", strerror(errno));
            }
            memcpy(new_program, program, program_len);
            program_len *= 2;
            free(program);
            program = new_program;
        }
        int amount = 0;
        switch (source[i]) {
            case '<':
                amount = -1;
                while (i + 1 < source_len && source[i + 1] == '<' && amount > -128) {
                    amount--;
                    i++;
                }
                // lea bx,[rbx-amount]
                emit(0x66);
                emit(0x8d);
                emit(0x5b);
                emit(amount);
                need_cmp = 1;
                break;
            case '>':
                amount = 1;
                while (i + 1 < source_len && source[i + 1] == '>' && amount < 127) {
                    amount++;
                    i++;
                }
                // lea bx,[rbx+amount]
                emit(0x66);
                emit(0x8d);
                emit(0x5b);
                emit(amount);
                need_cmp = 1;
                break;
            case '+':
                amount = 1;
                while (i + 1 < source_len && source[i + 1] == '+' && amount < 255) {
                    amount++;
                    i++;
                }
                // add BYTE PTR [r12+rbx],amount
                emit(0x41);
                emit(0x80);
                emit(0x04);
                emit(0x1c);
                emit(amount);
                need_cmp = 0;
                break;
            case '-':
                amount = 1;
                while (i + 1 < source_len && source[i + 1] == '-' && amount < 255) {
                    amount++;
                    i++;
                }
                // sub BYTE PTR [r12+rbx],amount
                emit(0x41);
                emit(0x80);
                emit(0x2c);
                emit(0x1c);
                emit(amount);
                need_cmp = 0;
                break;
            case '.':
                // call = SYS_WRITE
                // mov rax, 1
                emit(0x48);
                emit(0xc7);
                emit(0xc0);
                emit(0x01);
                emit(0x00);
                emit(0x00);
                emit(0x00);
                // fd = STDOUT
                // mov rdi, rax
                emit(0x48);
                emit(0x89);
                emit(0xc7);
                // buf
                // lea rsi,[r12+rbx]
                emit(0x49);
                emit(0x8d);
                emit(0x34);
                emit(0x1c);
                // count
                // mov rdx,rax
                emit(0x48);
                emit(0x89);
                emit(0xc2);
                // syscall
                emit(0x0f);
                emit(0x05);
                break;
            case '[':
                if (i < source_len - 2 && (source[i + 1] == '-' || source[i + 1] == '+') && source[i + 2] == ']') {
                    // [-] and [+] set cell to 0
                    // mov BYTE PTR [r12+rbx],0x0
                    emit(0x41);
                    emit(0xc6);
                    emit(0x04);
                    emit(0x1c);
                    emit(0x00);
                    i += 2;
                } else {
                    if (need_cmp) {
                        // cmp BYTE PTR [r12+rbx],0x0
                        emit(0x41);
                        emit(0x80);
                        emit(0x3c);
                        emit(0x1c);
                        emit(0x00);
                        need_cmp = 0;
                    }
                    // jz loop_end
                    emit(0x0f);
                    emit(0x84);
                    pc += 4;
                    stack[sp++] = pc;
                    if (sp == MAX_STACK) {
                        die("Maximum stack depth exceeded.");
                    }
                }
                break;
            case ']':
                if (need_cmp) {
                    // cmp BYTE PTR [r12+rbx],0x0
                    emit(0x41);
                    emit(0x80);
                    emit(0x3c);
                    emit(0x1c);
                    emit(0x00);
                    need_cmp = 0;
                }
                // jnz loop_start
                emit(0x0f);
                emit(0x85);
                pc += 4;
                if (sp == 0) {
                    die("Unexpected loop end.");
                }
                size_t loop_start = stack[--sp];
                *(int *) (program + pc - 4) = (int) (loop_start - pc);
                *(int *) (program + loop_start - 4) = (int) (pc - loop_start);
                break;
            case '#':
                // int 3 (breakpoint)
                emit(0xcc);
                break;
            default:
                break;
        }
    }

    if (sp != 0) {
        die("Unterminated loop.");
    }

    // leave
    emit(0xc9);
    // ret
    emit(0xc3);

    if (mprotect(program, program_len, PROT_EXEC) == -1) {
        die("Error making program memory executable: %s.", strerror(errno));
    }

    ((void (*)()) program)();

    return EXIT_SUCCESS;
}