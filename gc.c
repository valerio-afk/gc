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
 *  File: <gc.c>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#if defined(_WIN32)
    #include <windows.h>
    #include <winnt.h>

    /*
        _gc_win32_get_data_section

        This function returns the start and end addresses of the some memory sections
        in Windows, such as, .data, and .bss.

        This function is currently untested

        Arguments:
            - [in] section: name of the section (eg., .data or .bss)
            - [out] start: pointer to pointer that will be populated with the start of the specified memory section
            - [out] end: pointer to pointer that will be populated with the end of the specified memory section
    */
    void _gc_win32_get_data_section(const char* section, void ** start, void ** end)
    {
        HMODULE hModule = GetModuleHandle(NULL);

            if (!hModule) return;
            
            PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hModule;
            PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hModule + dosHeader->e_lfanew);
            PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);

            for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++, section++) {
                if (strncmp((const char*)section->Name, section, 5) == 0) {
                    *start = (void*)((BYTE*)hModule + section->VirtualAddress);
                    *size = (BYTE*)*start + section->Misc.VirtualSize;
                    break;
                }
            }
    }
#else
    
    #include <pthread.h>
    
    #if defined(__linux__)
        /*
          The following builtin variables are linux-specific
          and will be populated with the start and end addresses of .data and .bss.
        */
        extern char __data_start;
        extern char _edata;
        extern char __bss_start;
        extern char _end;

        #define PROC_MAPS "/proc/self/maps"

    #elif defined(__APPLE__) && defined(__MACH__)
        #include <mach-o/getsect.h>
        #include <mach-o/dyld.h>

        /*
        _gc_macos_get_data_section

        This function returns the start and end addresses of the some memory sections
        in macOS, such as, .data, and .bss.

        This function is currently untested

        Arguments:
            - [in] section: name of the section (eg., .data or .bss)
            - [out] start: pointer to pointer that will be populated with the start of the specified memory section
            - [out] end: pointer to pointer that will be populated with the end of the specified memory section
    */
        void _gc_macos_get_data_section(const char* section, void **start, void ** end)
        {
            unsigned long section_size = 0;
            const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(0);
            void *section_start = getsectiondata(mh, "__DATA", section, &section_size);
            *start = section_start;
            *end = (char *) section_start + section_size;
        }
    #endif

#endif

#ifndef restrict
#  if defined(_MSC_VER)
#    define restrict __restrict
#  else
#    define restrict __restrict__
#  endif
#endif

#include "gc.h"

/* this struct contains a snapshot of the CPU registers */
arch_regs gp_registers = {0};
/* this variable contains the address of the previous struct - useful to minimise use of registers */
arch_regs * gp_registers_ptr = &gp_registers;

/*
    gc_memcmp

    This function is a reimplementation of the memcmp. Useful to stop when differences starts
    and it returns either true or false.

    Arguments:
        [in] a: first operand to compare
        [in] b: second operand to compare
        [in] n: maximum length of memory to compare

    Returns:
        TRUE if a==b (not the pointers, but their content up tp n), FALSE otherwise
*/
bool gc_memcmp(const char * restrict a, char* restrict b,size_t n)
{
    for (size_t j = 0;j<n;j++)
    {
        if (*(a+j) != *(b+j)) return false;
    }

    return true;
}

/*
    gc_init

    Initialise the state of the garbage collector

    Arguments:
        [in] flags: flags indicating what memory area to check.  Suitable flags are

            GC_SCAN_STACK 
            GC_SCAN_HEAPS 
            GC_SCAN_DATA_SECTION 
            GC_SCAN_BSS_SECTION 
            GC_SCAN_REGISTERS 
            GC_SCAN_ALL_GLOBALS (this incluides GC_SCAN_DATA_SECTION and GC_SCAN_BSS_SECTION)
            GC_SCAN_ALL_MEMORY (this includes everything except GC_SCAN_REGISTERS)
            GC_SCAN_EVERYTHING (this includes all of the above)

    Return:
        A pointer to the initialised state of the garbage collector.
        This should be free'd using gc_destroy.
*/
gc_state* gc_init(uint8_t flags)
{
    gc_state* state = malloc(sizeof(gc_state));
    if (state!=NULL)
    {
        strcpy((char*)&state->_tag,GC_TAG_STATE);
        state->stack_start = gc_stack_base();
        gc_data_section(&state->data.start, &state->data.end);
        gc_bss_section(&state->bss.start, &state->bss.end);
        state->head = NULL;
        state->threshold = GC_ALLOC_THRESHOLD;
        state->flags = flags;
    } 

    return state;
}

