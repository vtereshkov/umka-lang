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


typedef struct tagUmka Umka;


typedef union
{
    int64_t intVal;
    uint64_t uintVal;
    void *ptrVal;
    double realVal;
    float real32Val;   // Not used in result slots
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

    UMKA_NUM_HOOKS
} UmkaHookEvent;


typedef void (*UmkaHookFunc)(const char *fileName, const char *funcName, int line);


typedef struct tagType UmkaType;


#define UmkaDynArray(T) struct \
{ \
    const UmkaType *type; \
    int64_t itemSize; \
    T *data; \
}


typedef struct
{
    const UmkaType *type;
    struct tagMapNode *root;
} UmkaMap;


typedef struct
{
    // Different field names are allowed for backward compatibility
    union
    {
        void *data;
        void *self;
    };
    union
    {
        const UmkaType *type;
        const UmkaType *selfType;        
    };
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


typedef Umka *(*UmkaAlloc)                      (void);
typedef bool (*UmkaInit)                        (Umka *umka, const char *fileName, const char *sourceString, int stackSize, void *reserved, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled, UmkaWarningCallback warningCallback);
typedef bool (*UmkaCompile)                     (Umka *umka);
typedef int  (*UmkaRun)                         (Umka *umka);
typedef int  (*UmkaCall)                        (Umka *umka, UmkaFuncContext *fn);
typedef void (*UmkaFree)                        (Umka *umka);
typedef UmkaError *(*UmkaGetError)              (Umka *umka);
typedef bool (*UmkaAlive)                       (Umka *umka);
typedef char *(*UmkaAsm)                        (Umka *umka);
typedef bool (*UmkaAddModule)                   (Umka *umka, const char *fileName, const char *sourceString);
typedef bool (*UmkaAddFunc)                     (Umka *umka, const char *name, UmkaExternFunc func);
typedef bool (*UmkaGetFunc)                     (Umka *umka, const char *moduleName, const char *fnName, UmkaFuncContext *fn);
typedef bool (*UmkaGetCallStack)                (Umka *umka, int depth, int nameSize, int *offset, char *fileName, char *fnName, int *line);
typedef void (*UmkaSetHook)                     (Umka *umka, UmkaHookEvent event, UmkaHookFunc hook);
typedef void *(*UmkaAllocData)                  (Umka *umka, int size, UmkaExternFunc onFree);
typedef void (*UmkaIncRef)                      (Umka *umka, void *ptr);
typedef void (*UmkaDecRef)                      (Umka *umka, void *ptr);
typedef void *(*UmkaGetMapItem)                 (Umka *umka, UmkaMap *map, UmkaStackSlot key);
typedef char *(*UmkaMakeStr)                    (Umka *umka, const char *str);
typedef int  (*UmkaGetStrLen)                   (const char *str);
typedef void (*UmkaMakeDynArray)                (Umka *umka, void *array, const UmkaType *type, int len);
typedef int  (*UmkaGetDynArrayLen)              (const void *array);
typedef const char *(*UmkaGetVersion)           (void);
typedef int64_t (*UmkaGetMemUsage)              (Umka *umka);
typedef void (*UmkaMakeFuncContext)             (Umka *umka, const UmkaType *closureType, int entryOffset, UmkaFuncContext *fn);
typedef UmkaStackSlot *(*UmkaGetParam)          (UmkaStackSlot *params, int index);
typedef UmkaAny *(*UmkaGetUpvalue)              (UmkaStackSlot *params);
typedef UmkaStackSlot *(*UmkaGetResult)         (UmkaStackSlot *params, UmkaStackSlot *result);
typedef void *(*UmkaGetMetadata)                (Umka *umka);
typedef void (*UmkaSetMetadata)                 (Umka *umka, void *metadata);
typedef void *(*UmkaMakeStruct)                 (Umka *umka, const UmkaType *type);
typedef const UmkaType *(*UmkaGetBaseType)      (const UmkaType *type);
typedef const UmkaType *(*UmkaGetParamType)     (UmkaStackSlot *params, int index);
typedef const UmkaType *(*UmkaGetResultType)    (UmkaStackSlot *params, UmkaStackSlot *result);
typedef const UmkaType *(*UmkaGetFieldType)     (const UmkaType *structType, const char *fieldName);
typedef const UmkaType *(*UmkaGetMapKeyType)    (const UmkaType *mapType);
typedef const UmkaType *(*UmkaGetMapItemType)   (const UmkaType *mapType);


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
    UmkaGetMetadata     umkaGetMetadata;
    UmkaSetMetadata     umkaSetMetadata;
    UmkaMakeStruct      umkaMakeStruct;
    UmkaGetBaseType     umkaGetBaseType;
    UmkaGetParamType    umkaGetParamType;
    UmkaGetResultType   umkaGetResultType; 
    UmkaGetFieldType    umkaGetFieldType;
    UmkaGetMapKeyType   umkaGetMapKeyType;
    UmkaGetMapItemType  umkaGetMapItemType;   
} UmkaAPI;


