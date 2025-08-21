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
 *  File: <gc_test.c>
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <setjmp.h>
#include <assert.h>
#include "gc.h"

int * global_ptr = (void*)0x1;
int * global_bss_ptr;


typedef struct Obj
{
    struct Obj *ref;
    uint32_t value;
} Obj;

void test_longjmp(gc_state* state)
{
    int * ptr = gc_alloc(state, sizeof(int),false);
    *ptr = 74;
    jmp_buf env;

    printf("\nLong jump fn - ptr value: %d\n",*ptr);

    if (setjmp(env)==0)
    {
        gc_collect(state);
        printf("No jump branch - ptr value: %d\n",*ptr);
        longjmp(env,1);
    }
    else
    {
        gc_collect(state);
        printf("Jump branch - ptr value: %d\n",*ptr);
    }
    printf("Long jump fn ends. If no sweeping appears, all is good!\n");
}


int main()
{
    #ifdef __OPTIMIZE__
        gc_state* state = gc_init(GC_SCAN_EVERYTHING_EXCEPT_HEAPS);
        printf("It appears you are compiling with -O1 or higher.\n");
        printf("CPU registers will be also scanned\n");
        printf("This is a conservatory approach\n");
        printf("Some pointers may not be sweeped at first because their reference is in any of the registers\n");
        printf("The GC will work normally, although some pointers may be sweeped not at the time it's indicated in this test\n");
        printf("To check if the test is working properly, compile without optimisation (-O0).\n");
        printf("This is not a bug. Some pointers may stay in any of the CPU registers for longer, so it may take more than one collection.\n\n");
    #else
        gc_state* state = gc_init(GC_SCAN_ALL_MEMORY_EXCEPT_HEAPS);
    #endif

    printf("Reachable on stack\n");
    int * ptr = gc_alloc(state,sizeof(int),true);
    *ptr = 47;
    gc_collect (state);
    printf("Check if pointer is still accessible: %d\n",*ptr);

    printf("\nUnreachable on stack\n");
    ptr = NULL;
    gc_collect(state);
    printf("If sweeping appeared, all is good!\n");


    printf("\nReachable on stack (testing gc_realloc)\n");
    ptr = gc_alloc(state,sizeof(int),true);
    *ptr = 47;
    ptr = gc_realloc(state,ptr,sizeof(long int));
    gc_collect (state);
    printf("Check if pointer is still accessible: %d\n",*ptr);

    printf("\nUnreachable on stack\n");
    ptr = NULL;
    gc_collect(state);
    printf("If sweeping appeared, all is good!\n");


    printf("\nReachable on .data segment\n");
    global_ptr = gc_alloc(state,sizeof(int),true);
    *global_ptr = 47;
    gc_collect (state);
    printf("Check if pointer is still accessible: %d\n",*global_ptr);

    printf("\nUnreachable on .data segment\n");
    global_ptr = NULL;
    gc_collect(state);
    printf("If sweeping appeared, all is good!\n");

    printf("\nReachable on .bss segment\n");
    global_bss_ptr = gc_alloc(state,sizeof(int),true);
    *global_bss_ptr = 47;
    gc_collect (state);
    printf("Check if pointer is still accessible: %d\n",*global_bss_ptr);

    printf("\nUnreachable on .bss segment\n");
    global_bss_ptr = NULL;
    gc_collect(state);
    printf("If sweeping appeared, all is good!\n");



    printf("\nReachable on heap\n");
    int ** heap_ptr1 = gc_alloc(state,sizeof(int*),true);
    int * heap_ptr2 = gc_alloc(state,sizeof(int),true);
    *heap_ptr1 = heap_ptr2;
    *heap_ptr2 = 47;

    heap_ptr2 = NULL;
    gc_collect (state);
    printf("Check if pointer is still accessible: %d\n",**heap_ptr1);

    printf("\nUnreacheable on heap\n");
    *heap_ptr1=NULL;
    heap_ptr1 = NULL;
    gc_collect (state);
    printf("If sweeping appeared, all is good!\n");

    test_longjmp(state);
    gc_collect(state);
    printf("If sweeping appeared, all is good!\n");

    printf("\n\nStress test. Allocating an array of 16 pointer 1024 times");

    size_t * ptrs[16];

    for (int i = 0;i<16;i++)
    {
        for (int j = 0;j<16;j++)
        {
            ptrs[j] = gc_alloc(state,sizeof(size_t),true);
            *ptrs[j] = j*i;
        }
    }

    printf("\n\nStress test over - now you should see 16 sweeps\n");

    for (int j = 0;j<16;j++)
    {
        *ptrs[j] = 0;
        ptrs[j] = NULL;
    }

    gc_collect(state);
    printf("If the last allocation is still reachable, this may be something related to malloc reusing memory with stale values.\n");

    printf("\nCycle test\n");
    Obj *a = gc_alloc(state,sizeof(Obj),true);
    Obj *b = gc_alloc(state,sizeof(Obj),true);

    a->ref = b;
    b->ref = a;

    a->value = 47;
    b->value = 74;

    gc_collect(state);

    printf("The two objects should be still reachable.\n");
    printf("a->value = %d\n",a->value);
    printf("b->value = %d\n",b->value);

    printf("Now I delete their references.\n");
    a = NULL; b = NULL;
    gc_collect(state);
    printf("If two sweeping appeared, all is good!\n");

    printf("\nTest immediate unreachable\n");
    gc_alloc(state,1024,true);

    gc_collect(state);
    printf("If sweeping appeared, all is good!\n");

    printf("\n\nTest over - no sweeps beyond this point\n\n");

    gc_print_state(state);
    printf("If any allocations are present, they will be free'd in destroy\n");

    gc_destroy(state);
    return 0;
}

