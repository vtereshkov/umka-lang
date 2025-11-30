# The Umka Embedding API Reference

The Umka interpreter is a static or shared library that provides the API for embedding into a C/C++ host application.

## Initialization and running

### Types

```
typedef struct tagUmka Umka;
```
Umka interpreter instance handle.

### Functions

```
UMKA_API Umka *umkaAlloc(void);
```
Allocates memory for the interpreter.

Returned value: Interpreter instance handle.

```
UMKA_API bool umkaInit(Umka *umka, const char *fileName, const char *sourceString, 
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
UMKA_API bool umkaAddModule(Umka *umka, const char *fileName, const char *sourceString);
```
Adds an Umka module contained in a string buffer.

Parameters:

* `umka`: Interpreter instance handle
* `sourceString`: String buffer
* `fileName`: A fake file name assigned to the module

Returned value: `true` if the module has been successfully added.

```
UMKA_API bool umkaCompile(Umka *umka);
```
Compiles the Umka program into bytecode.

Parameters:

* `umka`: Interpreter instance handle

Returned value: `true` if the compilation is successful and no compile-time errors are detected.

```
UMKA_API int umkaRun(Umka *umka);
```
Runs the Umka program previously compiled to bytecode, i.e., calls its `main` function, if it exists. 

Parameters:

* `umka`: Interpreter instance handle

Returned value: 0 if the program execution finishes successfully and no run-time errors were detected, otherwise the error code.

```
UMKA_API void umkaFree(Umka *umka);
```
Deallocates memory allocated for the interpreter and program's global variables.

Parameters:

* `umka`: Interpreter instance handle

```
UMKA_API const char *umkaGetVersion(void);
```
Returns Umka interpreter version.

Returned value: Umka interpreter version (build date) string.

```
UMKA_API void umkaSetMetadata(Umka *umka, void *metadata);
```
Saves an arbitrary user data pointer to the Umka instance. Umka does not use the data in any way.

Parameters:

* `umka`: Interpreter instance handle
* `metadata`: User data pointer

```
UMKA_API void *umkaGetMetadata(Umka *umka);
```
Retrieves the user data pointer previously saved to the Umka instance with `umkaSetMetadata`.

Parameters:

* `umka`: Interpreter instance handle