/*
    gc_destroy

    Deinitialise the state of the garbage collector. This function should be called
    at the end of the program or when the GC is not required anymore.
    All memory allocations performed with the GC still reachable will be free'd as well.

    Arguments:
        [in] state: the state of the garbage collector initialised with gc_init.
*/
void gc_destroy(gc_state* state)
{
    if (state != NULL)
    {
        gc_entry *itm = state->head;
        gc_entry *tmp;

        while(itm != NULL)
        {
            tmp = itm->next;
            free(itm->ptr);
            free(itm);
            itm=tmp;
        }

        free(state);
    }
}

/*
    gc_alloc

    It allocates memory using malloc and records allocation data.

    Arguments:
        [in] state: the state of the garbage collector initialised with gc_init.
        [in] sz: number of bytes to allocate
        [in] init: if TRUE, the allocated memory will be zero'd.

    Return:
        See malloc.

*/
void *gc_alloc(gc_state* state,size_t sz,bool init)
{
    void * ptr = malloc(sz);

    if (ptr!=NULL)
    {
        if (init) memset(ptr,0,sz);

        gc_entry * new_entry = malloc(sizeof(gc_entry));

        if (new_entry!=NULL)
        {
            strcpy((char*)&new_entry->_tag,GC_TAG_ENTRY);
            new_entry->ptr = ptr;
            new_entry->prev = NULL;
            new_entry->next = state->head;
            new_entry->size = sz;
            new_entry->reachable = false;
            if (state->head != NULL) state->head->prev = new_entry;
            state->head = new_entry;

            state->allocations++;

            if ((state->threshold>0) && ((state->allocations%state->threshold) == 0))
            {
                gc_collect(state);
            }
        }
        else //we couldn't allocate memory for the gc entry - so let's free everything else
        {
            free(ptr);
            ptr = NULL;
        }
    }

    return ptr;
}

void *gc_realloc(gc_state* state,void *ptr, size_t sz)
{
    if (ptr == NULL) return gc_alloc(state,sz,false);
    if (sz == 0)
    {
        gc_free(state,ptr);
        return NULL;
    }

    gc_entry *entry = state->head;

    while (entry != NULL)
    {
        if (entry->ptr == ptr)
        {
            entry->ptr = realloc(entry->ptr,sz);
            return entry->ptr;
        }
        entry = entry->next;
    }

    return NULL; //we shouldn't ever get here - it's to silence the compiler
}

