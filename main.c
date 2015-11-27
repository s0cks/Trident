#include <stdio.h>
#include "gc.h"

typedef enum{
    T_INT,
    T_STR,
    T_NULL
} object_vtype;

typedef struct object{
    object_vtype vtype;
    void* data;
} object;

void
print_object(object* obj){
    switch(obj->vtype){
        case T_INT:{
            printf("%d\n", (int) obj->data);
            break;
        };
        case T_STR:{
            printf("%s\n", (char*) obj->data);
            break;
        };
        case T_NULL:{
            printf("null\n");
            break;
        };
    }
}

int main(int argc, char** argv){
    collector* gc = gc_new();
    return 0;
}