Returned value: User data pointer

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
UMKA_API bool umkaAddFunc(Umka *umka, const char *name, UmkaExternFunc func);
```
Adds a C/C++ function to the list of external functions that can be called from Umka. 

Parameters:

* `umka` Interpreter instance handle
* `name` Function name
* `func` Function pointer

Returned value: `true` if the function has been successfully added.

```
UMKA_API bool umkaGetFunc(Umka *umka, const char *moduleName, const char *fnName, 
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
UMKA_API void umkaMakeFuncContext(Umka *umka, const UmkaType *closureType, int entryOffset, 
                                  UmkaFuncContext *fn);
```
Fills in the function context required by `umkaCall`, if it could not be filled in by `umkaGetFunc`.

Parameters:

* `umka`: Interpreter instance handle
* `closureType`: Umka function type. Can be obtained by calling `umkaGetParamType`
* `entryOffset`: Function entry point offset
* `fn`: Function context to be filled in

``` 
UMKA_API int umkaCall(Umka *umka, UmkaFuncContext *fn);
```
Calls an Umka function. 

Parameters:

* `umka`: Interpreter instance handle
* `fn`: Function context previously filled in by `umkaGetFunc` or `umkaMakeFuncContext`

Returned value: 0 if the Umka function returns successfully and no run-time errors are detected, otherwise the error code.

```
UMKA_API UmkaStackSlot *umkaGetParam(UmkaStackSlot *params, int index);
```
Finds function parameter slot.

Parameters:

* `params`: Parameter stack slots
* `index`: Parameter position. The leftmost parameter is at position 0

Returned value: Pointer to the first stack slot occupied by the parameter, `NULL` if there is no such parameter.

```
UMKA_API UmkaAny *umkaGetUpvalue(UmkaStackSlot *params);
```
Finds variables captured by a closure.

Parameters:

* `params`: Parameter stack slots

Returned value: Pointer to the captured variables stored as `any`.

```
UMKA_API UmkaStackSlot *umkaGetResult(UmkaStackSlot *params, UmkaStackSlot *result);
```
Finds the returned value slot.

Parameters:

* `params`: Parameter stack slots
* `result`: Returned value stack slots

Returned value: Pointer to the stack slot allocated for storing the returned value. 

Notes:

* Returned values of all ordinal types except `uint` are stored in the `intVal` field of the returned value slot

* Returned values of type `uint` are stored in the `uintVal` field of the returned value slot

* Returned values of types `real` and `real32` are stored in the `realVal` field of the returned value slot

* Returned values of type `str` are stored in the `ptrVal` field of the returned value slot, treated as being of type `const unsigned char *`

* Returned values of all structured types `T` are assumed to be allocated by the caller. The pointer to allocated memory is stored in the `ptrVal` field of the returned value slot, treated as being of type `T *`. In particular:

  * Inside a C function called from Umka, the returned value is allocated by the Umka interpreter. The C function must put the returned value to allocated memory, but must not overwrite the pointer

  * Before calling an Umka function from C, the C program must allocate memory needed for storing the returned value and put the pointer into the `ptrVal` field of the returned value slot

* Multiple returned values of types `(T0, T1 /*...*/)`are treated as a single structure `struct {item0: T0; item1: T1 /*...*/}` and follow the rules for structured types

```
static inline Umka *umkaGetInstance(UmkaStackSlot *result);
```
Returns the interpreter instance handle. Must not be called after the first call to `umkaGetResult`.

Parameters:

* `result`: Returned value stack slots

Returned value: Interpreter instance handle. 

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

    UMKA_NUM_HOOKS
} UmkaHookEvent;
```
Umka debug hook event kind. A `UMKA_HOOK_CALL` hook is called after calling any Umka function, `UMKA_HOOK_RETURN` before returning from any Umka function.

```
typedef void (*UmkaHookFunc)(const char *fileName, const char *funcName, int line);
```
Umka debug hook function. A callback that is called each time a hook event occurs.

Parameters:

* `fileName`: Source file in which the event occurred
* `funcName`: Function in which the event occurred
* `line`: Source file line at which the event occurred

### Functions

```
UMKA_API UmkaError *umkaGetError(Umka *umka);
```
Returns the last compile-time or run-time error.

Parameters:

* `umka`: Interpreter instance handle

Returned value: Pointer to the error description. The pointer is valid until either a new error occurs, or `umkaFree` is called.

```
UMKA_API bool umkaAlive(Umka *umka);
```
Checks if the interpreter instance has been initialized and not yet terminated. Termination means that either a runtime error has happened, or `exit` has been called. Neither `umkaRun`, nor `umkaCall` can be called after the termination.

Parameters:

* `umka`: Interpreter instance handle

Returned value: `true` if the interpreter instance has not yet terminated.

```
UMKA_API bool umkaGetCallStack(Umka *umka, int depth, int nameSize, 
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
UMKA_API void umkaSetHook(Umka *umka, UmkaHookEvent event, UmkaHookFunc hook);
```
Sets a debug hook function that will be called by the Umka interpreter each time an event occurs.

Parameters:

* `umka`: Interpreter instance handle
* `event`: Event kind that will trigger the hook function call 
* `hook`: Hook function

```
UMKA_API int64_t umkaGetMemUsage(Umka *umka);
```
Returns the allocated heap memory size.

Parameters:

* `umka`: Interpreter instance handle

Returned value: Memory size in bytes.

```
UMKA_API char *umkaAsm(Umka *umka);
```
Generates the Umka assembly listing for the Umka program previously compiled to bytecode. 

Parameters:

* `umka`: Interpreter instance handle

Returned value: String buffer pointer. It stays valid until `umkaFree` is called.

## Accessing Umka data types

### Types

```
typedef struct tagType UmkaType;
```
Umka data type.

```
#define UmkaDynArray(T) struct \
{ \
    const UmkaType *type; \
    int64_t itemSize; \
    T *data; \
}
```
Umka dynamic array containing items of type `T`. Can be initialized by calling `umkaMakeDynArray`.

```
typedef struct
{
    const UmkaType *type;
    struct tagMapNode *root;
} UmkaMap;
```
Umka map. Can be accessed by calling `umkaGetMapItem`.

```
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
UMKA_API const UmkaType *umkaGetParamType(UmkaStackSlot *params, int index);
```
Returns function parameter type.

Parameters:

* `params`: Parameter stack slots
* `index`: Parameter position. The leftmost parameter is at position 0

Returned value: Parameter type, or `NULL` if there is no such parameter or if `umkaGetParamType` is called from an `onFree` callback.

```
UMKA_API const UmkaType *umkaGetResultType(UmkaStackSlot *params, UmkaStackSlot *result);
```
Returns function result type.

Parameters:

* `params`: Parameter stack slots
* `result`: Returned value stack slots

Returned value: Function result type, or `NULL` if `umkaGetResultType` is called from an `onFree` callback.

```
UMKA_API const UmkaType *umkaGetBaseType(const UmkaType *type);
```
For a pointer type, returns the base type. For an array or dynamic array type, returns the item type.

Parameters:

* `type`: Pointer, array or dynamic array type

Returned value: Base type of a pointer type; item type of an array or dynamic array type; `NULL` otherwise.

```
UMKA_API const UmkaType *umkaGetFieldType(const UmkaType *structType, const char *fieldName);
```
Returns structure field type.

Parameters:

* `structType`: Structure type
* `fieldName`: Field name

Returned value: Field type; `NULL` if `structType` is not a structure type or has no field `fieldName`.

```
UMKA_API const UmkaType *umkaGetMapKeyType(const UmkaType *mapType);
```
Returns map key type.

Parameters:

* `mapType`: Map type

Returned value: Key type; `NULL` if `mapType` is not a map type.

```
UMKA_API const UmkaType *umkaGetMapItemType(const UmkaType *mapType);
```
Returns map item type.

Parameters:

* `mapType`: Map type

Returned value: Item type; `NULL` if `mapType` is not a map type.

```
UMKA_API void *umkaAllocData(Umka *umka, int size, UmkaExternFunc onFree);
```
Allocates an untyped chunk of heap memory.

Parameters:

* `umka`: Interpreter instance handle
* `size`: Chunk size in bytes
* `onFree`: Optional callback function that will be called when the chunk reference count reaches zero. It accepts one parameter, the chunk pointer

Returned value: Pointer to the allocated chunk. 

```
UMKA_API void umkaIncRef(Umka *umka, void *ptr);
```
Increments the reference count of a memory chunk.

Parameters:

* `umka`: Interpreter instance handle
* `ptr`: Chunk pointer

```
UMKA_API void umkaDecRef(Umka *umka, void *ptr);
```
Decrements the reference count of a memory chunk.

Parameters:

* `umka`: Interpreter instance handle
* `ptr`: Chunk pointer

```
UMKA_API char *umkaMakeStr(Umka *umka, const char *str);
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
UMKA_API void umkaMakeDynArray(Umka *umka, void *array, const UmkaType *type, int len);
```
Creates a dynamic array. Equivalent to `array = make(type, len)` in Umka.

Parameters:

* `umka`: Interpreter instance handle
* `array`: Pointer to the dynamic array, actually of type `UmkaDynArray(ItemType)`
* `type`: Dynamic array type. Can be obtained by calling `UmkaGetParamType`
* `len`: Dynamic array length 

```
UMKA_API int umkaGetDynArrayLen(const void *array);
```
Returns the length of a dynamic array. Equivalent to `len(array)` in Umka.

Parameters:

* `array`: Pointer to the dynamic array, actually of type `UmkaDynArray(ItemType)`

Returned value: Dynamic array length

```
UMKA_API void *umkaMakeStruct(Umka *umka, const UmkaType *type);
```
Creates a structure or array in heap memory.

Parameters:

* `umka`: Interpreter instance handle
* `type`: Structure or array type

Returned value: Pointer to the created structure or array.

```
UMKA_API void *umkaGetMapItem(Umka *umka, UmkaMap *map, UmkaStackSlot key);
```
Finds the map item by the given key.

Parameters:

* `umka`: Interpreter instance handle
* `map`: Umka map
* `key`: Key value

Returned value: Pointer to the map item, `NULL` if the item does not exist.

## Accessing Umka API dynamically

Using the Umka API functions generally requires linking against the Umka interpreter library. This dependency is undesirable when implementing UMIs. In such cases, the same Umka API functions can be accessed dynamically, through the Umka interpreter instance handle passed to the UMI functions.

### Types

```
typedef Umka *(*UmkaAlloc) (void);
// ... all other API function pointer types

typedef struct
{
    UmkaAlloc umkaAlloc;
    // ... all other API function pointers
} UmkaAPI;
```
Collection of pointers to all the Umka API functions, except those declared as `inline`. For any API function, there is a corresponding field with the same name in `UmkaAPI`.

### Functions

```
static inline UmkaAPI *umkaGetAPI(Umka *umka);
```
Returns Umka API function pointers. 

Parameters:

* `umka`: Interpreter instance handle

Returned value: Collection of Umka API function pointers

## Appendix: UMI example

```
// lib.um - UMI interface

fn add*(a, b: real): real
fn mulVec*(a: real, v: [2]real): [2]real
fn hello*(): str
fn squares*(n: int): []int
```
```
// lib.c - UMI implementation

#include "umka_api.h"

UMKA_EXPORT void add(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);

    const double a = api->umkaGetParam(params, 0)->realVal;
    const double b = api->umkaGetParam(params, 1)->realVal;
    api->umkaGetResult(params, result)->realVal = a + b;
}

UMKA_EXPORT void mulVec(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka); 

    const double a = api->umkaGetParam(params, 0)->realVal;
    const double* v = (const double *)api->umkaGetParam(params, 1);
    double* out = api->umkaGetResult(params, result)->ptrVal;

    out[0] = a * v[0];
    out[1] = a * v[1];
}

UMKA_EXPORT void hello(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    
    api->umkaGetResult(params, result)->ptrVal = api->umkaMakeStr(umka, "Hello");
}

UMKA_EXPORT void squares(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);

    const int n = api->umkaGetParam(params, 0)->intVal;

    typedef UmkaDynArray(int64_t) IntArray;
    IntArray *array = api->umkaGetResult(params, result)->ptrVal;
    const UmkaType *arrayType = api->umkaGetResultType(params, result); 

    api->umkaMakeDynArray(umka, array, arrayType, n);

    for (int i = 0; i < n; i++)
        array->data[i] = i * i;
}
```
