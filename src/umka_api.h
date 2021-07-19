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


#if defined(__cplusplus)
extern "C" {
#endif


typedef union
{
    int64_t intVal;
    uint64_t uintVal;
    int64_t ptrVal;
    double realVal;
    float real32Val;
} UmkaStackSlot;


typedef void (*UmkaExternFunc)(UmkaStackSlot *params, UmkaStackSlot *result);


enum
{
    UMKA_MSG_LEN = 255
};


typedef struct
{
    char fileName[UMKA_MSG_LEN + 1];
    char fnName[UMKA_MSG_LEN + 1];
    int line, pos;
    char msg[UMKA_MSG_LEN + 1];
} UmkaError;


UMKA_API void *umkaAlloc            (void);
UMKA_API bool umkaInit              (void *umka, const char *fileName, const char *sourceString, int storageSize, int stackSize, const char *locale, int argc, char **argv);
UMKA_API bool umkaCompile           (void *umka);
UMKA_API bool umkaRun               (void *umka);
UMKA_API bool umkaCall              (void *umka, int entryOffset, int numParamSlots, UmkaStackSlot *params, UmkaStackSlot *result);
UMKA_API void umkaFree              (void *umka);
UMKA_API void umkaGetError          (void *umka, UmkaError *err);
UMKA_API void umkaAsm               (void *umka, char *buf, int size);
UMKA_API void umkaAddModule         (void *umka, const char *fileName, const char *sourceString);
UMKA_API void umkaAddFunc           (void *umka, const char *name, UmkaExternFunc entry);
UMKA_API int  umkaGetFunc           (void *umka, const char *moduleName, const char *funcName);
UMKA_API bool umkaGetCallStack      (void *umka, int depth, int *offset, char *name, int size);
UMKA_API const char *umkaGetVersion (void);


#if defined(__cplusplus)
}
#endif

#endif // UMKA_API_H_INCLUDED