/*
    _gc_collect

    This function invokes mark 'n' sweep operations.
    This function should not be called - use the gc_collect macro as it gets a snapshots of the CPU registers

    Arguments:
        [in] state: the state of the garbage collector initialised with gc_init.
*/
void _gc_collect(gc_state*state)
{
    size_t n_heaps = 0;
    gc_entry * entry = state->head;

    //set all the memory allocation to unreachable    
    while (entry!=NULL)
    {
        entry->reachable = false;
        entry->reach_addr = NULL;

        /*
            for optimisation purposes, we can check at this stage if the current
            entry is reachable in the CPU registers
        */
        if ((state->flags & GC_SCAN_REGISTERS) != 0)
        {
            char * regs = (char*)gp_registers_ptr;
            //scan registers to see if the pointer is reachable
            for (size_t t = 0; t<sizeof(arch_regs);t+=sizeof(UINTPTR_MAX))
            {
                if ( *(void**)(regs+t) == entry->ptr )
                {
                    entry->reachable = true;
                    #ifdef DEBUG
                        printf("Pointer %p is reachable in registers\n",entry->ptr);
                    #endif
                }
            }
        }


        entry = entry->next;
    }

    void * stack_top = gc_current_stack_top();

    //scan the stack
    if ((state->flags & GC_SCAN_STACK) != 0) gc_mark(state,stack_top,state->stack_start,false);
    //scan .data section
    if ((state->flags & GC_SCAN_DATA_SECTION) != 0) gc_mark(state,state->data.start,state->data.end,false);
    //scan .bss section
    if ((state->flags & GC_SCAN_BSS_SECTION) != 0) gc_mark(state,state->bss.start,state->bss.end,false);

    //scan all the heaps
    if ((state->flags & GC_SCAN_HEAPS) != 0)
    {
        memory_region * heaps = gc_heap_regions(&n_heaps);
        for (size_t t = 0;t<n_heaps;t++) gc_mark(state,heaps[t].start,heaps[t].end,true);
        free(heaps);
    }

    //after the marking, we start with the sweep operation
    gc_sweep(state);
}


/*
    gc_sweep

    Sweep operation. Free all allocated memory marked as unreachable.

    Arguments:
        [in] state: the state of the garbage collector initialised with gc_init.
*/
void gc_sweep(gc_state*state)
{
    gc_entry * entry = state->head;
    gc_entry *next;

    while (entry!=NULL)
    {
        next = entry->next;
        if (entry->reachable == false)
        {
            if (entry->prev != NULL)
            {
                entry->prev->next = entry->next;
            }
            else state->head = entry->next;

            if (entry->next != NULL)
            {
                entry->next->prev = entry->prev;
            }

            #ifdef DEBUG
                printf("Sweeping %p\n",entry->ptr);
            #endif

            free(entry->ptr);
            free(entry);
        }
        entry = next;
    }

}

/*
    gc_free

    Manually free memory allocated with gc_alloc

    Arguments:
        [in] state: the state of the garbage collector initialised with gc_init.
        [in] ptr: pointer to allocated memory to free
*/
void gc_free(gc_state*state, void*ptr)
{
    if (ptr == NULL) return;

    gc_entry * entry = state->head;
    gc_entry *next;

    //search for the pointer in the linked list
    while(entry!=NULL)
    {
        next = entry->next;
        if (entry->ptr == ptr)
        {
            //once it's found, it updates the linked list references.
            if (entry->prev != NULL)
            {
                entry->prev->next = entry->next;
            }
            else state->head = entry->next;

            if (entry->next != NULL)
            {
                entry->next->prev = entry->prev;
            }

            //free both the allocated memory and the struct gc_entry
            free(entry->ptr);
            free(entry);
        }
        entry = next;
    }
}

/*
    gc_mark

    Mark operation. Check what pointers are still reachable in the provided memory areas

    Arguments:
        [in] state: the state of the garbage collector initialised with gc_init.
        [in] start: address to where to start the mark operation
        [in] end: address to where to end the mark operation
        [in] check_tags: if TRUE, it checks the tags of the allocated GC struct data and skip them. When scanning the heap, this should be true.
*/
void gc_mark (gc_state*state,void *start, void *end, bool check_tags)
{
    end-=sizeof(void*);
    while (start <=end)
    {
        gc_entry *e = state->head;
        while(e!=NULL)
        {
            if (check_tags)
            {
                if (gc_memcmp(start,GC_TAG_STATE,sizeof(GC_TAG_STATE))) start += sizeof(gc_state) -1;
                else if (gc_memcmp(start,GC_TAG_ENTRY,sizeof(GC_TAG_ENTRY))) start += sizeof(gc_entry) -1;
                
            }
            if ((e->ptr == *(void**)(start)) && (e->reachable == false))
            {
                e->reachable = true;

                #ifdef DEBUG
                    printf("Pointer %p is reachable\n",e->ptr);
                #endif
                e->reach_addr = start;
                gc_mark(state,e->ptr,e->ptr+e->size,true);
            }
            e = e->next;
        }
        start+=sizeof(void*);
    }
}

