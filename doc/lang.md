# The Umka Language Reference

## Introduction

Umka is a statically typed embeddable scripting language. It combines the simplicity and flexibility needed for scripting with a compile-time protection against type errors.

Language grammar in this manual is specified in the EBNF notation.

## Lexical elements

A program consists of keywords, identifiers, numbers, character literals, string literals, operators, punctuation and comments.

### Keywords

Keywords have special meaning and cannot be used in any other role. Umka has the following keywords:

```
break case const continue default else enum fn for import 
interface if in map return str struct switch type var weak
```

### Identifiers

Identifiers denote constants, types, variables and functions.

Syntax:

```
ident  = (letter | "_") {letter | "_" | digit}.
letter = "A".."Z" | "a".."z".
digit  = "0".."9".
```

Examples:

```
i
myName
printf
Point_1
```

### Numbers

Numbers in Umka can be integer (decimal or hexadecimal) or real.

Syntax:

```
intNumber   = decNumber | hexHumber.
decNumber   = digit {digit}.
hexNumber   = "0" ("X | "x") hexDigit {hexDigit}.
realNumber  = decNumber ["." decNumber] [("E" | "e") decNumber].
hexDigit    = digit | "A".."F" | "a".."f".
```

Examples:

```
16
0xA6
0xffff
3.14
2.6e-5
7e8
```

### Character literals

A character literal is an ASCII character enclosed in single quotes. Character literals can be specified "as is" or by an escape sequence. Escape sequence prefixes have the same meaning as in C or other languages.

Syntax:

```
charLiteral = "'" (char | escSeq) "'".
char        = "\x00".."\xFF".
escSeq      = "\" ("0" | "a" | "b" | "e" | "f" | "n" | "r" | "t" | "v" | "x" hexNumber).
hexDigit    = digit | "A".."F" | "a".."f"
```

Examples:

```
'a'
'\n'
'\xFF'
```

### String literals

A string literal is a sequence of ASCII characters enclosed in double quotes. Characters can be specified "as is" or by an escape sequence.

Syntax:

```
stringLiteral = """ {char | escSeq} """.
```

Examples:

```
"A long string"
"Hello, World\n"
""
```

### Operators and punctuation

Umka has a set of arithmetical, bitwise, logical, relation operators, as well as a set of punctuation characters:

```
+    -    *    /    %    &    |    ~    <<    >>
+=   -=   *=   /=   %=   &=   |=   ~=   <<=   >>=
&&   ||   ?    !    ++   --
==   <    >    !=   <=   >=
=    :=   (    )    [    ]    {    }    
^    ;    :    ::   .    ..
```

#### Implicit semicolons

Umka uses semicolons as statement terminators. To reduce the number of semicolons, an implicit semicolon is automatically inserted immediately after a line's final token if that token is

* An identifier
* A number, character literal or string literal
* `str`,  `break`,  `continue` or `return`
* `++`, `--`, `)`, `]`, `}`or `^`
* `*` as an export mark 

A semicolon may be omitted before a `)` or `}`. An implicit semicolon inserted before a `)` in actual parameter lists or a `}` in composite literals is ignored.

### Comments

Umka supports line comments starting with `//`  and block comments starting with `/*` and ending with `*/`.

Examples:

```
// This is a line comment

/*
This is
a block
comment
*/
```

## Data types

Umka is a statically typed language. Each variable or constant has a type that is either explicitly specified in the variable declaration or inferred from the type of the right-hand side expression in a variable assignment or constant declaration. After a variable or constant is declared or assigned for the first time, it cannot change its type.

Syntax:

```
type = qualIdent | ptrType | arrayType | dynArrayType | strType | enumType | 
       mapType | structType | interfaceType | closureType.
```

Any data type except the interface type can have a set of functions attached to it, which are called *methods*. A variable of that type is called the *receiver* with respect to any method which is called on it. Methods are not considered a part of the type declaration. 

### Simple types

Simple type represent single values.

#### Ordinal types

Umka supports the following ordinal types:

* Signed integer types  `int8`,  `int16` , `int32`,  `int`
* Unsigned integer types `uint8`,  `uint16` , `uint32`,  `uint`
* Boolean type `bool`
* Character type `char`
* Enumeration types

The  `int`  and  `uint`  are the recommended 64-bit integer types. Other integer types are for compatibility with an external environment.

The two possible values of the Boolean type are `true` and `false`.

An enumeration type is defined by its base type (any integer type) and a list of named enumeration constants used as values of the enumeration type. If the base type is omitted, `int` is assumed. If the integer value of an enumeration constant is omitted, the value is obtained by incrementing the previous constant's value by 1. For the first constant, 0 is assumed.

Syntax:

```
enumType = "enum" ["(" type ")"] "{" {enumItem ";"} "}".
enumItem = ident ["=" expr].
```

Examples:

```
enum {left; middle; right}   // 0; 1; 2

enum (uint8) {
    draw = 74
    select                   // 75
    remove = 8
    edit                     // 9
}
```

#### Real types  

Umka supports the  `real32` and  `real`  floating-point types.  The  `real`  is the recommended 64-bit real type. The `real32`  type is for compatibility with an external environment.

#### Pointer types

A variable that stores a memory address of another variable has a pointer type. The type of that another variable is called the *base type* of the pointer type. The pointer type is specified by a `^`  followed by the base type specification. If the base type is unknown, it should be specified as `void`. An uninitialized pointer has the value `null`. 

Umka performs automatic memory management using reference counting. All pointers are reference-counted by default. However, in processing data structures with cyclic references (like doubly-linked lists), reference counting is unable to deallocate memory properly. In this case, one of the pointers that constitute the cycle can be declared `weak`. A weak pointer is not reference-counted and its existence does not prevent the pointed-to variable from being deallocated. If a weak pointer does not point to a valid dynamically allocated memory block, it is equal to `null`.

Syntax:

```
ptrType = ["weak"] "^" type.
```

Examples:

