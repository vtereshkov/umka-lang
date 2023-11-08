# The Umka Embedding API Reference

The Umka interpreter is a static or shared library that provides the API for embedding into a C/C++ host application.

## Initialization and running

### Functions

```
UMKA_API void *umkaAlloc(void);
```

Allocates memory for the interpreter and returns the interpreter instance handle.

```
UMKA_API bool umkaInit(void *umka, const char *fileName, const char *sourceString, 
                       int stackSize, const char *locale, 
                       int argc, char **argv, 
                       bool fileSystemEnabled, bool implLibsEnabled,
                       UmkaWarningCallback warningCallback);
```

Initializes the interpreter instance. Here, `umka` is the interpreter instance handle, `fileName` is the Umka source file name, `sourceString` is an optional string buffer that contains the program source, `stackSize` is the fiber stack size, in slots, `argc` and `argv` represent the standard C/C++ command-line parameter data. If `sourceString` is not `NULL`, the program source is read from this string rather than from a file. A fictitious `fileName` should nevertheless be specified.  An optional `locale` string can be specified. The `fileSystemEnabled` flag allows the Umka program to access the file system, and the `implLibsEnabled` flag allows it to use UMIs. If `warningCallback` is not `NULL`, it will be called on every warning. Returns `true` if the source has been successfully loaded.

```
UMKA_API bool umkaAddModule(void *umka, const char *fileName, const char *sourceString);
```

Adds an Umka module contained in the `sourceString`. A fictitious `fileName` should be specified. Returns `true` if the module has been successfully added.

```
UMKA_API bool umkaCompile(void *umka);
```

Compiles the Umka program into bytecode. Here, `umka` is the interpreter instance handle. Returns `true` if the compilation is successful and no compile-time errors are detected.

```
UMKA_API bool umkaRun(void *umka);
```

Runs the Umka program previously compiled to bytecode, i. e., calls its `main()` function. Here, `umka` is the interpreter instance handle. Returns `true` if the program execution finishes successfully and no run-time errors are detected.

```
UMKA_API void umkaFree(void *umka);
```

Deallocates memory allocated for the interpreter. Here, `umka` is the interpreter instance handle.

```
UMKA_API const char *umkaGetVersion(void);
```

Returns Umka interpreter version (build date) string.

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

Umka fiber stack slot. Used for passing parameters to functions and returning results from functions. 

Each parameter or result occupies at least one slot. A parameter of a structured type, when passed by value, occupies the minimal number of consecutive slots sufficient to store it. Parameter size is determined by the following rules:

* Array size is the sum of the sizes of its items
* Dynamic array size is `sizeof(UmkaDynArray)`
* Structure size is the sum of the padded sizes of its fields. The padding is determined by the natural alignment of the fields, i.e., the address of an `n`-byte field should be a multiple of `n`  
* Map size is `sizeof(UmkaMap)`

```
typedef void (*UmkaExternFunc)(UmkaStackSlot *params, UmkaStackSlot *result);
```

Umka external C/C++ function pointer. When an external C/C++ function is called from Umka, its parameters are stored in `params` in right-to-left order (i.e., the rightmost parameter is `params[0]`). The `params` is the array of slots, not of parameters. Thus, if some of the parameters are of structured types and passed by value, `params[i]` may not represent the `i`-th parameter from the right. 

The returned value should be put to `result`. If the function returns a structured type, it implicitly declares a hidden rightmost parameter that is the pointer to the memory allocated to store the returned value. This pointer should be duplicated in `result`. 

Upon entry, `result` stores a pointer to the Umka interpreter instance handle.

Examples:

```
// lib.um
fn add*(a, b: real): real

// lib.c
void add(UmkaStackSlot *params, UmkaStackSlot *result)
{
    double a = params[1].realVal;
    double b = params[0].realVal;
    result->realVal = a + b;
}

params:
    0:  b
    1:  a
```
```
// lib.um
fn mulVec*(a: real, v: [2]real): [2]real

// lib.c
void mulVec(UmkaStackSlot *params, UmkaStackSlot *result)
{
    double a = params[3].realVal;
    double* v = (double *)&params[1];
    double* out = params[0].ptrVal;   // Hidden result pointer
    out[0] = a * v[0];
    out[1] = a * v[1];
    result->ptrVal = out;
}

params:
    0:  out
    1:  v[0]
    2:  v[1]
    3:  a
```

### Functions

```
UMKA_API bool umkaAddFunc(void *umka, const char *name, UmkaExternFunc func);
```