/*
    gc_stack_base

    Returns the base of the stack.
    This function has different implementation wrt the operating system.

    The way it works is to scan several stack frames from to to bottom and returns the lowest valid address.
    This is a good proxy for the bottom of the stack.

    Returns:
        Pointer to the start of the stack section.
*/
void *gc_stack_base() 
{
    
  #ifdef _MSC_VER
    void* stackFrames[5] = { 0 };
    USHORT framesCaptured = RtlCaptureStackBackTrace(0, 5, stackFrames, NULL);

    // Return the deepest valid frame pointer found, starting from the last
    for (int i = framesCaptured - 1; i >= 0; i--) {
        if (stackFrames[i] != NULL) {
            return stackFrames[i];
        }
    }

    return NULL;
  #elif defined(__GNUC__) || defined(__clang__)
    pthread_attr_t attr;
    void *stack_addr;
    size_t stack_size;

    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &stack_addr, &stack_size);
    pthread_attr_destroy(&attr);

    return stack_addr + stack_size;

  #else
    return NULL;
  #endif

}

/*
    gc_data_section

    Returns the addresses of the start and end of the data section
    This implementation depends on the operating system.

    Arguments:
        [out] start: beginning of the data section
        [out] end: end of the data section
*/
void gc_data_section(void ** start, void ** end)
{
    *start = NULL;
    *end = NULL;

    #if defined(__linux__)
        *start = (void *)&__data_start;
        *end = (void *) &_edata;
    #elif defined(__APPLE__) && defined(__MACH__)
        _gc_macos_get_data_section("__data",start,end);
    #elif defined(_WIN32)
        _gc_win32_get_data_section(".data",start,end);
    #endif
}

/*
    gc_bss_section

    Returns the addresses of the start and end of the bss section
    This implementation depends on the operating system.

    Arguments:
        [out] start: beginning of the bss section
        [out] end: end of the bss section
*/
void gc_bss_section(void ** start, void ** end)
{
    *start = NULL;
    *end = NULL;

    #if defined(__linux__)
        *start = (void *)&__bss_start;
        *end = (void *) &_end;
    #elif defined(__APPLE__) && defined(__MACH__)
        _gc_macos_get_data_section("__bss",start,end);
    #elif defined(_WIN32)
        _gc_win32_get_data_section(".bss",start,end);
    #endif
}

/*
    gc_heap_regions

    Returns an alloc'd array of memory_region structs indicating the start and end of several heap regions.
    This implementation depends on the operating system.

    macOs and Windows implementations are untested.

    Arguments:
        [out] n: number of heap regions found. If not provided, NULL will be returned

    Return:
        Array of memory_region's. This is allocated with malloc and needs to be free'd with std free function.
        If there are no heap regions (or you are compiling this in an not-supported platform), NULL will be returned.

*/

#if defined(__linux__)
/*
    Linux implementation works by parsing /proc/self/maps
*/

#if UINTPTR_MAX == 0xffffffff
    /*32BIT*/
    #define MAPS_PATTERN "%x-%x %4s %x %5s %d %63[^\n]"
#elif UINTPTR_MAX == 0xffffffffffffffff
    /*64bit*/
    #define MAPS_PATTERN "%lx-%lx %4s %lx %5s %d %63[^\n]"
    
#else
    #error "Unknown architecture"
#endif

