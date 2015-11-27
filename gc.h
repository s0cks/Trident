#ifndef GGC_GC_H
#define GGC_GC_H

#ifdef __cplusplus
extern "C"{
#endif

typedef struct gc_obj{
    unsigned char vtype;
    void* value;
} gc_obj;

#define TNULL 0
#define TINT 1
#define TSTR 2

#define GC_MAX_REFS 65536

#define GC_MINCHUNK_SIZE 256
#define GC_MINHEAP_SIZE (1024 * 64)
#define GC_MAJHEAP_SIZE (1024 * 1024 * 32)
#define GC_MINCHUNKS (GC_MINHEAP_SIZE / GC_MINCHUNK_SIZE)

typedef char byte;

typedef enum{
    GC_FREE,
    GC_WHITE,
    GC_BLACK,
    GC_GRAY
} gc_color;

typedef struct collector{
    byte gc_minor_heap[GC_MINHEAP_SIZE];
    byte gc_major_heap[GC_MAJHEAP_SIZE];
    int gc_free_chunk;
    int gc_ref_count;
    void* gc_backpatch[GC_MINCHUNKS];
    void** gc_refs[GC_MAX_REFS][2];
} collector;

collector* gc_new();
void gc_add_ref(collector* gc, void** begin, void** end);
void gc_remove_ref(collector* gc, void* begin, void* end);
void gc_mark_chunk(collector* gc, byte* chunk);
void gc_major(collector* gc);
void gc_minor(collector* gc);
void* gc_minor_alloc(collector* gc, int size);
void* gc_major_alloc(collector* gc, int size);
void* gc_alloc(collector* gc, int size);

static inline void
gc_add_single_ref(collector* gc, void* ref){
    gc_add_ref(gc, ref, ref + sizeof(void*));
}

static inline void
gc_remove_single_ref(collector* gc, void* ref){
    gc_add_ref(gc, ref, ref + sizeof(void*));
}

#define WITH_HEADER(size) ((size) + sizeof(int))
#define WITHOUT_HEADER(size) ((size) - sizeof(int))
#define FLAGS(chunk) (*((unsigned int*)(chunk)))
#define CHUNK_SIZE(chunk) (FLAGS((chunk)) & (~3))
#define MARK_CHUNK(chunk, c) ((FLAGS(chunk)) = CHUNK_SIZE(chunk)|c)
#define PTR_INDEX(ofs) ((ofs) / sizeof(void*))
#define BITS(chunk) (WITH_HEADER(chunk))
#define BITS_AT(chunk, idx) ((((void**)(BITS((chunk)) + (idx) * sizeof(void*)))))
#define MEM_TAG(ptr) (!(((unsigned int)(ptr))&1))
#define POINTS_MINOR(gc, ptr) (((byte*)(ptr)) >= &(gc)->gc_minor_heap[0] && ((byte*)(ptr)) < &(gc)->gc_minor_heap[GC_MINHEAP_SIZE])
#define POINTS_MAJOR(gc, ptr) (((byte*)(ptr)) >= &gc->gc_major_heap[0] && ((byte*)(ptr)) < &gc->gc_major_heap[GC_MAJHEAP_SIZE])
#define REF_PTR_MIN(gc, ptr) (MEM_TAG((ptr)) && POINTS_MINOR(gc, (ptr)))
#define REF_PTR_MAJ(gc, ptr) (MEM_TAG((ptr)) && POINTS_MAJOR(gc, (ptr)))
#define MINOR_CHUNK(gc, ptr) (((((byte*)(ptr) - &(gc)->gc_minor_heap[0]) / GC_MINCHUNK_SIZE) * GC_MINCHUNK_SIZE) + &(gc)->gc_minor_heap[0])
#define MARKED(chunk) (FLAGS(chunk)&1)
#define CHUNK_FLAGS(chunk) (FLAGS(chunk)&(3))
#define ALIGN(ptr) (((ptr)+3)&~3)
#define CHUNK_AT(gc, i) (&gc->gc_minor_heap[(i) * GC_MINCHUNK_SIZE])
#define CHUNK_OFFSET(gc, chunk) ((((chunk)) - &gc->gc_minor_heap[0]) / GC_MINCHUNK_SIZE)

#ifdef GC_DEBUG
void gc_print_minor(collector* gc);
void gc_print_refs(collector* gc);
#endif

#ifdef __cplusplus
};
#endif

#endif