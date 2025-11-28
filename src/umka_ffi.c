#ifdef UMKA_FFI

#include "umka_ffi.h"
#include "umka_common.h"
#include "umka_compiler.h"
#include "umka_types.h"


static FfiStructs ffiStructs = {0};

ffi_type* appendFfiStructs(Umka *umka, const Type *type) {
    if (ffiStructs.size >= ffiStructs.capacity) {
        if (ffiStructs.capacity == 0) {
            ffiStructs.capacity = sizeof(FfiStructs) * 16;
            ffiStructs.items = storageAdd(&umka->storage, ffiStructs.capacity);
        } else {
            storageRealloc(&umka->storage, ffiStructs.items, ffiStructs.capacity*2);
        }
    }

    ffi_type *struct_type = storageAdd(&umka->storage, sizeof(ffi_type));
    struct_type->type = FFI_TYPE_STRUCT;
    struct_type->alignment = type->alignment;
    struct_type->size = type->size;

    ffiStructs.items[ffiStructs.size++] = (FfiStruct){ .hash = type->typeIdent->hash, .type = struct_type };
    return struct_type;
}

ffi_type* findFfiStruct(int hash) {
    for (int i = 0; i < ffiStructs.size; i++)
        if (ffiStructs.items[i].hash == hash) return ffiStructs.items[i].type;
    return NULL;    
}

ffi_type *mapToFfiStruct(Umka *umka, const Type *type) {
    ffi_type *struct_type = findFfiStruct(type->typeIdent->hash);
    if (struct_type != NULL)
        return struct_type;

    struct_type = appendFfiStructs(umka, type);
    if (type->numItems > MAX_STRUCT_FIELDS) {
        // todo make this a flag?
        umka->error.handler(umka->error.context,
                          "Structs passed to dynamic fn cannot have more than "
                          "%d direct members.\n"
                          "You can increase this by setting `MAX_STRUCT_FIELDS` compiler definition."
                          , MAX_STRUCT_FIELDS);
    }

    size_t fieldsSize = sizeof(ffi_type)*(type->numItems+1); // +1 for null termination
    ffi_type **structFields = storageAdd(&umka->storage, fieldsSize);
    memset(structFields, 0, fieldsSize);
    struct_type->elements = structFields;

    for (int i = 0; i < type->numItems && i < MAX_STRUCT_FIELDS; i++) {
        structFields[i] = mapToFfiType(umka, type->field[i]->type);
    }

    return struct_type;
}

ffi_type *mapToFfiType(Umka *umka,const struct tagType *type) {
    switch (type->kind) {
        // int types
    case TYPE_INT8:
        return &ffi_type_sint8;
    case TYPE_INT16:
        return &ffi_type_sint16;
    case TYPE_INT32:
        return &ffi_type_sint32;
    case TYPE_INT:
        return &ffi_type_sint64;
    case TYPE_UINT8:
        return &ffi_type_uint8;
    case TYPE_UINT16:
        return &ffi_type_uint16;
    case TYPE_UINT32:
        return &ffi_type_uint32;
    case TYPE_UINT:
        return &ffi_type_uint64;
    case TYPE_CHAR:
        return &ffi_type_uchar;
    case TYPE_BOOL:
        switch (sizeof(bool)) {
        case 1:
            return &ffi_type_uint8;
        case 2:
            return &ffi_type_uint16;
        case 4:
            return &ffi_type_uint32;
        case 8:
            return &ffi_type_uint64;
        }

        // ptr types
    case TYPE_STR:
    case TYPE_NULL:
    case TYPE_ARRAY:
    case TYPE_PTR:
        return &ffi_type_pointer;

    case TYPE_STRUCT:
        return mapToFfiStruct(umka, type->typeIdent->type);

        // float types
    case TYPE_REAL32:
        return &ffi_type_float;
        break;
    case TYPE_REAL:
        return &ffi_type_double;
        break;

        // skip
    case TYPE_INTERFACE:
        return NULL;

    case TYPE_VOID:
        return &ffi_type_void;

        // not supported
    case TYPE_WEAKPTR:
    case TYPE_DYNARRAY:
    case TYPE_MAP:
    case TYPE_NONE:
    case TYPE_FORWARD:
    case TYPE_CLOSURE:
    case TYPE_FIBER:
    case TYPE_FN:
      umka->error.handler(
          umka->error.context,
          "Type `%s` is unsupported in ffi function declarations",
          typeKindSpelling(type->kind));
    }
    return NULL;
}

int assignFfiTypes(Umka *umka, ffi_type **types, const Signature *sig)
{
    int numArgs = 0;
    for(int i = 0; i < sig->numParams && i < 16; i++) {
        const Param *param = sig->param[i];
        ffi_type *type = mapToFfiType(umka, param->type);

        if (strcmp(param->name, "#upvalues") == 0) continue;
        if (strcmp(param->name, "#result") == 0) continue;

        types[numArgs++] = type;
    }
    return numArgs;
}    

#endif // UMKA_FFI
