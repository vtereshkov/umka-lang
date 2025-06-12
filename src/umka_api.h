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
    const char *fileName;
    const char *fnName;
    int line, pos, code;
    const char *msg;
} UmkaError;


typedef void (*UmkaWarningCallback)(UmkaError *warning);


typedef void *(*UmkaAlloc)            (void);
typedef bool (*UmkaInit)              (void *umka, const char *fileName, const char *sourceString, int stackSize, void *reserved, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled, UmkaWarningCallback warningCallback);
typedef bool (*UmkaCompile)           (void *umka);
typedef int  (*UmkaRun)               (void *umka);
typedef int  (*UmkaCall)              (void *umka, UmkaFuncContext *fn);
typedef void (*UmkaFree)              (void *umka);
typedef UmkaError *(*UmkaGetError)    (void *umka);
typedef bool (*UmkaAlive)             (void *umka);
typedef char *(*UmkaAsm)              (void *umka);
typedef bool (*UmkaAddModule)         (void *umka, const char *fileName, const char *sourceString);
typedef bool (*UmkaAddFunc)           (void *umka, const char *name, UmkaExternFunc func);
typedef bool (*UmkaGetFunc)           (void *umka, const char *moduleName, const char *fnName, UmkaFuncContext *fn);
typedef bool (*UmkaGetCallStack)      (void *umka, int depth, int nameSize, int *offset, char *fileName, char *fnName, int *line);
typedef void (*UmkaSetHook)           (void *umka, UmkaHookEvent event, UmkaHookFunc hook);
typedef void *(*UmkaAllocData)        (void *umka, int size, UmkaExternFunc onFree);
typedef void (*UmkaIncRef)            (void *umka, void *ptr);
typedef void (*UmkaDecRef)            (void *umka, void *ptr);
typedef void *(*UmkaGetMapItem)       (void *umka, UmkaMap *map, UmkaStackSlot key);
typedef char *(*UmkaMakeStr)          (void *umka, const char *str);
typedef int  (*UmkaGetStrLen)         (const char *str);
typedef void (*UmkaMakeDynArray)      (void *umka, void *array, void *type, int len);
typedef int  (*UmkaGetDynArrayLen)    (const void *array);
typedef const char *(*UmkaGetVersion) (void);
typedef int64_t (*UmkaGetMemUsage)    (void *umka);
typedef void (*UmkaMakeFuncContext)   (void *umka, void *closureType, int entryOffset, UmkaFuncContext *fn);
typedef UmkaStackSlot *(*UmkaGetParam)(UmkaStackSlot *params, int index);
typedef UmkaAny *(*UmkaGetUpvalue)    (UmkaStackSlot *params);
typedef UmkaStackSlot *(*UmkaGetResult)(UmkaStackSlot *params, UmkaStackSlot *result);


typedef struct
{
    UmkaAlloc           umkaAlloc;
    UmkaInit            umkaInit;
    UmkaCompile         umkaCompile;
    UmkaRun             umkaRun;
    UmkaCall            umkaCall;
    UmkaFree            umkaFree;
    UmkaGetError        umkaGetError;
    UmkaAlive           umkaAlive;
    UmkaAsm             umkaAsm;
    UmkaAddModule       umkaAddModule;
    UmkaAddFunc         umkaAddFunc;
    UmkaGetFunc         umkaGetFunc;
    UmkaGetCallStack    umkaGetCallStack;
    UmkaSetHook         umkaSetHook;
    UmkaAllocData       umkaAllocData;
    UmkaIncRef          umkaIncRef;
    UmkaDecRef          umkaDecRef;
    UmkaGetMapItem      umkaGetMapItem;
    UmkaMakeStr         umkaMakeStr;
    UmkaGetStrLen       umkaGetStrLen;
    UmkaMakeDynArray    umkaMakeDynArray;
    UmkaGetDynArrayLen  umkaGetDynArrayLen;
    UmkaGetVersion      umkaGetVersion;
    UmkaGetMemUsage     umkaGetMemUsage;
    UmkaMakeFuncContext umkaMakeFuncContext;
    UmkaGetParam        umkaGetParam;
    UmkaGetUpvalue      umkaGetUpvalue;
    UmkaGetResult       umkaGetResult;
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
UMKA_API UmkaStackSlot *umkaGetParam(UmkaStackSlot *params, int index);
UMKA_API UmkaAny *umkaGetUpvalue    (UmkaStackSlot *params);
UMKA_API UmkaStackSlot *umkaGetResult(UmkaStackSlot *params, UmkaStackSlot *result);


static inline UmkaAPI *umkaGetAPI(void *umka)
{
    return (UmkaAPI *)umka;
}


static inline void *umkaGetInstance(UmkaStackSlot *result)
{
    return result->ptrVal;
}


#if defined(__cplusplus)
}
#endif

#endif // UMKA_API_H_INCLUDED