```
^int16
^void
weak ^Vec
```

#### Function types

A constant that stores an entry point of a function has a function type. A function is characterized by its *signature* that consists of a set of parameter names, types and optional default values, and an optional set of returned value types. The type of the last parameter in the function signature may be prefixed with `..`. The type `..T` is equivalent to `[]T` within the function. Such a function is called *variadic*.

Syntax:

```
signature = "(" [typedIdentList ["=" expr] {"," typedIdentList ["=" expr]}] ")" 
            [":" (type | "(" type {"," type} ")")].
```

Each function definition in the module scope defines a constant of a function type. A constant or variable of a function type cannot be declared using a `const` or `var` declaration, which produce a closure type instead.   

Examples:

```
fn foo(code: int)
fn MyFunc(p: int, x, y: real): (bool, real)
fn printMsg(msg: char, values: ..real)
```

### Structured types

Structured types represents sequences of values.

#### Array types

An array is a numbered sequence of items of a single type. The number of items is called the *length* of the array and should be a non-negative integer constant expression.

Syntax:

```
arrayType = "[" expr "]" type.
```

Examples:

```
[3]real
[len1 + len2][10]Vec
```

Array assignment copies the contents of the array.

#### Dynamic array types

A dynamic array is a numbered sequence of items of a single type. Its length is not fixed at compile time and can change at run time due to appending or deleting of items.

Syntax:

```
dynArrayType = "[" "]" type.
```

Examples:

```
[]real
[][3]uint8
```

Dynamic arrays require initialization by assignment from a static array or from a dynamic array literal, or by explicitly calling `make()`. 

Dynamic array assignment copies the pointer, but not the contents of the array. 

#### String type

A string is a sequence of ASCII characters. Its length is not fixed at compile time and can change at run time due to concatenation by using the  `+` operator.

Syntax:

```
strType = "str"
```

String assignment copies the contents of the string.

#### Map types

A map is a collection of items of a single type indexed by unique values, called *keys*, of another type. The key type must be comparable, i.e., the `==` and `!=` operators must be applicable to operands of this type.

Syntax:

```
mapType = "map" "[" type "]" type.
```

Examples:

```
map[str]real
map[[]int]str
```

Maps require initialization by assignment from a map literal or by explicitly calling `make()`. 

Map assignment copies the pointer, but not the contents of the map

#### Structure types

A structure is a sequence of named items, called *fields*, that possibly have different types. Each field should have a unique name.

Syntax:

```
structType = "struct" "{" {typedIdentList ";"} "}".
```

Examples:

```
struct {x, y, z: int}

struct {
    q: [4]real
    normalized: bool
}
```

Structure assignment copies the contents of the structure.

#### Interface types

An interface is a sequence of method names and signatures. Each method should have a unique name. It is allowed to specify either a method name and its signature, or a name of some other interface type whose methods are to be included to the new interface type.

Syntax:

```
interfaceType = "interface" "{" {(ident signature | qualIdent) ";"} "}".
```

Examples:

```
interface {
    print(): int
    Saveable
}
```

The interface does not implement these methods itself. Instead, any type that implements all the methods declared in the interface can be converted to that interface. Since different types can be converted to the same interface, interfaces provide polymorphic behavior in Umka.

Let `T` be a non-pointer type. If a pointer of type `^T` is converted to the interface, the interface stores that pointer. If a variable of type `T`  is converted to the interface, the variable is first copied to a new memory location, and the interface stores the pointer to that location. In any case, the interface can be converted back to the type `T` or  `^T`. If a conversion of the interface to some non-pointer type `S` not equivalent to `T` is attempted, a runtime error is triggered. If a conversion of the interface to some pointer type `^S` not equivalent to `^T` is attempted, the result is `null`. 

Any type can be converted to the built-in type `any`, which is an alias for `interface{}`.

Interface assignment copies the pointer, but not the contents of the variable that has been converted to the interface.

#### Closure types

A closure stores a function entry point (a value of a function type) and an optional set of variables *captured* from the enclosing function scope. These variables are copied to the closure function, so they can be used as local variables within the closure function even if they no longer exist in the enclosing function. 

Syntax:

```
closureType = "fn" signature.
```

Examples:

```
fn (code: int)
fn (p: int, x, y: real): (bool, real)
fn (msg: char, values: ..real)
```

#### Fiber type

The `fiber` type represents a fiber context to use in multitasking.

### Type compatibility

Two types are *equivalent* if

* They are the same type
* They are declared types that have the same identifier. Any other declared types are not equivalent 
* They are pointer types and have equivalent base types and both are either strong or weak
* They are array types and have equal length and equivalent item types
* They are dynamic array types and have equivalent item types
* They are map types and have equivalent key types and equivalent item types
* They are structure or interface types and have equal number of fields, equal field names and equivalent field types
* They are function types, which are both either methods or non-methods, and have equal number of parameters, equivalent parameter types, equal parameter default values (excluding the receiver parameter) and equivalent result type.

Two types are *compatible* if

* They are equivalent
* They are integer types
* They are real types

### Type conversions

#### Implicit conversions

If a value `s` of type `S` is given where a value `t` of some other type `T` is expected, the `s` is implicitly converted to `t` if

* `S` and `T` are compatible
* `S` is an integer type and `T` is a real type
* `S` is `char` and `T` is `str`
* `S` is a `[]char` and `T` is `str`
* `S` is `str` and `T` is `[]char` and `s` and `t` are not operands of a binary operator
* `S` is an array type and `T` is a dynamic array type and the item types of `S` and `T` are equivalent and `s` and `t` are not operands of a binary operator
* `S` is a dynamic array type and `T` is an array type and the item types of `S` and `T` are equivalent and `len(s) <= len(t)` 
* `S` is a type (or a pointer to a type) that implements all the methods of `T` and `T` is an interface type 
* `S` and `T` are interface types and `S` declares all the methods of `T`
* `S` and `T` are pointer types and either `s` is `null`, or `T` is `^void`
* `S` is a weak pointer type and `T` is a strong pointer type and either the base types of `S` and `T` are equivalent, or `t` is `null` 
* `S` is a strong pointer type and `T` is a weak pointer type and either the base types of `S` and `T` are equivalent, or `s` is `null`, and `s` and `t` are not operands of a binary operator
* `S` is a function type and `T` is a closure type and `S` and the underlying function type of `T` are equivalent