memory_region * gc_heap_regions(size_t * n)
{
    if (n==NULL) return NULL;

    *n = 0;
    FILE* fp = fopen(PROC_MAPS, "r");
    if (!fp) return NULL;

    char line[256];
    memory_region * regions = NULL;

    size_t start, end;
    char perms[5], dev[6], mapname[64];
    size_t offset;
    int inode;
    bool is_heap;

    while (fgets(line, sizeof(line), fp)) 
    {
        is_heap = false;

        int n_char = sscanf(line, MAPS_PATTERN,
                       &start, &end, perms, &offset, dev, &inode, mapname);

        if (perms[0] != 'r' || perms[1] != 'w' || perms[3] != 'p')
            continue;

        if (strcmp(mapname, "[heap]") == 0) is_heap = true;
        else if (strlen(mapname) == 0) is_heap = true;
        else if ((mapname[0]=='[') && (strstr(mapname,"anon")!=NULL)) is_heap = true;
        

        if (is_heap) 
        {
            regions = realloc(regions, (*n + 1) * sizeof(memory_region));
            regions[*n].start = (void*)start;
            regions[*n].end = (void*)end;
            (*n)++;
        }
    }

    fclose(fp);

    return regions;
}
#elif defined(__APPLE__) && defined(__MACH__)
/*
    macOs implementation works by parsing asking the kernel (I think) to provide certain memory regions
*/
memory_region * gc_heap_regions(size_t * n)
{
    if (n == NULL) return NULL;
    *n = 0;
    memory_region * regions = NULL;

    mach_port_t task = mach_task_self();
    mach_vm_address_t address = 0;
    mach_vm_size_t size = 0;
    natural_t depth = 0;
    vm_region_submap_info_data_64_t info;
    mach_msg_type_number_t info_count = VM_REGION_SUBMAP_INFO_COUNT_64;
    kern_return_t kr;

    while (1) {
        info_count = VM_REGION_SUBMAP_INFO_COUNT_64;
        kr = mach_vm_region_recurse(task,
                                    &address,
                                    &size,
                                    &depth,
                                    (vm_region_info_t)&info,
                                    &info_count);
        if (kr != KERN_SUCCESS)
            break;

        // Filter: writable, private, not a submap
        if ((info.protection & VM_PROT_WRITE) &&
            info.share_mode == SM_PRIVATE &&
            info.is_submap == 0) {

            regions = realloc(regions, (*n + 1) * sizeof(memory_region));
            if (regions == NULL) return NULL;

            regions[*n].start = (void*)address;
            regions[*n].end = (void*)(address + size);
            (*n)++;
        }

        address += size;
    }

    return regions;
}
#elif defined(_WIN32)

/*
    Windows implementation works by parsing asking the kernel (I think) to provide certain memory regions
    Similar to the macOs version in concept, but with Win32 API
*/

memory_region * gc_heap_regions(size_t * n)
{
    if (n == NULL) return NULL;
    *n = 0;
    memory_region * regions = NULL;

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    char* addr = (char*)sysInfo.lpMinimumApplicationAddress;

    while (addr < sysInfo.lpMaximumApplicationAddress) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0)
            break;

        // Only committed and private pages, with read-write access
        if (mbi.State == MEM_COMMIT &&
            mbi.Type == MEM_PRIVATE &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {

            regions = realloc(regions, (*n + 1) * sizeof(memory_region));
            if (regions == NULL) return NULL;

            regions[*n].start = mbi.BaseAddress;
            regions[*n].end = (void*)((char*)mbi.BaseAddress + mbi.RegionSize);
            (*n)++;
        }

        addr += mbi.RegionSize;
    }

    return regions;
}

#else
/*
  If you are doing this in a no-supported system, NULL will be returned.
*/
memory_region * gc_heap_regions(size_t *n)
{
    if (n!=NULL) *n = 0;
    return NULL;
}

#endif

void * gc_current_stack_top()
{
    #if defined(__aarch64__)
        /*
        I prefer doing in this way rather than modifying the SAVE_GP_REGISTERS because sp is not
        a general purpose register in aarch64 and cannot be used as x0-31.
        */
        void * sp_val;
        __asm__ volatile (
            "mov %0, sp"
            : "=r" (sp_val)   // output operand
        );
        return sp_val + 16; //I add 16 because the call to gc_currect_stack_top pushes sp and lr on the stack (16bytes)
    #else
        return (void *)gp_registers.ARCH_REG_STACK_POINTER;
    #endif
}