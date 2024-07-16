# The Umka Embedding API Reference

The Umka interpreter is a static or shared library that provides the API for embedding into a C/C++ host application.

## Initialization and running

### Functions

```
UMKA_API void *umkaAlloc(void);
```
Allocates memory for the interpreter.

Returned value: Interpreter instance handle.

```
UMKA_API bool umkaInit(void *umka, const char *fileName, const char *sourceString, 
                       int stackSize, void *reserved, 
                       int argc, char **argv, 
                       bool fileSystemEnabled, bool implLibsEnabled,
                       UmkaWarningCallback warningCallback);
```
Initializes the interpreter instance.

Parameters:

* `umka`: Interpreter instance handle
* `fileName`: Umka source file name
* `sourceString`: Optional string buffer that contains the program source. If not `NULL`, the program source is read from this string rather than from a file. Even in this case, some fake `fileName` should be specified
* `stackSize`: Umka stack size, in slots
* `argc`/`argv`: C/C++ command-line parameter data
* `fileSystemEnabled`: Allows the Umka program to access the file system
* `implLibsEnabled`: Allows the Umka program to use UMIs
* `warningCallback`: Warning callback. If not `NULL`, it will be called on every warning

Returned value: `true` if the source has been successfully loaded.

```
UMKA_API bool umkaAddModule(void *umka, const char *fileName, const char *sourceString);
```
Adds an Umka module contained in a string buffer.

Parameters:

* `umka`: Interpreter instance handle
* `sourceString`: String buffer
* `fileName`: A fake file name assigned to the module

Returned value: `true` if the module has been successfully added.

```
UMKA_API bool umkaCompile(void *umka);
```
Compiles the Umka program into bytecode.

Parameters:

* `umka`: Interpreter instance handle

Returned value: `true` if the compilation is successful and no compile-time errors are detected.

```
UMKA_API int umkaRun(void *umka);
```
Runs the Umka program previously compiled to bytecode, i.e., calls its `main` function, if it exists. After it returns,  gracefully deallocates heap memory referenced by global variables. 

Parameters:

* `umka`: Interpreter instance handle

Returned value: 0 if the program execution finishes successfully and no run-time errors were detected, otherwise the error code.

```
UMKA_API void umkaFree(void *umka);
```
Deallocates memory allocated for the interpreter.

Parameters:

* `umka`: Interpreter instance handle

```
UMKA_API const char *umkaGetVersion(void);
```
Returns Umka interpreter version.

Returned value: Umka interpreter version (build date) string.

## Calling functions

### Types

```
typedef union
{
    int64_t intVal;
    uint64_t uintVal;
    void *ptrVal;
    double realVal;
    float real32Val;
} UmkaStackSlot;
```
Umka stack slot. Used for passing parameters to functions and returning results from functions. Each parameter or result occupies at least one slot. A parameter of a structured type, when passed by value, occupies the minimal number of consecutive slots sufficient to store it.

```
typedef struct
{
    int64_t entryOffset;
    UmkaStackSlot *params;
    UmkaStackSlot *result;
} UmkaFuncContext;
```
Umka function context used to call an Umka function from C/C++. Can be filled in by `umkaGetFunc` or `umkaMakeFuncContext` and then passed to `umkaCall`.

```
typedef void (*UmkaExternFunc)(UmkaStackSlot *params, UmkaStackSlot *result);
```
External C/C++ function that can be called from Umka.

Parameters:

* `params`: Stack slots that store the function parameters passed from Umka to C/C++. Use `umkaGetParam` to access individual parameters and `umkaGetUpvalue` to access captured variables from the slots
* `result`: Stack slots that can store the value returned from C/C++ to Umka. Use `umkaGetResult` to access the stack slot that can actually store the value

### Functions

```
UMKA_API bool umkaAddFunc(void *umka, const char *name, UmkaExternFunc func);
```
Adds a C/C++ function to the list of external functions that can be called from Umka. 

Parameters:

* `umka` Interpreter instance handle
* `name` Function name
* `func` Function pointer

Returned value: `true` if the function has been successfully added.

```
UMKA_API bool umkaGetFunc(void *umka, const char *moduleName, const char *fnName, 
                          UmkaFuncContext *fn);
```
Finds an Umka function that can be called from C/C++ using `umkaCall`.

Parameters:

* `umka`: Interpreter instance handle
* `moduleName`: Module name where the function is defined
* `funcName`: Function name
* `fn`: Function context to be filled in

Returned value: `true` if the function was found and its context filled.

```
UMKA_API void umkaMakeFuncContext(void *umka, void *closureType, int entryOffset, 
                                  UmkaFuncContext *fn);
```
Fills in the function context required by `umkaCall`, if it could not be filled in by `umkaGetFunc`.

Parameters:

* `umka`: Interpreter instance handle
* `closureType`: Umka function type. Must be obtained by calling `typeptr` and passed to the C/C++ code
* `entryOffset`: Function entry point offset
* `fn`: Function context to be filled in