#### Explicit conversions

If a value `s` of type `S` is given where a value `t` of some other type `T` is expected, the `s` can be explicitly converted (cast) to `t` if

* `S` can be implicitly converted to `T`, possibly except the type name
* `S` and `T` are ordinal types
* `S` and `T` are pointer types and `sizeof(s^) >= sizeof(t^)` and both `S` and `T` don't contain pointers
* `S` is an interface type and `T` is a type (or a pointer to a type) that was actually converted to `S`
* `S` is `[]U`, `T` is `[]V` and `U` can be explicitly converted to `V` 

## Declarations

All types, constants, variables and functions should be declared before the first use. As an exception, a pointer base type, a dynamic array base type or a map item type may be declared after using the pointer type, the dynamic array type or the map type, respectively, but before the end of the same `type` declaration list. No identifier may be declared twice in the same block, except for redeclarations allowed in multi-value short variable declarations.

Syntax:

```
decls = decl {";" decl}.
decl  = typeDecl | constDecl | varDecl | fnDecl.
```

### Scopes

* Universe scope. All built-in identifiers implicitly belong to the universe scope. They are valid throughout the program.
* Module scope. Identifiers declared outside any function body belong to the module scope. They are valid from the point of declaration to the end of the module. If the declared identifiers are marked as exported and the module in which they are declared is imported from some other module, the identifiers are also valid in that other module, if qualified with the module name. Non-constant expressions are not allowed in declarations belonging to the module scope.
* Block scope. Identifiers declared within a function body belong to the block scope. They are valid from the point of declaration to the end of the block, including all the nested blocks, except that a variable belonging to the block scope is not valid within the nested function blocks. A declaration can be shadowed by a declaration of the same name in a nested block.

Declaration export syntax:

```
exportMark = ["*"].
```

### Type declarations

Syntax:

```
typeDecl     = "type" (typeDeclItem | "(" {typeDeclItem ";"} ")").
typeDeclItem = ident exportMark "=" type.
```

Examples:

```
type Vec = struct {x, y, z: int}
type (
    i8 = int8
    u8 = uint8
)
```

#### Built-in types

```
void
int8 int16 int32 int
uint8 uint16 uint32 uint
bool
char
real32 real
fiber
any
```

### Constant declarations

There are ordinal constants, real constants, string constants and closure constants. A constant declaration generally requires an explicit constant value. However, if a constant declaration is preceded by another integer constant declaration within the same declaration list, its explicit value can be omitted. In this case, the value will be set to the preceding constant value incremented by 1. 

Syntax:

```
constDecl     = "const" (constDeclItem | "(" {constDeclItem ";"} ")").
constDeclItem = ident exportMark ["=" expr].
```

Examples:

```
const a = 3
const b* = 2.38
const (
    c = sin(b) / 5
    d = "Hello" + " World"
)
const (
    e = 5
    f       // 6
    g       // 7
    h = 19
    i       // 20
)
```

#### Built-in constants

```
true false
null
```

### Variable declarations

Syntax:

```
varDecl = fullVarDecl | shortVarDecl.
```

#### Full variable declarations

Full variable declarations require explicit type specification and have an optional list of initializer expressions. If the initializer expressions are omitted, the variables are initialized with zeros.

Syntax:

```
fullVarDecl    = "var" (varDeclItem | "(" {varDeclItem ";"} ")").
varDeclItem    = typedIdentList "=" exprList.
typedIdentList = identList ":" [".."] type.
identList      = ident exportMark {"," ident exportMark}.
```

Examples:

```
var e1, e2: int = 2, 3
var f: String = d + "!"
var z: []int = {2, 4}   // Literal type omitted
var (
    g: Arr
    h: []real
)
```

#### Short variable declarations

Short variable declarations are always combined with the assignment statements. They don't require explicit type specifications, since they infer the variable types from the types of the right-hand side expressions.

Syntax:

```
shortVarDecl        = declAssignmentStmt.
singleDeclAssgnStmt = ident ":=" expr.
listDeclAssgnStmt   = identList ":=" exprList.
declAssignmentStmt  = singleDeclAssgnStmt | listDeclAssgnStmt.
```

Examples:

```
a := 5
s, val := "Hello", sin(0.1 * a)
```

A short variable declaration may redeclare variables provided they were originally declared earlier in the same block with the same type and at least one of the variables in the left-hand side list is new. Redeclaration does not introduce new variables. 

### Function and method declarations

A function or method declaration can be either a complete definition that includes the function block, or a *prototype* declaration if the function block is omitted. The prototype should be resolved in one of the following ways (ordered by descending priority):

* A complete function definition having the same signature somewhere below in the current module
* An external C/C++ function defined in the host application and registered by calling `umkaAddFunc`. The current module must be contained in a source string registered by calling `umkaAddModule`, rather than in a physical source file
* An external C/C++ function defined in the shared library (Umka module implementation library, UMI) named `mod.umi`, where `mod` is the current module name

If the prototype is not resolved, an error is triggered. 

Function and method declarations are only allowed in the module scope. In the block scope, functions should be declared as constants or variables of a function type. Methods cannot be declared in the block scope. 

The method receiver type should be a pointer to any declared type, except interface and pointer types. Methods should be declared in the same module as the method receiver type name.

Syntax:

```
fnDecl       = "fn" [rcvSignature] ident exportMark signature (fnBlock | fnPrototype).
rcvSignature = "(" ident ":" type ")".
fnBlock      = block.
fnPrototype  = .
```

Examples:

