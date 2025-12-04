#ifndef UMKA_FFI_H_
#define UMKA_FFI_H_

#include "ffi.h"

#include "umka_common.h"
#include "umka_types.h"

#ifndef MAX_STRUCT_FIELDS
#define MAX_STRUCT_FIELDS (64)
#endif

#ifdef UMKA_VM_DEBUG
    #define FORCE_INLINE
    #define UNLIKELY(x)  (x)
#else
    #ifdef _MSC_VER  // MSVC++ only
        #define FORCE_INLINE __forceinline
        #define UNLIKELY(x)  (x)
    #else
        #define FORCE_INLINE __attribute__((always_inline)) inline
        #define UNLIKELY(x)  __builtin_expect(!!(x), 0)
    #endif
#endif


typedef struct {
    int hash;
    ffi_type *type;
} FfiStruct;


typedef struct {
    FfiStruct* items;
    size_t size;
    size_t capacity;
} FfiStructs;

typedef struct
{
    void *entry;
    ffi_cif cif;
} DynamicCall;


ffi_type*   mapToFfiType    (Umka *umka,const struct tagType *type);
int         assignFfiTypes  (Umka *umka, ffi_type **types, const Signature *sig);

#endif // UMKA_FFI_H_
