#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "gc.h"

#ifdef GC_DEBUG
#include <stdio.h>
#endif

collector*
gc_new(){
    collector* gc = malloc(sizeof(collector));
    gc->gc_ref_count = 0;
    gc->gc_free_chunk = 0;
    return gc;
}

static void
set_ref_block(collector* gc, int index, void* start, void* end){
    gc->gc_refs[index][0] = start;
    gc->gc_refs[index][1] = end;
}

void
gc_add_ref(collector* gc, void** begin, void** end){
    if(begin == end){
        return;
    }

    if(begin > end){
        void** t = begin;
        begin = end;
        end = t;
    }

    for(int i = 0; i < gc->gc_ref_count; i++){
        if(begin >= gc->gc_refs[i][0] && end <= gc->gc_refs[i][1]){
            return;
        }
    }

    for(int i = 0; i < gc->gc_ref_count; i++){
        if(gc->gc_refs[i][0] == 0){
            set_ref_block(gc, i, begin, end);
            return;
        }
    }

    set_ref_block(gc, gc->gc_ref_count++, begin, end);
    return;
}

void
gc_remove_ref(collector* gc, void* begin, void* end){
    if(begin == end){
        return;
    }

    if(begin > end){
        void** t = begin;
        begin = end;
        end = t;
    }

    int i;
    for(i = 0; i < gc->gc_ref_count - 1; i++){
        if((void**) begin >= gc->gc_refs[i][0] && (void**) end <= gc->gc_refs[i][1]){
            gc->gc_refs[i][0] = 0;
            return;
        }
    }

    if((void**) begin >= gc->gc_refs[i][0] && (void**) end <= gc->gc_refs[i][1]){
        gc->gc_ref_count--;
    }
}

void
gc_mark_chunk(collector* gc, byte* chunk){
    MARK_CHUNK(chunk, 1);
    for(int i = 0; i < PTR_INDEX(CHUNK_SIZE(chunk)); i++){
        if(REF_PTR_MIN(gc, *BITS_AT(chunk, i))){
            byte* ptr = *BITS_AT(chunk, i);
            byte* ref = MINOR_CHUNK(gc, ptr);
            if(!MARKED(ref)){
                gc_mark_chunk(gc, ref);
            }
        }
    }
}

static byte*
find_major_chunk(collector* gc, byte* ptr){
    byte* curr;
    for(curr = &gc->gc_major_heap[0]; !(ptr >= curr && (ptr < (curr + CHUNK_SIZE(curr)))) && curr < &gc->gc_major_heap[GC_MAJHEAP_SIZE]; curr = curr + CHUNK_SIZE(curr));
    if(curr < &gc->gc_major_heap[GC_MAJHEAP_SIZE]){
        return curr;
    }
    return 0;
}

static void
mark_major_chunk(collector* gc, byte* chunk){
    if(CHUNK_FLAGS(chunk) != GC_BLACK){
        MARK_CHUNK(chunk, CHUNK_FLAGS(chunk) + GC_BLACK);
        for(int i = 0; i < PTR_INDEX(CHUNK_SIZE(chunk)); i++){
            if(REF_PTR_MAJ(gc, *BITS_AT(chunk, i))){
                byte* ptr = *BITS_AT(chunk, i);
                byte* ref = find_major_chunk(gc, ptr);
                mark_major_chunk(gc, ref);
            }
        }
    }
}

void*
gc_minor_alloc(collector* gc, int size){
    if(size == 0){
        return 0;
    }
    size = (int) WITH_HEADER(ALIGN(size));
    if(size > GC_MINCHUNK_SIZE){
        return 0;
    }
    if(gc->gc_free_chunk >= GC_MINCHUNKS){
        gc_minor(gc);
        return gc_minor_alloc(gc, (int) WITHOUT_HEADER(size));
    }
    FLAGS(CHUNK_AT(gc, gc->gc_free_chunk)) = (unsigned int) size;
    return BITS(CHUNK_AT(gc, gc->gc_free_chunk++));
}

void*
gc_major_alloc(collector* gc, int size){
    if(size == 0){
        return NULL;
    }

    size = (int) WITH_HEADER(ALIGN(size));
    byte* curr;
    for(curr = &gc->gc_major_heap[0]; curr < &gc->gc_major_heap[GC_MAJHEAP_SIZE]; curr = curr + CHUNK_SIZE(curr)){
        if(CHUNK_FLAGS(curr) != GC_FREE || size > CHUNK_SIZE(curr)){
            break;
        }
    }

    if(curr >= &gc->gc_major_heap[GC_MAJHEAP_SIZE]){
        return 0;
    }

    byte* free_chunk = curr;
    unsigned int prev_size = CHUNK_SIZE(free_chunk);
    FLAGS(free_chunk) = prev_size;
    MARK_CHUNK(free_chunk, GC_WHITE);
    byte* next = (curr + size);
    FLAGS(next) = prev_size - size;
    return (void*) (WITH_HEADER(free_chunk));
}

static void
darken_chunk(collector* gc, byte* ptr){
    byte* curr = find_major_chunk(gc, ptr);
    if(curr != 0 && CHUNK_FLAGS(curr) == GC_WHITE){
        MARK_CHUNK(curr, GC_GRAY);
    }
}

static void
darken_roots(collector* gc){
    int counter;
    for(counter = 0; counter < gc->gc_ref_count; counter++){
        void** ref;
        for(ref = gc->gc_refs[counter][0]; ref < gc->gc_refs[counter][1]; ref++){
            if(ref != 0){
                darken_chunk(gc, *ref);
            }
        }
    }
}

