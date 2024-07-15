#ifndef UMKA_API_H_INCLUDED
#define UMKA_API_H_INCLUDED


#ifdef _WIN32
    #if defined(UMKA_STATIC)
        #define UMKA_EXPORT
        #define UMKA_IMPORT
    #else
        #define UMKA_EXPORT __declspec(dllexport)
        #define UMKA_IMPORT __declspec(dllimport)
    #endif
#else
    #define UMKA_EXPORT __attribute__((visibility("default")))
    #define UMKA_IMPORT __attribute__((visibility("default")))
#endif

#ifdef UMKA_BUILD
    #define UMKA_API UMKA_EXPORT
#else
    #define UMKA_API UMKA_IMPORT
#endif


#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


#if defined(__cplusplus)
extern "C" {
#endif


typedef union
{
    int64_t intVal;
    uint64_t uintVal;
    void *ptrVal;
    double realVal;
    float real32Val;
} UmkaStackSlot;


typedef struct
{
    int64_t entryOffset;
    UmkaStackSlot *params;
    UmkaStackSlot *result;
} UmkaFuncContext;


typedef void (*UmkaExternFunc)(UmkaStackSlot *params, UmkaStackSlot *result);


typedef enum
{
    UMKA_HOOK_CALL,
    UMKA_HOOK_RETURN,
} UmkaHookEvent;


typedef void (*UmkaHookFunc)(const char *fileName, const char *funcName, int line);


#define UmkaDynArray(T) struct \
{ \
    void *internal; \
    int64_t itemSize; \
    T *data; \
}


typedef struct
{
    void *internal1;
    void *internal2;
} UmkaMap;


typedef struct
{
    void *data;
    void *type;
} UmkaAny;


typedef struct
{
    int64_t entryOffset;
    UmkaAny upvalue;
} UmkaClosure;


typedef struct
{
    char *fileName;
    char *fnName;
    int line, pos, code;
    char *msg;
} UmkaError;


typedef void (*UmkaWarningCallback)(UmkaError *warning);


typedef struct
{
    int64_t numParams;
    int64_t numResultParams;
    int64_t numParamSlots;
    int64_t firstSlotIndex[];
} UmkaExternalCallParamLayout;


typedef struct
{
    void *(*umkaAlloc)            (void);
    bool (*umkaInit)              (void *umka, const char *fileName, const char *sourceString, int stackSize, void *reserved, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled, UmkaWarningCallback warningCallback);
    bool (*umkaCompile)           (void *umka);
    int  (*umkaRun)               (void *umka);
    int  (*umkaCall)              (void *umka, UmkaFuncContext *fn);
    void (*umkaFree)              (void *umka);
    UmkaError *(*umkaGetError)    (void *umka);
    bool (*umkaAlive)             (void *umka);
    char *(*umkaAsm)              (void *umka);
    bool (*umkaAddModule)         (void *umka, const char *fileName, const char *sourceString);
    bool (*umkaAddFunc)           (void *umka, const char *name, UmkaExternFunc func);
    bool (*umkaGetFunc)           (void *umka, const char *moduleName, const char *fnName, UmkaFuncContext *fn);
    bool (*umkaGetCallStack)      (void *umka, int depth, int nameSize, int *offset, char *fileName, char *fnName, int *line);
    void (*umkaSetHook)           (void *umka, UmkaHookEvent event, UmkaHookFunc hook);
    void *(*umkaAllocData)        (void *umka, int size, UmkaExternFunc onFree);
    void (*umkaIncRef)            (void *umka, void *ptr);
    void (*umkaDecRef)            (void *umka, void *ptr);
    void *(*umkaGetMapItem)       (void *umka, UmkaMap *map, UmkaStackSlot key);
    char *(*umkaMakeStr)          (void *umka, const char *str);
    int  (*umkaGetStrLen)         (const char *str);
    void (*umkaMakeDynArray)      (void *umka, void *array, void *type, int len);
    int  (*umkaGetDynArrayLen)    (const void *array);
    const char *(*umkaGetVersion) (void);
    int64_t (*umkaGetMemUsage)    (void *umka);
    void (*umkaMakeFuncContext)   (void *umka, void *closureType, int entryOffset, UmkaFuncContext *fn);
} UmkaAPI;


UMKA_API void *umkaAlloc            (void);
UMKA_API bool umkaInit              (void *umka, const char *fileName, const char *sourceString, int stackSize, void *reserved, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled, UmkaWarningCallback warningCallback);
UMKA_API bool umkaCompile           (void *umka);
UMKA_API int  umkaRun               (void *umka);
UMKA_API int  umkaCall              (void *umka, UmkaFuncContext *fn);
UMKA_API void umkaFree              (void *umka);
UMKA_API UmkaError *umkaGetError    (void *umka);
UMKA_API bool umkaAlive             (void *umka);
UMKA_API char *umkaAsm              (void *umka);
UMKA_API bool umkaAddModule         (void *umka, const char *fileName, const char *sourceString);
UMKA_API bool umkaAddFunc           (void *umka, const char *name, UmkaExternFunc func);
UMKA_API bool umkaGetFunc           (void *umka, const char *moduleName, const char *fnName, UmkaFuncContext *fn);
UMKA_API bool umkaGetCallStack      (void *umka, int depth, int nameSize, int *offset, char *fileName, char *fnName, int *line);
UMKA_API void umkaSetHook           (void *umka, UmkaHookEvent event, UmkaHookFunc hook);
UMKA_API void *umkaAllocData        (void *umka, int size, UmkaExternFunc onFree);
UMKA_API void umkaIncRef            (void *umka, void *ptr);
UMKA_API void umkaDecRef            (void *umka, void *ptr);
UMKA_API void *umkaGetMapItem       (void *umka, UmkaMap *map, UmkaStackSlot key);
UMKA_API char *umkaMakeStr          (void *umka, const char *str);
UMKA_API int  umkaGetStrLen         (const char *str);
UMKA_API void umkaMakeDynArray      (void *umka, void *array, void *type, int len);
UMKA_API int  umkaGetDynArrayLen    (const void *array);
UMKA_API const char *umkaGetVersion (void);
UMKA_API int64_t umkaGetMemUsage    (void *umka);
UMKA_API void umkaMakeFuncContext   (void *umka, void *closureType, int entryOffset, UmkaFuncContext *fn);


static inline UmkaAPI *umkaGetAPI(void *umka)
{
    return (UmkaAPI *)umka;
}


static inline UmkaStackSlot *umkaGetParam(UmkaStackSlot *params, int index)
{
    const UmkaExternalCallParamLayout *paramLayout = (UmkaExternalCallParamLayout *)params[-4].ptrVal;      // For -4, see the stack layout diagram in umka_vm.c
    if (index < 0 || index >= paramLayout->numParams - paramLayout->numResultParams - 1)
        return NULL;
    return params + paramLayout->firstSlotIndex[index + 1];                                                 // + 1 to skip upvalues
}


static inline UmkaAny *umkaGetUpvalue(UmkaStackSlot *params)
{
    const UmkaExternalCallParamLayout *paramLayout = (UmkaExternalCallParamLayout *)params[-4].ptrVal;      // For -4, see the stack layout diagram in umka_vm.c
    return (UmkaAny *)(params + paramLayout->firstSlotIndex[0]);
}


static inline UmkaStackSlot *umkaGetResult(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const UmkaExternalCallParamLayout *paramLayout = (UmkaExternalCallParamLayout *)params[-4].ptrVal;      // For -4, see the stack layout diagram in umka_vm.c
    if (paramLayout->numResultParams == 1)
        result->ptrVal = params[paramLayout->firstSlotIndex[paramLayout->numParams - 1]].ptrVal;
    return result;
}


static inline void *umkaGetInstance(UmkaStackSlot *result)
{
    return result->ptrVal;
}


#if defined(__cplusplus)
}
#endif

#endif // UMKA_API_H_INCLUDED