UMKA_API Umka *umkaAlloc                    (void);
UMKA_API bool umkaInit                      (Umka *umka, const char *fileName, const char *sourceString, int stackSize, void *reserved, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled, UmkaWarningCallback warningCallback);
UMKA_API bool umkaCompile                   (Umka *umka);
UMKA_API int  umkaRun                       (Umka *umka);
UMKA_API int  umkaCall                      (Umka *umka, UmkaFuncContext *fn);
UMKA_API void umkaFree                      (Umka *umka);
UMKA_API UmkaError *umkaGetError            (Umka *umka);
UMKA_API bool umkaAlive                     (Umka *umka);
UMKA_API char *umkaAsm                      (Umka *umka);
UMKA_API bool umkaAddModule                 (Umka *umka, const char *fileName, const char *sourceString);
UMKA_API bool umkaAddFunc                   (Umka *umka, const char *name, UmkaExternFunc func);
UMKA_API bool umkaGetFunc                   (Umka *umka, const char *moduleName, const char *fnName, UmkaFuncContext *fn);
UMKA_API bool umkaGetCallStack              (Umka *umka, int depth, int nameSize, int *offset, char *fileName, char *fnName, int *line);
UMKA_API void umkaSetHook                   (Umka *umka, UmkaHookEvent event, UmkaHookFunc hook);
UMKA_API void *umkaAllocData                (Umka *umka, int size, UmkaExternFunc onFree);
UMKA_API void umkaIncRef                    (Umka *umka, void *ptr);
UMKA_API void umkaDecRef                    (Umka *umka, void *ptr);
UMKA_API void *umkaGetMapItem               (Umka *umka, UmkaMap *map, UmkaStackSlot key);
UMKA_API char *umkaMakeStr                  (Umka *umka, const char *str);
UMKA_API int  umkaGetStrLen                 (const char *str);
UMKA_API void umkaMakeDynArray              (Umka *umka, void *array, const UmkaType *type, int len);
UMKA_API int  umkaGetDynArrayLen            (const void *array);
UMKA_API const char *umkaGetVersion         (void);
UMKA_API int64_t umkaGetMemUsage            (Umka *umka);
UMKA_API void umkaMakeFuncContext           (Umka *umka, const UmkaType *closureType, int entryOffset, UmkaFuncContext *fn);
UMKA_API UmkaStackSlot *umkaGetParam        (UmkaStackSlot *params, int index);
UMKA_API UmkaAny *umkaGetUpvalue            (UmkaStackSlot *params);
UMKA_API UmkaStackSlot *umkaGetResult       (UmkaStackSlot *params, UmkaStackSlot *result);
UMKA_API void *umkaGetMetadata              (Umka *umka);
UMKA_API void umkaSetMetadata               (Umka *umka, void *metadata);
UMKA_API void *umkaMakeStruct               (Umka *umka, const UmkaType *type);
UMKA_API const UmkaType *umkaGetBaseType    (const UmkaType *type);
UMKA_API const UmkaType *umkaGetParamType   (UmkaStackSlot *params, int index);
UMKA_API const UmkaType *umkaGetResultType  (UmkaStackSlot *params, UmkaStackSlot *result);
UMKA_API const UmkaType *umkaGetFieldType   (const UmkaType *structType, const char *fieldName);
UMKA_API const UmkaType *umkaGetMapKeyType  (const UmkaType *mapType);
UMKA_API const UmkaType *umkaGetMapItemType (const UmkaType *mapType);


static inline UmkaAPI *umkaGetAPI(Umka *umka)
{
    return (UmkaAPI *)umka;
}


static inline Umka *umkaGetInstance(UmkaStackSlot *result)
{
    return (Umka *)result->ptrVal;
}


#if defined(__cplusplus)
}
#endif

#endif // UMKA_API_H_INCLUDED
