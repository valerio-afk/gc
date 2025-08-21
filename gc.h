/*
 * ============================================================================
 *  Garbage Collector (Mark & Sweep) - GPLv3 License
 * ============================================================================
 *
 *  Copyright (C) 2025 Valerio AFK
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * ============================================================================
 *
 *  File: <gc.h>
 *
 */



#ifndef _GC_H_
#define _GC_H_

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#define GC_TAG_ENTRY "___GC__ENTRY___"
#define GC_TAG_STATE "___GC__STATE___"

#define GC_ALLOC_THRESHOLD 128

#define GC_SCAN_STACK 0b1
#define GC_SCAN_HEAPS 0b10
#define GC_SCAN_DATA_SECTION 0b100
#define GC_SCAN_BSS_SECTION 0b1000
#define GC_SCAN_REGISTERS 0b10000
#define GC_SCAN_ALL_GLOBALS (GC_SCAN_DATA_SECTION | GC_SCAN_BSS_SECTION)
#define GC_SCAN_ALL_MEMORY (GC_SCAN_STACK | GC_SCAN_HEAPS | GC_SCAN_ALL_GLOBALS )
#define GC_SCAN_EVERYTHING (GC_SCAN_ALL_MEMORY | GC_SCAN_REGISTERS)
#define GC_SCAN_ALL_MEMORY_EXCEPT_HEAPS (GC_SCAN_ALL_MEMORY & ~GC_SCAN_HEAPS)
#define GC_SCAN_EVERYTHING_EXCEPT_HEAPS (GC_SCAN_EVERYTHING & ~GC_SCAN_HEAPS)

#define SIZE_T_MAX_DIGITS ((sizeof(size_t) * CHAR_BIT * 302) / 1000 + 1)

/*
  This struct maintains information regarding an allocation
  performed with gc_alloc.

  The entries are organised with a doubled linked list.
*/
typedef struct _gc_entry
{
    char _tag[sizeof(GC_TAG_ENTRY)]; //Unique tag marking this struct - useful to skip it when scanning the heap.
    void * ptr;                      //pointer to the allocated memory
    size_t size;                     //length of the allocation
    bool reachable;                  //TRUE if it's reachable, FALSE otherwise - used by gc_collect
    void * reach_addr;               //it specifies where in memory is reachable. NULL means in the CPU registers
    struct _gc_entry * next;         //Next entry
    struct _gc_entry * prev;         //Previous entry
} gc_entry;

/*
  Keeps information about memory regions (eg start and end of heap)
*/
typedef struct _mem_region
{
    void * start;
    void * end;
} memory_region;


/*
    Main struct of the garbage collector to keep its state.
*/
typedef struct _gc_state
{
    char _tag[sizeof(GC_TAG_ENTRY)]; //Unique tag marking this struct - useful to skip it when scanning the heap.
    void * stack_start; //address to the beginning of the stack
    memory_region data; //addresses of start/end of data section
    memory_region bss;  //addresses of start/end of bss section
    #if defined(__APPLE__) && defined(__MACH__)
        memory_region common; //sometimes uninitialised variables can end up in the common section (just for macOS)
    #endif
    gc_entry* head;     //head of the linked list with allocated memory information
    size_t allocations; //number of total allocations
    size_t threshold;   //threshold of allocations to trigger gc_collect
    uint8_t flags;      //garbage collector flags
} gc_state;


/*
The following code contains a struct and ASM code to save a snapshot of the registers
Current implementation supports intel x86 both 32 and 64bits, as well as arm both 32 and 64bits

The structs have the same name (arch_regs) and just one is selected at compile time,
together with the macro SAVE_GP_REGISTERS
*/

// x86 32-bit
#if defined(__i386__)
typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t esp;
} arch_regs;

#define ARCH_REG_STACK_POINTER esp