``` 
UMKA_API int umkaCall(void *umka, UmkaFuncContext *fn);
```
Calls an Umka function. 

Parameters:

* `umka`: Interpreter instance handle
* `fn`: Function context previously filled in by `umkaGetFunc` or `umkaMakeFuncContext`

Returned value: 0 if the Umka function returns successfully and no run-time errors are detected, otherwise the error code.

```
static inline UmkaStackSlot *umkaGetParam(UmkaStackSlot *params, int index);
```
Finds the parameter slot.

Parameters:

* `params`: Parameter stack slots
* `index`: Parameter position. The leftmost parameter is at position 0

Returned value: Pointer to the first stack slot occupied by the parameter, `NULL` if there is no such parameter.

```
static inline UmkaAny *umkaGetUpvalue(UmkaStackSlot *params);
```
Finds the captured variables.

Parameters:

* `params`: Parameter stack slots

Returned value: Pointer to the captured variables stored as `any`.

```
static inline UmkaStackSlot *umkaGetResult(UmkaStackSlot *params, UmkaStackSlot *result);
```
Finds the returned value slot.

Parameters:

* `params`: Parameter stack slots
* `result`: Returned value stack slots

Returned value: Pointer to the stack slot allocated for storing the returned value. For a returned value of a structured type, this stack slot already contains the pointer to the memory area sufficiently large to store the actual returned value. In this case, the user must fill this memory area, but not rewrite the pointer.

```
static inline void *umkaGetInstance(UmkaStackSlot *result);
```
Returns the interpreter instance handle. Must not be called after the first call to `umkaGetResult`.

Parameters:

* `result`: Returned value stack slots

Returned value: Interpreter instance handle. 

Example:

```
// lib.um - UMI interface

fn add*(a, b: real): real
fn mulVec*(a: real, v: [2]real): [2]real
fn hello*(): str
```
```
// lib.c - UMI implementation

#include "umka_api.h"

void add(UmkaStackSlot *params, UmkaStackSlot *result)
{
    double a = umkaGetParam(params, 0)->realVal;
    double b = umkaGetParam(params, 1)->realVal;
    umkaGetResult(params, result)->realVal = a + b;
}

void mulVec(UmkaStackSlot *params, UmkaStackSlot *result)
{
    double a = umkaGetParam(params, 0)->realVal;
    double* v = (double *)umkaGetParam(params, 1);
    double* out = umkaGetResult(params, result)->ptrVal;
    out[0] = a * v[0];
    out[1] = a * v[1];
}

void hello(UmkaStackSlot *params, UmkaStackSlot *result)
{
    void *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    umkaGetResult(params, result)->ptrVal = api->umkaMakeStr(umka, "Hello");
}
```

## Debugging and profiling

### Types

```
typedef struct
{
    char *fileName;
    char *fnName;
    int line, pos, code;
    char *msg;
} UmkaError;
```
Umka error or warning description. Returned by `umkaGetError`, passed to `UmkaWarningCallback`.

```
typedef void (*UmkaWarningCallback)(UmkaError *warning);
```
Umka warning callback that can be set by `umkaInit`.

Parameters:

* `warning`: Warning description

```
typedef enum
{
    UMKA_HOOK_CALL,
    UMKA_HOOK_RETURN,
} UmkaHookEvent;
```
Umka debug hook event kind. A `UMKA_HOOK_CALL` hook is called after calling any Umka function, `UMKA_HOOK_RETURN` before returning from any Umka function.

```
typedef void (*UmkaHookFunc)(const char *fileName, const char *funcName, int line);
```
Umka debug hook function. A callback that is called each time a hook event occurs.

Parameters:

* `fileName`: Source file in which the event occured
* `funcName`: Function in which the event occured
* `line`: Source file line at which the event occured

### Functions

```
UMKA_API UmkaError *umkaGetError(void *umka);
```
Returns the last compile-time or run-time error.

Parameters:

* `umka`: Interpreter instance handle

Returned value: Pointer to the error description. The pointer is valid until either a new error occurs, or `umkaFree` is called.

```
UMKA_API bool umkaAlive(void *umka);
```
Checks if the interpreter instance has been initialized and not yet terminated. Termination means that either the `main` function has exited, or `exit` has been called. Neither `umkaRun`, nor `umkaCall` can be called after the termination.

Parameters:

* `umka`: Interpreter instance handle

Returned value: `true` if the interpreter instance has not yet terminated.

```
UMKA_API bool umkaGetCallStack(void *umka, int depth, int nameSize, 
                               int *offset, char *fileName, char *fnName, int *line);
```
Finds the Umka call stack entry.

Parameters:

* `umka`: Interpreter instance handle
* `depth`: Call stack unwinding depth. If zero, the current function information is retrieved
* `nameSize`: Size of the string buffers that will be allocated for `fileName` and `fnName`, including the null characters
* `offset`: Bytecode position, in instructions
* `fileName`: Source file name corresponding to the bytecode position
* `fnName`: Function name corresponding to the bytecode position
* `line`: Source file line corresponding to the bytecode position