```
fn tan(x: real): real {return sin(x) / cos(x)}

fn getValue(): (int, bool) {
    return 42, true
}

fn (a: ^Arr) print(): int {
    printf("Arr: %v\n", a^)
    return 0
}
```

#### Built-in functions 

Built-in functions don't necessarily adhere to the general rules for functions.

##### Input/output functions

```
fn printf(format: str, a1: T1, a2: T2...): int
fn fprintf(f: ^std::File, format: str, a1: T1, a2: T2...): int
fn sprintf(format: str, a1: T1, a2: T2...): str
```

Write `a1`, `a2`... to the console, or to the file `f`, or to a string, according to the `format` string. `printf` and `fprintf` return the number of bytes written, `sprintf` returns the resulting string.

```
fn scanf(format: str, a1: ^T1, a2: ^T2...): int
fn fscanf(f: ^std::File, format: str, a1: ^T1, a2: ^T2...): int
fn sscanf(buf, format: str, a1: ^T1, a2: ^T2...): int
```

Read `a1`, `a2`... from the console, or from the file `f`, or from the string `buf`, according to the `format` string. `T1`, `T2`... should be ordinal, or real, or string types. Return the number of values read.

The `format` string used by the input/output functions may contain format specifiers of the following form:

```
%[flags][width][.precision][length]type

Length:

hh                  Very short
h                   Short
l                   Long
ll                  Very long

Type:

d, i                Integer or enumeration
u                   Unsigned integer or enumeration
x, X                Hexadecimal integer or enumeration
f, F, e, E, g, G    Real
s                   String
c                   Character
v                   Any
```

The `v` type specifier prints the hierarchical structure of any value according to its type. It is only allowed in `printf`, `fprintf`, `sprintf`. When combined with the `l` or `ll` length specifier, it also dereferences all pointers contained in the value. The `ll` length specifier also enables multi-line pretty-printing with indentation. 

The number of the format specifiers and the data types encoded by them should match the number and types of `a1`, `a2`... Otherwise, a runtime error is triggered.



##### Mathematical functions

```
fn round(x: real): int          // Rounding to the nearest integer 
fn trunc(x: real): int          // Rounding towards zero
fn ceil (x: real): int          // Rounding upwards
fn floor(x: real): int          // Rounding downwards
fn fabs (x: real): real         // Absolute value
fn sqrt (x: real): real         // Square root
fn sin  (x: real): real         // Sine
fn cos  (x: real): real         // Cosine
fn atan (x: real): real         // Arctangent
fn atan2(y, x: real): real      // Arctangent of y / x
fn exp  (x: real): real         // Exponent
fn log  (x: real): real         // Natural logarithm
```

##### Memory management functions

```
fn new(T [, x: T]): ^T
```

Allocates memory for a variable of type `T`, initializes it with zeros (or with the value `x` if specified) and returns a pointer to it. 

```
fn make([]T, length: int): []T                // (1)
fn make(map[K]T): map[K]T                     // (2)
fn make(fiber, f: fn()): fiber                // (3)
```

(1) Constructs a dynamic array of `length` items of type `T` initialized with zeroes. 

(2) Constructs an empty map with item type `T` indexed by keys of type `K`.

(3) Constructs a fiber and prepares it for calling the function `f`. The actual execution starts on the first call to `resume`.

```
fn copy(a: []T): []T              // (1)
fn copy(m: map[K]T): map[K]T      // (2)
```

(1) Constructs a copy of the dynamic array `a`.

(2) Constructs a copy of the map `m`.

```
fn append(a: []T, x: (^T | []T)): []T
```

Appends `x` to the dynamic array `a`. The `x` can be either a new item of the same type as the item type `T`  of `a`, or another dynamic array of the same item type `T`. If `len(a) + len(x) <= cap(a)`, it is altered and returned. Otherwise, a new dynamic array is constructed and returned. 

```
fn insert(a: []T, index: int, x: T): []T
```

Inserts `x` into the dynamic array `a` at position `index`. The `x` should be a new item of the same type as the item type `T` of `a`. If `len(a) + 1 <= cap(a)`, it is altered and returned. Otherwise, a new dynamic array is constructed and returned.

```
fn delete(a: []T, index: int): []T        // (1)
fn delete(m: map[K]T, key: K): map[K]T    // (2)
```

(1) Deletes an item at position `index` from the dynamic array `a` and returns this dynamic array.

(2) Deletes an item indexed by `key` from the map `m` and returns this map.

```
fn slice(a: ([]T | str), startIndex [, endIndex]: int): ([]T | str)
```

Constructs a copy of the part of the dynamic array or string `a` starting at `startIndex` and ending before `endIndex`. If `endIndex` is omitted, it is treated as equal to `len(a)`. If `endIndex` is negative, `len(a)` is implicitly added to it.

```
fn sort(d: []T, compare: fn (a, b: ^T): int)    // (1)
fn sort(d: []T, ascending: bool [, fieldName])  // (2)
```

(1) Sorts the dynamic array `d` in ascending order as determined by the `compare` function. This function should return a negative number when `a^ < b^`, a positive number when `a^ > b^` and zero when `a^ == b^`.

(2) Sorts the dynamic array `d` in ascending or descending order as determined by the `ascending` flag. The type `T` should be either a type for which the `<` operator is defined, or a structure type that has a field named `fieldName` and the `<` operator is defined for the type of this field. Generally performs faster than (1).   

```
fn len(a: ([...]T | []T | map[K]T | str)): int
```

Returns the length of `a`, where `a` can be an array, a dynamic array, a map or a string.

```
fn cap(a: []T): int
```

Returns the capacity of the dynamic array `a`, i.e., the number of items for which the space is allocated in `a`.

```
fn sizeof(T | a: T): int
```

Returns the size of type `T ` in bytes.

```
fn sizeofself(a: interface{...}): int
```

Returns the size in bytes of the variable that has been converted to the interface `a`.

```
fn selfhasptr(a: interface{...}): bool
```