#define SAVE_GP_REGISTERS()                 \
    do {                                    \
        uint32_t tmp_esp, tmp_ebp;      \
        __asm__ __volatile__ (              \
            "movl %%eax, %0\n\t"           \
            "movl %%ebx, %1\n\t"           \
            "movl %%ecx, %2\n\t"           \
            "movl %%edx, %3\n\t"           \
            "movl %%esi, %4\n\t"           \
            "movl %%edi, %5\n\t"           \
            : "=m"(gp_registers.eax),      \
              "=m"(gp_registers.ebx),      \
              "=m"(gp_registers.ecx),      \
              "=m"(gp_registers.edx),      \
              "=m"(gp_registers.esi),      \
              "=m"(gp_registers.edi)       \
            :                               \
            : "memory");                    \
        __asm__ ("movl %%esp, %0" : "=r"(tmp_esp)); \
        __asm__ ("movl %%ebp, %0" : "=r"(tmp_ebp)); \
        gp_registers.esp = tmp_esp;        \
        gp_registers.ebp = tmp_ebp;        \
    } while (0)


// x86-64
#elif defined(__x86_64__)

typedef struct {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
} arch_regs;

#define ARCH_REG_STACK_POINTER  rsp

#define SAVE_GP_REGISTERS()                                \
    do {                                                   \
        long tmp_rsp, tmp_rbp;                             \
        __asm__ __volatile__ (                             \
            "movq %%rax, %0\n\t"                           \
            "movq %%rbx, %1\n\t"                           \
            "movq %%rcx, %2\n\t"                           \
            "movq %%rdx, %3\n\t"                           \
            "movq %%rsi, %4\n\t"                           \
            "movq %%rdi, %5\n\t"                           \
            "movq %%r8,  %6\n\t"                           \
            "movq %%r9,  %7\n\t"                           \
            "movq %%r10, %8\n\t"                           \
            "movq %%r11, %9\n\t"                           \
            "movq %%r12, %10\n\t"                          \
            "movq %%r13, %11\n\t"                          \
            "movq %%r14, %12\n\t"                          \
            "movq %%r15, %13\n\t"                          \
            : "=m"(gp_registers.rax),                      \
              "=m"(gp_registers.rbx),                      \
              "=m"(gp_registers.rcx),                      \
              "=m"(gp_registers.rdx),                      \
              "=m"(gp_registers.rsi),                      \
              "=m"(gp_registers.rdi),                      \
              "=m"(gp_registers.r8),                       \
              "=m"(gp_registers.r9),                       \
              "=m"(gp_registers.r10),                      \
              "=m"(gp_registers.r11),                      \
              "=m"(gp_registers.r12),                      \
              "=m"(gp_registers.r13),                      \
              "=m"(gp_registers.r14),                      \
              "=m"(gp_registers.r15)                       \
            :                                              \
            : "memory");                                   \
        __asm__ ("movq %%rsp, %0" : "=r"(tmp_rsp));        \
        __asm__ ("movq %%rbp, %0" : "=r"(tmp_rbp));        \
        gp_registers.rsp = tmp_rsp;                        \
        gp_registers.rbp = tmp_rbp;                        \
    } while (0)





// ARM 32-bit
#elif defined(__arm__)
typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11; // fp
    uint32_t r12; // ip
    uint32_t sp;
    uint32_t lr;
    //uint32_t pc;
} arch_regs;

#define ARCH_REG_STACK_POINTER sp

#define SAVE_GP_REGISTERS()                       \
    __asm__ __volatile__ (                   \
            "str r0,  [%0, #0]\n\t"             \
            "str r1,  [%0, #4]\n\t"             \
            "str r2,  [%0, #8]\n\t"             \
            "str r3,  [%0, #12]\n\t"            \
            "str r4,  [%0, #16]\n\t"            \
            "str r5,  [%0, #20]\n\t"            \
            "str r6,  [%0, #24]\n\t"            \
            "str r7,  [%0, #28]\n\t"            \
            "str r8,  [%0, #32]\n\t"            \
            "str r9,  [%0, #36]\n\t"            \
            "str r10, [%0, #40]\n\t"            \
            "str r11, [%0, #44]\n\t"            \
            "str r12, [%0, #48]\n\t"            \
            "str sp,  [%0, #52]\n\t"            \
            "str lr,  [%0, #56]\n\t"            \
            :                                   \
            : "r"(gp_registers_ptr)             \
            : "memory"                          \
        );                                     