Adds a C/C++ function to the list of external functions that can be called from Umka. Here, `umka` is the interpreter instance handle, `name` is the function name, `func` is the function pointer. Returns `true` if the function has been successfully added.

```
UMKA_API int umkaGetFunc(void *umka, const char *moduleName, const char *funcName);
```

Gets an Umka function that can be called from C/C++ using `umkaCall()`.  Here, `umka` is the interpreter instance handle, `moduleName` is the Umka module name, `funcName` is the Umka function name. Returns the function entry point offset.

``` 
UMKA_API bool umkaCall(void *umka, int entryOffset, 
                       int numParamSlots, UmkaStackSlot *params, UmkaStackSlot *result);
```

Calls the specific Umka function. Here, `umka` is the interpreter instance handle, `entryPoint` is the function entry point offset previously obtained by calling `umkaGetFunc()`,  `numParamSlots` is the number of Umka fiber stack slots occupied by the actual parameters passed to the function (equal to the number of parameters if no structured parameters are passed by value), `params` is the array of stack slots occupied by the actual parameters, `result` is the pointer to the stack slot to be occupied by the returned value. Returns `true` if the Umka function returns successfully and no run-time errors are detected.

## Debugging and profiling

### Types

```
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
```

Umka error description structure.

```
typedef void (*UmkaWarningCallback)(UmkaError *warning);
```

Umka warning callback.

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

### Functions

```
UMKA_API void umkaGetError(void *umka, UmkaError *err);
```

Gets the last compile-time or run-time error. Here, `umka` is the interpreter instance handle, `err` is the pointer to the error description structure to be filled.

```
UMKA_API bool umkaGetCallStack(void *umka, int depth, int nameSize, 
                               int *offset, char *fileName, char *fnName, int *line);
```

Gets the Umka call stack entry at the specified call `depth`. The entry is defined by the bytecode `offset`, source file `fileName`, function `fnName` and source `line`. If `depth` is zero, the current function information is retrieved. Here, `umka` is the interpreter instance handle, `nameSize` is the `fileName` or `fnName` buffer size, including the null character. Returns `true` on success.

```
UMKA_API void umkaSetHook(void *umka, UmkaHookEvent event, UmkaHookFunc hook);
```

Sets a debug hook function `hook` that will be called by the Umka virtual machine each time the `event` occurs.

```
UMKA_API char *umkaAsm(void *umka);
```

Generates the Umka assembly listing for the Umka program previously compiled to bytecode. Here, `umka` is the interpreter instance handle. The string buffer returned by `umkaAsm` should be deallocated by calling `free` on the caller side.

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

Umka dynamic array containing items of type `T`. Even though the array items pointed to by `data` can be both read and written, the structure itself is considered read-only. It cannot be used for returning a dynamic array from an external C/C++ function.

```
typedef struct
{
    void *internal1;
    void *internal2;
} UmkaMap;
```

Umka map. Can be accessed by `umkaGetMapItem`.

### Functions

```
UMKA_API void *umkaAllocData(void *umka, int size, UmkaExternFunc onFree);
```

Allocates a reference-counted memory chunk of `size` bytes and returns a pointer to it. `onFree` is an optional callback function that will be called when the chunk reference count reaches zero. It accepts one parameter, the chunk pointer. 

```
UMKA_API void umkaIncRef(void *umka, void *ptr);
```

Increments the reference count of a memory chunk pointed to by `ptr`.

```
UMKA_API void umkaDecRef(void *umka, void *ptr);
```

Decrements the reference count of a memory chunk pointed to by `ptr`.

```
UMKA_API void *umkaGetMapItem(void *umka, UmkaMap *map, UmkaStackSlot key);
```

Gets the pointer to the `map` item indexed by `key`. Returns `NULL` if the item does not exist.

```
UMKA_API char *umkaMakeStr(void *umka, const char *str);
```

Creates an Umka string from a C string. Every string passed from the host C/C++ application to Umka should be created with this function.

```
UMKA_API int umkaGetStrLen(const char *str);
```

Returns the length of an Umka string. The result is always identical to that of `strlen`, but does not imply searching for the terminating null character.

```
UMKA_API void umkaMakeDynArray(void *umka, void *array, void *type, int len);
```

Creates a dynamic `array` (treated as a pointer to `UmkaDynArray(T)`). Here, `type` is the array type represented as an opaque pointer obtained by the `typeptr()` built-in function, `len` is the array length. 

```
UMKA_API int umkaGetDynArrayLen(const void *array);
```

Returns the length of the dynamic `array` (treated as a pointer to `UmkaDynArray(T)`).