Returned value: `true` on success.

```
UMKA_API void umkaSetHook(void *umka, UmkaHookEvent event, UmkaHookFunc hook);
```
Sets a debug hook function that will be called by the Umka interpreter each time an event occurs.

Parameters:

* `umka`: Interpreter instance handle
* `event`: Event kind that will trigger the hook function call 
* `hook`: Hook function

```
UMKA_API int64_t umkaGetMemUsage(void *umka);
```
Returns the allocated heap memory size.

Parameters:

* `umka`: Interpreter instance handle

Returned value: Memory size in bytes.

```
UMKA_API char *umkaAsm(void *umka);
```
Generates the Umka assembly listing for the Umka program previously compiled to bytecode. 

Parameters:

* `umka`: Interpreter instance handle

Returned value: String buffer pointer. It should be deallocated by calling `free` on the caller side.

## Accessing Umka data types

### Types

```
#define UmkaDynArray(T) struct \
{ \
    void *internal; \
    int64_t itemSize; \
    T *data; \
}
```
Umka dynamic array containing items of type `T`. Can be initialized by calling `umkaMakeDynArray`.

```
typedef struct
{
    void *internal1;
    void *internal2;
} UmkaMap;
```
Umka map. Can be accessed by calling `umkaGetMapItem`.

```
typedef struct
{
    void *data;
    void *type;
} UmkaAny;
```
Umka `any` interface.

```
typedef struct
{
    int64_t entryOffset;
    UmkaAny upvalue;
} UmkaClosure;
```
Umka closure.

### Functions

```
UMKA_API void *umkaAllocData(void *umka, int size, UmkaExternFunc onFree);
```
Allocates a reference-counted memory chunk.

Parameters:

* `umka`: Interpreter instance handle
* `size`: Chunk size in bytes
* `onFree`: Optional callback function that will be called when the chunk reference count reaches zero. It accepts one parameter, the chunk pointer

Returned value: Pointer to the allocated chunk. 

```
UMKA_API void umkaIncRef(void *umka, void *ptr);
```
Increments the reference count of a memory chunk.

Parameters:

* `umka`: Interpreter instance handle
* `ptr`: Chunk pointer

```
UMKA_API void umkaDecRef(void *umka, void *ptr);
```
Decrements the reference count of a memory chunk.

Parameters:

* `umka`: Interpreter instance handle
* `ptr`: Chunk pointer

```
UMKA_API void *umkaGetMapItem(void *umka, UmkaMap *map, UmkaStackSlot key);
```
Finds the map item by the given key.

Parameters:

* `umka`: Interpreter instance handle
* `map`: Umka map
* `key`: Key value

Returned value: Pointer to the map item, `NULL` if the item does not exist.

```
UMKA_API char *umkaMakeStr(void *umka, const char *str);
```
Creates an Umka string from a C string. Every string passed from C/C++ to Umka should be created by calling this function.

Parameters:

* `umka`: Interpreter instance handle
* `str`: C string pointer

Returned value: Umka string pointer

```
UMKA_API int umkaGetStrLen(const char *str);
```
Returns the length of an Umka string. The result is always identical to that of `strlen`, but does not imply searching for the terminating null character.

Parameters:

* `str`: Umka string pointer

Returned value: String length, in bytes, not including the null character.

```
UMKA_API void umkaMakeDynArray(void *umka, void *array, void *type, int len);
```
Creates a dynamic array. Equivalent to `array = make(type, len)` in Umka.

Parameters:

* `umka`: Interpreter instance handle
* `array`: Pointer to the dynamic array represented as `UmkaDynArray(ItemType)`
* `type`: Dynamic array type that can be obtained by calling `typeptr([]ItemType)` in Umka
* `len`: Dynamic array length 

```
UMKA_API int umkaGetDynArrayLen(const void *array);
```
Returns the length of a dynamic array. Equivalent to `len(array)` in Umka.

Parameters:

* `array`: Pointer to the dynamic array represented as `UmkaDynArray(ItemType)`

Returned value: Dynamic array length

## Accessing Umka API dynamically

Using the Umka API functions generally requires linking against the Umka interpreter library. This dependency is undesirable when implementing UMIs. In such cases, the same Umka API functions can be accessed dynamically, through the Umka interpreter instance handle passed to the UMI functions.

### Types

```
typedef struct
{
    // Function pointers
} UmkaAPI;
```
Collection of pointers to all the Umka API functions, except those declared as `inline`. For any API function, there is a corresponding field with the same name in `UmkaAPI`.

### Functions

```
static inline UmkaAPI *umkaGetAPI(void *umka);
```
Returns Umka API function pointers. 

Parameters:

* `umka`: Interpreter instance handle

Returned value: Collection of Umka API function pointers

Example:

```
UmkaAPI *api = umkaGetAPI(umka);
char *s = api->umkaMakeStr(umka, "Hello");
```