Checks whether the type of the variable that has been converted to the interface`a` is a pointer, string, dynamic array, interface or fiber, or has one of these types as its item type or field type.

```
fn selftypeeq(a, b: interface{...}): bool
```

Checks whether the types of the variables that have been converted to the interfaces `a`  and `b` are equivalent.

```
fn typeptr(T): ^void
```

Returns a representation of type `T` as an opaque pointer. This pointer can be passed to the C/C++ host application to be used by the embedding API functions that require Umka types, such as `umkaMakeDynArray()`. 

```
fn valid(a: ([]T | map[K]T | interface{...} | fn (...): T | fiber)): bool
```

Checks whether a dynamic array, map, interface, function variable or fiber has been initialized with some value.

##### Map functions

```
fn validkey(m: map[K]T, key: K): bool
```

Checks whether the map `m` has an item indexed by `key`.

```
fn keys(m: map[K]T): []K
```

Returns a dynamic array of the keys that index the items of the map `m`.

##### Multitasking functions

```
fn resume([fib: fiber])
```

Starts or resumes the execution of the fiber `fib`. If `fib` is omitted, the parent fiber of the current fiber is resumed. If the fiber to be resumed is dead or does not exist, the current fiber continues execution.

##### Miscellaneous functions

```
fn memusage(): int
```

Returns the allocated heap memory size in bytes.

```
fn exit(code: int, msg: str = "")
```

Terminates the program, exiting with exit code `code`, and an optional error message `msg`.

## Expressions

### Primary expressions

A primary expression is either an identifier optionally qualified with a module name, or a built-in function call.

Syntax:

```
primary     = qualIdent | builtinCall.
qualIdent   = [ident "::"] ident.
builtinCall = qualIdent "(" [expr {"," expr}] ")".
```

Examples:

```
i
std::File
atan2(y0, x0)
printf("a = %f\n", a)
```

### Type casts

A type cast explicitly converts the type of an expression to another type. It consists of the target type followed by the expression in parentheses.

Syntax:

```
typeCast = type "(" expr ")".
```

Examples:

```
int('b')
^[3]real(iface)
```

### Composite literals

A composite literal constructs a value for an array, dynamic array, map, structure or closure and creates a new value each time it is evaluated. It consists of the type followed by the brace-bound list of item values (for arrays or dynamic arrays), list of key/item pairs (for maps), list of field values with optional field names (for structures) or the function body (for closure). A closure literal can be prepended with a list of captured variables bound by a pair of `|`s.

The number of array item values or structure field values (if the optional field names are omitted) should match the literal type. The types of array item values, dynamic array item values, map key and item values, or structure field values should be compatible to those of the literal type.

The composite literal type can be omitted if it can be inferred from the context.

Syntax:

```
compositeLiteral = [type] (arrayLiteral | dynArrayLiteral | mapLiteral | 
                   structLiteral | closureLiteral).
arrayLiteral     = "{" [expr {"," expr}] "}".
dynArrayLiteral  = arrayLiteral.
mapLiteral       = "{" expr ":" expr {"," expr ":" expr} "}".
structLiteral    = "{" [[ident ":"] expr {"," [ident ":"] expr}] "}".
closureLiteral   = ["|" ident {"," ident} "|"] fnBlock.
```

Examples:

```
[3]real{2.3, -4.1 / 2, b}
[]any{7.2, "Hello", [2]int{3, 5}}
map[[]int]str{[]int{13, 15}: "First", []int{57, 89}: "Second"}
Vec{x: 2, y: 8}
Mat{ {1, 2}, {3, 4} }                   // Nested literals' types omitted
fn (x: int) |y, z| {return x + y + z}
```

### Enumeration constants

An enumeration constant is a value of an enumeration type. It is denoted by the enumeration type followed by a `.` and the enumeration constant name. The type can be omitted if it can be inferred from the context.

Syntax:

```
enumConst = [type] "." ident.
```

Examples:

```
Mode.draw
.left
```

### Designators and selectors

A number of postfix *selectors* can be applied to a primary expression, a type cast, a composite literal or an enumeration constant to get a *designator*.

Syntax:

```
designator = (primary | typeCast | compositeLiteral | enumConst) selectors.
selectors  = {derefSelector | indexSelector | fieldSelector | callSelector}.
```

#### Dereferencing selector

The dereferencing selector  `^` accesses the value pointed to by a pointer. It can be applied to a pointer with a non-`void` base type.

Syntax:

```
derefSelector = "^".
```

Examples:

```
p^
^int(a)^
```

#### Index selector

The index selector `[...]` accesses a collection item. It can be applied to an array, a dynamic array, a map, a string or a pointer to one of these types. For maps, the index should be of the type compatible with the key type of the map. If there is no such a key in the map, a new map item is constructed and initialized with zeroes. For all the other collection types, the index should be an integer expression. Item indexing starts from 0. If the index is out of bounds, an error is triggered. For strings, the result of applying the index selector cannot be in the left-hand side of the assignment statement.

Syntax:

```
indexSelector = "[" expr "]".
```

Examples:

```
b[5]
longVector[2 * i + 3 * j]
m["Hello"]
```

#### Field or method selector

The field or method selector `.` accesses a field or method. For accessing a field, it can be applied to a structure that has this field or to a pointer to such a structure. For accessing a method, it can be applied to any declared type `T`  or `^T` such that `^T` is compatible with the type that implements this method. If neither field nor method with the specified name is found, an error is triggered.

As a special case, the field selector can be applied to the result of a function call that returns an expression list. This expression list is treated as if it were a structure with the fields named `item0`, `item1`, etc.    

Syntax:

```
fieldSelector = "." ident.
```

Examples:

```
v.x
data.print
f().item2
```

#### Call selector

The call selector `(...)` calls a function and accesses its returned value. It can be applied to a value of a function type, including methods. All actual parameters are passed by value. If the number and the types of the actual parameters are not compatible with the function signature, an error is triggered. A variadic function, whose signature has the last parameter of type `..T`, can receive any number of actual parameters of types compatible with `T` in place of this parameter.