// ARM 64-bit
#elif defined(__aarch64__)
typedef struct {
    uint64_t x0;
    uint64_t x1;
    uint64_t x2;
    uint64_t x3;
    uint64_t x4;
    uint64_t x5;
    uint64_t x6;
    uint64_t x7;
    uint64_t x8;
    uint64_t x9;
    uint64_t x10;
    uint64_t x11;
    uint64_t x12;
    uint64_t x13;
    uint64_t x14;
    uint64_t x15;
    uint64_t x16;
    uint64_t x17;
    uint64_t x18;
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t fp;  // x29
    uint64_t lr;  // x30
} arch_regs;

#define SAVE_GP_REGISTERS()                          \
   __asm__ __volatile__ (\
    "str x0,  [%0, #0]\n\t"\
    "str x1,  [%0, #8]\n\t"\
    "str x2,  [%0, #16]\n\t"\
    "str x3,  [%0, #24]\n\t"\
    "str x4,  [%0, #32]\n\t"\
    "str x5,  [%0, #40]\n\t"\
    "str x6,  [%0, #48]\n\t"\
    "str x7,  [%0, #56]\n\t"\
    "str x8,  [%0, #64]\n\t"\
    "str x9,  [%0, #72]\n\t"\
    "str x10, [%0, #80]\n\t"\
    "str x11, [%0, #88]\n\t"\
    "str x12, [%0, #96]\n\t"\
    "str x13, [%0, #104]\n\t"\
    "str x14, [%0, #112]\n\t"\
    "str x15, [%0, #120]\n\t"\
    "str x16, [%0, #128]\n\t"\
    "str x17, [%0, #136]\n\t"\
    "str x18, [%0, #144]\n\t"\
    "str x19, [%0, #152]\n\t"\
    "str x20, [%0, #160]\n\t"\
    "str x21, [%0, #168]\n\t"\
    "str x22, [%0, #176]\n\t"\
    "str x23, [%0, #184]\n\t"\
    "str x24, [%0, #192]\n\t"\
    "str x25, [%0, #200]\n\t"\
    "str x26, [%0, #208]\n\t"\
    "str x27, [%0, #216]\n\t"\
    "str x28, [%0, #224]\n\t"\
    "str x29, [%0, #232]\n\t"\
    "str x30, [%0, #240]\n\t"\
    :\
    : "r"(gp_registers_ptr)\
    : "memory"\
);

#else
    #error "Unsupported architecture"
#endif

extern arch_regs gp_registers;
extern arch_regs * gp_registers_ptr;


/*
This macro invokes SAVE_GP_REGISTERS before calling _gc_collect
This is important because we want to take a snapshot of the registers
before function invocation, that will alter the values of the registers for function invocation
*/
#define gc_collect(state) SAVE_GP_REGISTERS();_gc_collect(state)

gc_state* gc_init(uint8_t flags);
void gc_destroy(gc_state*);
void *gc_alloc(gc_state*,size_t,bool);
void *gc_realloc(gc_state*,void *, size_t);
void gc_free(gc_state*,void*);
void _gc_collect(gc_state*);
void * gc_current_stack_top();
void gc_mark (gc_state*,void *, void *,bool);
void gc_sweep(gc_state*);
void *gc_stack_base();
void gc_data_section(void**,void**);
void gc_bss_section(void**,void**);
void gc_print_state(gc_state*);
memory_region * gc_heap_regions(size_t *);

#endif