static void
darken_major(collector* gc){
    byte* curr;
    for(curr = &gc->gc_major_heap[0]; curr != NULL && curr < &gc->gc_major_heap[GC_MAJHEAP_SIZE]; curr = curr + CHUNK_SIZE(curr)){
        switch(CHUNK_FLAGS(curr)){
            case GC_GRAY:{
                mark_major_chunk(gc, curr);
                break;
            };
            default:{
                break;
            }
        }
    }
}

static void
mark_minor(collector* gc){
    memset(gc->gc_backpatch, 0, sizeof(gc->gc_backpatch));
    int counter;
    for(counter = 0; counter < gc->gc_ref_count; counter++){
        void** ref;
        for(ref = gc->gc_refs[counter][0]; ref < gc->gc_refs[counter][1]; ref++){
            if(ref != 0){
                if(REF_PTR_MIN(gc, *ref)){
                    gc_mark_chunk(gc, MINOR_CHUNK(gc, *ref));
                }
            }
        }
    }
}

static void
backpatch_chunk(collector* gc, byte* chunk){
    int i;
    for(i = 0; i < PTR_INDEX(CHUNK_SIZE(chunk)); i++){
        if(REF_PTR_MIN(gc, *BITS_AT(chunk, i))){
            byte* ptr = *BITS_AT(chunk, i);
            byte* ref = MINOR_CHUNK(gc, ptr);
            if(MARKED(ref)){
                byte* new_ptr = (byte*) gc->gc_backpatch[CHUNK_OFFSET(gc, ref)];
                *BITS_AT(chunk, i) += (new_ptr - ref);
            }
        }
    }
}

static void
backpatch_refs(collector* gc){
    int counter;
    for(counter = 0; counter < gc->gc_ref_count; counter++){
        void** ref;
        for(ref = gc->gc_refs[counter][0]; ref < gc->gc_refs[counter][1]; ref++){
            if(ref != 0){
                if(POINTS_MINOR(gc, *ref)){
                    byte* chunk = MINOR_CHUNK(gc, *ref);
                    if(MARKED(chunk)){
                        byte* new_ptr = (byte*) gc->gc_backpatch[CHUNK_OFFSET(gc, chunk)];
                        if(new_ptr != 0){
                            *ref += (new_ptr - chunk);
                        }
                    }
                }
            }
        }
    }
}

static void
copy_minor_heap(collector* gc){
    int i;
    for(i = 0; i < gc->gc_free_chunk; i++){
        byte* chunk = CHUNK_AT(gc, i);
        void* ptr = WITHOUT_HEADER(gc_major_alloc(gc, (int) WITH_HEADER(CHUNK_SIZE(chunk))));
        gc->gc_backpatch[i] = ptr;
    }
    backpatch_refs(gc);
    for(i = 0; i < gc->gc_ref_count; i++){
        backpatch_chunk(gc, CHUNK_AT(gc, i));
    }
    for(i = 0; i < gc->gc_ref_count; i++){
        byte* chunk = CHUNK_AT(gc, i);
        if(MARKED(chunk)){
            byte* new = (byte*) gc->gc_backpatch[i];
            memcpy(new, chunk, CHUNK_SIZE(chunk));
        }
    }
}

void
gc_major(collector* gc){
    darken_roots(gc);
    darken_major(gc);
    for(byte* curr = &gc->gc_major_heap[0]; curr < &gc->gc_major_heap[GC_MAJHEAP_SIZE]; curr = curr + CHUNK_SIZE(curr)){
        switch(CHUNK_FLAGS(curr)){
            case GC_WHITE:{
                MARK_CHUNK(curr, GC_FREE);
                break;
            };
            default:{
                break;
            }
        }
    }
}

void
gc_minor(collector* gc){
    mark_minor(gc);
    copy_minor_heap(gc);
    gc->gc_free_chunk = 0;
}

void*
gc_alloc(collector* gc, int size){
    void* ptr = gc_minor_alloc(gc, size);
    if(ptr == 0){
        return gc_major_alloc(gc, size);
    }
    return ptr;
}

#ifdef GC_DEBUG
void
gc_print_minor(collector* gc){
    printf("*** List of minor heap allocated %d chunks\n", gc->gc_free_chunk);
    int i;
    for(i = 0; i < gc->gc_free_chunk; i++){
        byte* chunk = CHUNK_AT(gc, i);
        printf("\tChunk: %.4d\tsize: %.3d\tmarked: %c\n", (int) CHUNK_OFFSET(gc, chunk), CHUNK_SIZE(chunk), (MARKED(chunk) ? 'y' : 'n'));
    }
    printf("*** End of minor chunks list\n");
}

void
gc_print_refs(collector* gc){
    printf("*** List of stored references. %d references\n", gc->gc_ref_count);
    int counter;
    for(counter = 0; counter < gc->gc_ref_count; counter++){
        void** ref;
        for(ref = gc->gc_refs[counter][0]; ref < gc->gc_refs[counter][1]; ref++){
            if(ref != 0){
                if(gc->gc_refs[counter][0] != 0){
                    char* points_to = "??";
                    void* heap = 0;
                    if(POINTS_MINOR(gc, *gc->gc_refs[counter][0])){
                        points_to = "minor";
                        heap = gc->gc_minor_heap;
                    } else if(POINTS_MAJOR(gc, *gc->gc_refs[counter][0])){
                        points_to = "major";
                        heap = gc->gc_major_heap;
                    }

                    printf("\tReference pointing to: %0.8x(%s)\n", (unsigned int) (*gc->gc_refs[counter][0] - heap), points_to);
                } else{
                    printf("\tEmpty Slot\n");
                }
            }
        }
    }
    printf("*** End of stored references list\n");
}
#endif