Syntax:

```
callSelector = actualParams.
actualParams = "(" [expr {"," expr}] ")".
```

Examples:

```
f(x + 1, y + 1)
stop()
```

### Operators

Operators combine expressions into other expressions.

Syntax:

```
expr          = logicalExpr ["?" expr ":" expr].
logicalExpr   = logicalTerm {"||" logicalTerm}.
logicalTerm   = relation {"&&" relation}.
relation      = relationTerm [("==" | "!=" | "<" | "<=" | ">" | ">=") relationTerm].
relationTerm  = term {("+" | "-" | "|" | "~") term}.
term          = factor {("*" | "/" | "%" | "<<" | ">>" | "&") factor}.
factor        = designator | intNumber | realNumber | charLiteral | stringLiteral |
                 ("+" | "-" | "!" | "~") factor | "&" designator | "(" expr ")".
```

Examples:

```
2 + 2
-f(x) * (a[0] + a[1])
"Hello, " + person.name + '\n'
p != null && p[i] > 0
&Vec{2, 5}
x < 3 ? 42.5 : 60
```

#### Unary operators

```
+   Unary plus          Integers, reals
-   Unary minus         Integers, reals
~   Bitwise "not"       Integers
!   Logical "not"       Booleans
&   Address             Addressable designators
```

#### Binary operators

```
+   Sum                 Integers, reals, strings
-   Difference          Integers, reals
*   Product             Integers, reals
/   Quotient            Integers, reals
%   Remainder           Integers, reals
&   Bitwise "and"       Integers
|   Bitwise "or"        Integers
~   Bitwise "xor"       Integers
<<  Left shift          Integers
>>  Right shift         Integers
&&  Logical "and"       Booleans
||  Logical "or"        Booleans
==  "Equal"             Ordinals, reals, pointers, strings, arrays, structures
!=  "Not equal"         Ordinals, reals, pointers, strings, arrays, structures
>   "Greater"           Ordinals, reals, strings
<   "Less"              Ordinals, reals, strings
>=  "Greater or equal"  Ordinals, reals, strings
<=  "Less or equal"     Ordinals, reals, strings
```

The `/` operator performs an integer division (with the remainder discarded) if both operands are of integer types, otherwise it performs a real division.

The `&&` and `||` operators don't evaluate the second operand if the first operand is sufficient to evaluate the result.

The `==` and `!=` operators, when applied to arrays or structures, perform bitwise comparison.

##### Operand type conversions

Operand types are implicitly converted in two steps:

* The right operand type is converted to the left operand type
* The left operand type is converted to the right operand type

From the implicit type conversion rules it follows that:

* If one of the operands is a string and the other is a dynamic array, both are converted to the string type
* If one of the operands is an array and the other is a dynamic array, both are converted to the array type
* If one of the operands is a pointer and the other is a weak pointer, both are converted to the pointer type

#### Ternary operator

```
a ? b : c
```

The `? :` operator performs the conditional evaluation. For any expressions `a`, `b` and `c`, if `a` is `true`, it evaluates to `b` and skips `c`, otherwise it evaluates to `c` and skips `b`. The type of `a` must be compatible with `bool`. The operator result has the same type as `b`. The type of `c` is implicitly converted to the type of `b`. The expression `a` cannot be a ternary operator itself.

#### Operator precedence

```
*   /   %   <<   >>   &          Highest
+   -   |   ~
==  !=  <   <=   >   >=
&&
||
? :                              Lowest
```

## Statements

Statements perform some actions but don't evaluate to any value.

Syntax:

```
stmt       = decl | block | simpleStmt | 
             ifStmt | switchStmt | forStmt | breakStmt | continueStmt | returnStmt.
simpleStmt = assignmentStmt | shortAssignmentStmt | incDecStmt | callStmt.
```

A declaration in a block scope is also considered a statement.

### Block

A block is a statement list enclosed in braces. A block scope is associated with any block.

Syntax:

```
block    = "{" StmtList "}".
stmtList = stmt {";" stmt}.
```

### Assignment

An assignment evaluates the right-hand expression (or expression list) and assigns it to the left-hand side addressable designator (or designator list).

#### Full assignment

Syntax:

```
assignmentStmt  = singleAssgnStmt | listAssgnStmt.
singleAssgnStmt = designator "=" expr.
listAssgnStmt   = designatorList "=" exprList.
designatorList  = designator {"," designator}.
exprList   = expr {"," expr}.
```

Examples:

```
b = 2
person.msg = "Hello, " + person.name + '\n'
x[i], x[i + 1] = x[i + 1], x[i]
```

#### Short assignment

A short assignment combines one of the operators `+`, `-`, `*`, `/`, `%`, `&`, `|`, `~`  with assignment according to the following rule: `a op= b` is equivalent to `a = a op b`. 

Syntax:

```
shortAssignmentStmt = designator 
                      ("+=" | "-=" | "*=" | "/="  | "%=" |
                       "&=" | "|=" | "~=" | "<<=" | ">>=") expr.
```

Examples:

```
b += 2
mask[i] <<= 3
```

### Increment and decrement

Increments or decrements an addressable integer designator by 1.

Syntax:

```
incDecStmt = designator ("++" | "--").
```

Examples:

```
b++
numSteps[i]--
```

### Function or method call

Calls a function or method. If the returned value type is non-`void`, the value is discarded.

Syntax:

```
callStmt = designator.
```

Examples:

```
setValue(2, 4)
v[i].print()
```

### The `if` statement

Executes a block if a given Boolean expression evaluates to `true`. Optionally, executes another block if the expression evaluates to `false`. An optional short variable declaration may precede the Boolean expression. Its scope encloses the Boolean expression and the two blocks.

Syntax:

```
ifStmt = "if" [shortVarDecl ";"] expr block ["else" (ifStmt | block)].
```

Examples:

```
if a > max {max = a}

if x, ok := getValue(); ok {
    printf("Got %v\n", x)
} else {
    printf("Error\n")
}
```

### The `switch` statement

Executes one of several statement lists depending on the value of an ordinal expression or on the type of the value stored in an interface.

Syntax:

```
switchStmt = exprSwitchStmt | typeSwitchStmt.
```

#### Expression switch

Executes one of several statement lists depending on the value of the given ordinal expression. If the expression is equal to any of the constant expressions attached by a `case` label to a statement list, this statement list is executed and the control is transferred past the end of the `switch` statement. If no statement list is selected, the optional `default` statement list is executed. An optional short variable declaration may precede the ordinal expression. Its scope encloses the expression and all the statement lists.

Syntax:

```
exprSwitchStmt = "switch" [shortVarDecl ";"] expr "{" {exprCase} [default] "}".
exprCase       = "case" expr {"," expr} ":" stmtList.
default        = "default" ":" stmtList.
```

Example:

```
switch a {
    case 1, 3, 5, 7: printf("%d is odd\n", a)
    case 2, 4, 6, 8: printf("%d is even\n", a)
    default:         printf("I don't know")
}
```

#### Type switch

Executes one of several statement lists depending on the type of the value stored in an interface. If the interface can be explicitly converted to the type attached by a `case` label to a statement list, this statement list is executed and the control is transferred past the end of the `switch` statement. If no statement list is selected, the optional `default` statement list is executed. In each statement list, a variable is implicitly declared to store the result of the type conversion. The variable name must be specified in the `switch` statement header.

Syntax:

```
typeSwitchStmt = "switch" ident ":=" "type" "(" expr ")" "{" {typeCase} [default] "}".
typeCase       = "case" type ":" stmtList.
```

Example:

```
switch v := type(a) {
    case int: printf("int: %d + 5 = %d\n", v, v + 5)
    case str: printf("str: %s + 5 = %s\n", v, v + "5")
    default:  printf("unknown: %v\n", a)
}
```

### The `for` statement

Executes a block repeatedly.

Syntax:

```
forStmt = "for" (forHeader | forInHeader) block.
```

#### The general `for` statement

Executes a block while a given Boolean expression evaluates to `true`. The expression is re-evaluated before each iteration. An optional simple statement may follow the Boolean expression. If present, it is executed after executing the block and before re-evaluating the Boolean expression. An optional short variable declaration may precede the Boolean expression. Its scope encloses the Boolean expression, the simple statement and the block.

Syntax:

```
forHeader = [shortVarDecl ";"] expr [";" simpleStmt].
```

Examples:

```
for true {
    printf("Infinite loop\n")
}

for a > 1 {
    process(&a)
}

for k := 1; k <= 128; k *= 2 {
    printf("%v\n", k)
}
```

#### The `for...in` statement

Iterates through all items of an array, a dynamic array, a map or a string. Before each iteration, the index (or key) and the value of the next item are evaluated and assigned to the corresponding variables declared via an implicit short variable declaration in the statement header. 

Let the item type be `T`. If the item variable in the statement header is followed by a `^`, then its type is `^T` and its value is the pointer to the original item. Otherwise, its type is `T` and its value is the copy of the original item. Strings admit only the latter form.  

The item variable may be omitted.

Syntax:

```
forInHeader = ident ["," ident ["^"]] "in" expr.
```

Examples:

```
for i in a {
    sum += a[i]
}

for index, item in data {
    if item > maxItem {
        maxItem = item
        maxIndex = index
    }
}

for index, item^ in data {
    item^ = f(index)            // Modify data
}
```

### The `break` statement

Terminates the execution of the innermost enclosing `for` statement and transfers the control past the end of the `for` statement.

Syntax:

```
breakStmt = "break".
```

Examples:

```
for i, x in a {
    if fabs(x) > 1e12 {break}
    sum += x
}
```

### The `continue` statement

Terminates the execution of the current iteration of the innermost enclosing `for` statement and transfers the control to the point immediately before the end of the `for` statement block.

Syntax:

```
continueStmt = "continue".
```

Examples:

```
for i, x in a {
    if x < 0 {continue}
    sum += x
}
```

### The `return` statement

Terminates the execution of the innermost enclosing function and transfers the control to the calling function. For functions with non-`void` return value types, the `return` keyword should be followed by an expression or expression list whose types are compatible with the return value types declared in the function signature.

For functions with non-`void` return value types, the function block should have at least one `return` statement. 

Syntax:

```
returnStmt = "return" [exprList].
```

Examples:

```
fn doNothing() {
    return
}

fn sqr(x: real): real {
    return x * x
}

fn p(a: int): (int, bool) {
    return 2 * a, true
}
```

## Modules

An Umka program consists of one or more source files, or modules. If the main module has a `main()` function with no parameters and no return values, this function becomes the entry point of the program.

Modules can be imported from other modules using the `import` clause in the beginning of the importing module. In this case, all the identifiers declared as exported in the module scope of the imported module become valid in the importing module, if qualified with the imported module name.

The imported module name is specified as a string literal containing the module path. It can be assigned an optional alias that will be used instead of the module name.

Syntax:

```
program    = module.
module     = [import ";"] decls.
import     = "import" (importItem | "(" {importItem ";"} ")").
importItem = [ident "="] stringLiteral.
```

Examples:

```
import "std.um"
fn main() {
    std::println("Hello, World!")
}

import (
        utils1 = "lib1/utils.um"
        utils2 = "lib2/utils.um"
)
fn main() {
        utils1::f()
        utils2::f()
}
```

## Appendix: Language grammar

```
program             = module.
module              = [import ";"] decls.
import              = "import" (importItem | "(" {importItem ";"} ")").
importItem          = [ident "="] stringLiteral.
decls               = decl {";" decl}.
decl                = typeDecl | constDecl | varDecl | fnDecl.
typeDecl            = "type" (typeDeclItem | "(" {typeDeclItem ";"} ")").
typeDeclItem        = ident exportMark "=" type.
constDecl           = "const" (constDeclItem | "(" {constDeclItem ";"} ")").
constDeclItem       = ident exportMark "=" expr.
varDecl             = fullVarDecl | shortVarDecl.
fullVarDecl         = "var" (varDeclItem | "(" {varDeclItem ";"} ")").
varDeclItem         = typedIdentList "=" exprList.
shortVarDecl        = declAssignmentStmt.
fnDecl              = "fn" [rcvSignature] ident exportMark signature [block].
rcvSignature        = "(" ident ":" type ")".
signature           = "(" [typedIdentList ["=" expr] {"," typedIdentList ["=" expr]}] ")" 
                      [":" (type | "(" type {"," type} ")")].
exportMark          = ["*"].
identList           = ident exportMark {"," ident exportMark}.
typedIdentList      = identList ":" [".."] type.
type                = qualIdent | ptrType | arrayType | dynArrayType | strType | enumType | 
                      mapType | structType | interfaceType | closureType.
ptrType             = ["weak"] "^" type.
arrayType           = "[" expr "]" type.
dynArrayType        = "[" "]" type.
strType             = "str".
enumType            = "enum" ["(" type ")"] "{" {enumItem ";"} "}".
enumItem            = ident ["=" expr].
mapType             = "map" "[" type "]" type.
structType          = "struct" "{" {typedIdentList ";"} "}".
interfaceType       = "interface" "{" {(ident signature | qualIdent) ";"} "}".
closureType         = "fn" signature.
block               = "{" StmtList "}".
fnBlock             = block.
fnPrototype         = .
stmtList            = Stmt {";" Stmt}.
stmt                = decl | block | simpleStmt | ifStmt | switchStmt | forStmt |
                      breakStmt | continueStmt | returnStmt.
simpleStmt          = assignmentStmt | shortAssignmentStmt | incDecStmt | callStmt.
singleAssgnStmt     = designator "=" expr.
listAssgnStmt       = designatorList "=" exprList.
assignmentStmt      = singleAssgnStmt | listAssgnStmt.
shortAssignmentStmt = designator 
                      ("+=" | "-=" | "*=" | "/="  | "%=" |
                       "&=" | "|=" | "~=" | "<<=" | ">>=") expr.
singleDeclAssgnStmt = ident ":=" expr.
listDeclAssgnStmt   = identList ":=" exprList.
declAssignmentStmt  = singleDeclAssgnStmt | listDeclAssgnStmt.
incDecStmt          = designator ("++" | "--").
callStmt            = designator.
ifStmt              = "if" [shortVarDecl ";"] expr block ["else" (ifStmt | block)].
switchStmt          = exprSwitchStmt | typeSwitchStmt.
exprSwitchStmt      = "switch" [shortVarDecl ";"] expr "{" {exprCase} [default] "}".
exprCase            = "case" expr {"," expr} ":" stmtList.
typeSwitchStmt      = "switch" ident ":=" "type" "(" expr ")" "{" {typeCase} [default] "}".
typeCase            = "case" type ":" stmtList.
default             = "default" ":" stmtList.
forStmt             = "for" (forHeader | forInHeader) block.
forHeader           = [shortVarDecl ";"] expr [";" simpleStmt].
forInHeader         = ident ["," ident ["^"]] "in" expr.
breakStmt           = "break".
continueStmt        = "continue".
returnStmt          = "return" [exprList].
exprList            = expr {"," expr}.
expr                = logicalExpr ["?" expr ":" expr].
logicalExpr         = logicalTerm {"||" logicalTerm}.
logicalTerm         = relation {"&&" relation}.
relation            = relationTerm 
                      [("==" | "!=" | "<" | "<=" | ">" | ">=") relationTerm].
relationTerm        = term {("+" | "-" | "|" | "~") term}.
term                = factor {("*" | "/" | "%" | "<<" | ">>" | "&") factor}.
factor              = designator | intNumber | realNumber | charLiteral | stringLiteral |
                      ("+" | "-" | "!" | "~") factor | "&" designator | "(" expr ")".
designatorList      = designator {"," designator}.
designator          = (primary | typeCast | compositeLiteral | enumConst) selectors.
primary             = qualIdent | builtinCall.
qualIdent           = [ident "::"] ident.
builtinCall         = qualIdent "(" [expr {"," expr}] ")".
selectors           = {derefSelector | indexSelector | fieldSelector | callSelector}.
derefSelector       = "^".
indexSelector       = "[" expr "]".
fieldSelector       = "." ident.
callSelector        = actualParams.
actualParams        = "(" [expr {"," expr}] ")".
compositeLiteral    = [type] (arrayLiteral | dynArrayLiteral | mapLiteral | 
                      structLiteral | closureLiteral).
arrayLiteral        = "{" [expr {"," expr}] "}".
dynArrayLiteral     = arrayLiteral.
mapLiteral          = "{" expr ":" expr {"," expr ":" expr} "}".
structLiteral       = "{" [[ident ":"] expr {"," [ident ":"] expr}] "}".
closureLiteral      = ["|" ident {"," ident} "|"] fnBlock.
typeCast            = type "(" expr ")".
enumConst           = [type] "." ident.
ident               = (letter | "_") {letter | "_" | digit}.
intNumber           = decNumber | hexHumber.
decNumber           = digit {digit}.
hexNumber           = "0" ("X | "x") hexDigit {hexDigit}.
realNumber          = decNumber ["." decNumber] [("E" | "e") decNumber].
charLiteral         = "'" (char | escSeq) "'".
stringLiteral       = """ {char | escSeq} """.
escSeq              = "\" ("0" | "a" | "b" | "e" | "f" | "n" | "r" | "t" | "v" | 
                           "x" hexNumber).
letter              = "A".."Z" | "a".."z".
digit               = "0".."9".
hexDigit            = digit | "A".."F" | "a".."f".
char                = "\x00".."\xFF".
```

