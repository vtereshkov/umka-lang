# The Umka Language Reference

## Introduction

Umka is a statically typed embeddable scripting language. It combines the simplicity and flexibility needed for scripting with a compile-time protection against type errors.

Language grammar in this manual is specified in the EBNF notation.

## Lexical elements

A program consists of keywords, identifiers, numbers, character literals, string literals, operators, punctuation and comments.

### Keywords

Keywords have special meaning and cannot be used in any other role. Umka has the following keywords:

```
break case const continue default else for fn import 
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
&&   ||   !    ++   --
==   <    >    !=   <=   >=
=    :=   (    )    [    ]    {    }    
^    ;    :    .    ..
```

#### Implicit semicolons

Umka uses semicolons as statement terminators. To reduce the number of semicolons, an implicit semicolon is automatically inserted immediately after a line's final token if that token is

* An identifier
* A number, character literal or string literal
* `str`,  `break`,  `continue` or `return`
* `++`, `--`, `)`, `]`, `}`or `^`
* `*` as an export mark 

A semicolon may be omitted before a closing `)` or `}`.

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

## Data Types

Umka is a statically typed language. Each variable or constant has a type that is either explicitly specified in the variable declaration or inferred from the type of the right-hand side expression in a variable assignment or constant declaration. After a variable or constant is declared or assigned for the first time, it cannot change its type.

Syntax:

```
type = qualIdent | ptrType | arrayType | dynArrayType | strType | 
       mapType | structType | interfaceType | fnType.
qualIdent = [ident "."] ident.
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

The  `int`  and  `uint`  are the recommended 64-bit integer types. Other integer types are for compatibility with an external environment.

The two possible values of the Boolean type are `true` and `false`. 

#### Real types  

Umka supports the  `real32` and  `real`  floating-point types.  The  `real`  is the recommended 64-bit real type. The `real32`  type is for compatibility with an external environment.

#### Pointer types

A variable that stores a memory address of another variable has a pointer type. The type of that another variable is called the *base type* of the pointer type. The pointer type is specified by a `^`  followed by the base type specification. If the base type is unknown, it should be specified as `void`.

Umka performs automatic memory management using reference counting. All pointers are reference-counted by default. However, in processing data structures with cyclic references (like doubly-linked lists), reference counting is unable to deallocate memory properly. In this case, one of the pointers that constitute the cycle can be declared `weak` . A weak pointer is not reference-counted and its existence does not prevent the pointed-to variable from being deallocated. A weak pointer cannot be dereferenced. Instead, it should be first converted to a conventional (or strong) pointer of the same base type. If the weak pointer does not point to a valid dynamically allocated memory block, the result of this conversion is `null`.

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

An uninitialized pointer has the value `null`.

#### Function types

A constant or variable that stores an entry point of a function has a function type. A function is characterized by its *signature* that consists of a set of parameter names, types and optional default values, and an optional set of returned value types.

Syntax:

```
fnType    = "fn" signature.
signature = "(" [typedIdentList ["=" exprOrLit] {"," typedIdentList ["=" exprOrLit]}] ")" 
            [":" (type | "(" type {"," type} ")")].
```

Examples:

```
fn (code: int)
fn (p: int, x, y: real): (bool, real)
fn (msg: char, values: ..real)
```

The type of the last parameter in the function signature may be prefixed with `..`. The type `..T` is equivalent to `[]T` within the function. Such a function is called *variadic*. 

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

Let`T` be a non-pointer type. If a pointer of type `^T` is converted to the interface, the interface stores that pointer. If a variable of type `T`  is converted to the interface, the variable is first copied to a new memory location, and the interface stores the pointer to that location. In any case, the interface can be converted back to the type `T` or  `^T`. If a conversion of the interface to some non-pointer type `S` not equivalent to `T` is attempted, a runtime error is triggered. If a conversion of the interface to some pointer type `^S` not equivalent to `^T` is attempted, the result is `null`. 

Any type can be converted to the built-in type `any`, which is an alias for `interface{}`.

Interface assignment copies the pointer, but not the contents of the variable that has been converted to the interface.

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
* They are pointer types and the left-hand side pointer type has the `void` base type or the right-hand side is `null` 

### Type conversions

#### Implicit conversions

If a value `s` of type `S` is given where a value `t` of some other type `T` is expected, the `s` is implicitly converted to `t` if

* `S` and `T` are compatible
* `S` is an integer type and `T` is a real type
* `S` is `char` and `T` is `str`
* `S` is a `[]char` and `T` is `str`
* `S` is `str` and `T` is `[]char` 
* `S` is an array type and `T` is a dynamic array type and the item types of `S` and `T` are equivalent
* `S` is a dynamic array type and `T` is an array type and the item types of `S` and `T` are equivalent and `len(s) <= len(t)` 
* `S` is a type (or a pointer to a type) that implements all the methods of `T` and `T` is an interface type 
* `S` and `T` are interface types and `S` declares all the methods of `T`
* `S` is a weak pointer type and `T` is a strong pointer type and the base types of `S` and `T` are equivalent 
* `S` is a strong pointer type and `T` is a weak pointer type and the base types of `S` and `T` are equivalent

#### Explicit conversions

If a value `s` of type `S` is given where a value `t` of some other type `T` is expected, the `s` can be explicitly converted (cast) to `t` if

* `S` can be implicitly converted to `T`, possibly except the type name
* `S` and `T` are ordinal types
* `S` and `T` are pointer types and either `T` is `^void` or  `sizeof(s^) >= sizeof(t^)` and both `S` and `T` don't contain pointers
* `S` is an interface type and `T` is a type (or a pointer to a type) that was actually converted to `S`
* `S` is `[]U`, `T` is `[]V` and `U` can be explicitly converted to `V` 

## Declarations

All types, constants, variables and functions should be declared before the first use. The only exception is that a pointer base type `T` may be declared after using the pointer type `^T` but before the end of the same `type` declaration list. No identifier may be declared twice in the same block.

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

A constant declaration generally requires an explicit constant value. However, if a constant declaration is preceded by another integer constant declaration within the same declaration list, its explicit value can be omitted. In this case, the value will be set to the preceding constant value incremented by 1. 

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
varDeclItem    = typedIdentList "=" exprOrLitList.
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
listDeclAssgnStmt   = identList ":=" exprOrLitList.
declAssignmentStmt  = singleDeclAssgnStmt | listDeclAssgnStmt.
```

Examples:

```
a := 5
s, val := "Hello", sin(0.1 * a)
```

### Function and method declarations

A function or method declaration can be either a complete definition that includes the function block, or a *prototype* declaration if the function block is omitted. A prototype should be resolved somewhere below in the same module by duplicating the declaration, now with the function block. If a prototype is not resolved, it is considered an external C/C++ function. If such a function has not been registered via the Umka API, it is searched in the shared library (Umka module implementation library, UMI) `mod.umi`, where `mod` is the current module name. If the UMI does not exist or contains no such function, an error is triggered. 

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
    printf("Arr: %s\n", repr(a^))
    return 0
}
```

#### Built-in functions 

Built-in functions don't necessarily adhere to the general rules for functions.

##### Input/output functions

```
fn printf(format: str, a1: T1, a2: T2...): int
fn fprintf(f: ^std.File, format: str, a1: T1, a2: T2...): int
fn sprintf(format: str, a1: T1, a2: T2...): str
```

Write `a1`, `a2`... to the console, or to the file `f`, or to a string, according to the `format` string. `T1`, `T2`... should be ordinal, or real, or string types. Additional run-time type checking is performed to match the `format` string. `printf` and `fprintf` return the number of bytes written, `sprintf` returns the resulting string.

```
fn scanf(format: str, a1: ^T1, a2: ^T2...): int
fn fscanf(f: ^std.File, format: str, a1: ^T1, a2: ^T2...): int
fn sscanf(buf, format: str, a1: ^T1, a2: ^T2...): int
```

Read `a1`, `a2`... from the console, or from the file `f`, or from the string `buf`, according to the `format` string. `T1`, `T2`... should be ordinal, or real, or string types. Additional run-time type checking is performed to match the `format` string. Return the number of values read.

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
fn make([]T, length: int): []T    // (1)
fn make(map[K]T): map[K]T         // (2)
```

(1) Constructs a dynamic array of `length` items of type `T` initialized with zeroes. 

(2) Constructs an empty map with item type `T` indexed by keys of type `K`. 

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
type FiberFunc = fn(parent: fiber, anyParam: ^T)
fn fiberspawn(childFunc: FiberFunc, anyParam: ^T): fiber
```

Creates a new fiber and assigns `childFunc` as the fiber function. `anyParam` is a pointer to heap-allocated data buffer that will be passed to `childFunc` .

```
fn fibercall(child: fiber)
```

Resumes the execution of the `child` fiber. 

```
fn fiberalive(child: fiber)
```

Checks whether the `child` fiber function has not yet returned.  

##### Miscellaneous functions

```
fn repr(a: T): str
```

Returns the string representation of `a`.

```
fn exit()
```

Terminates the program without a run-time error.

```
fn error(msg: str)
```

Triggers a run-time error with the message `msg`.

## Expressions

### Primary expressions

A primary expression is either an identifier optionally qualified with a module name, or a built-in function call.

Syntax:

```
primary     = qualIdent | builtinCall.
qualIdent   = [ident "."] ident.
builtinCall = qualIdent "(" [expr {"," expr}] ")".
```

Examples:

```
i
std.File
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

A composite literal constructs a value for an array, dynamic array, map, structure or function and creates a new value each time it is evaluated. It consists of the type followed by the brace-bound list of item values (for arrays or dynamic arrays), list of key/item pairs (for maps), list of field values with optional field names (for structures) or the function body (for functions). 

The number of array item values or structure field values (if the optional field names are omitted) should match the literal type. The types of array item values, dynamic array item values, map key and item values, or structure field values should be compatible to those of the literal type.

#### Untyped composite literals

In the following special cases the composite literal type may be omitted and inferred from the context:

* Assignments, except short variable declarations
* Actual parameters
* Default parameter values
* The `return` statements
* Nested composite literals
* Map keys

Untyped composite literals cannot be used in general expressions. Implicit type conversions are never applied to untyped literals.

Syntax:

```
compositeLiteral = type untypedLiteral.
untypedLiteral   = arrayLiteral | dynArrayLiteral | mapLiteral | 
                   structLiteral | fnLiteral.
arrayLiteral     = "{" [exprOrLit {"," exprOrLit}] "}".
dynArrayLiteral  = arrayLiteral.
mapLiteral       = "{" exprOrLit ":" exprOrLit {"," exprOrLit ":" exprOrLit} "}".
structLiteral    = "{" [[ident ":"] exprOrLit {"," [ident ":"] exprOrLit}] "}".
fnLiteral        = fnBlock.
```

Examples:

```
[3]real{2.3, -4.1 / 2, b}
[]any{7.2, "Hello", [2]int{3, 5}}
map[[]int]str{[]int{13, 15}: "First", []int{57, 89}: "Second"}
Vec{x: 2, y: 8}
Mat{ {1, 2}, {3, 4} }             // Nested literals' types omitted
fn (x: int) {return 2 * x}
```

### Designators and selectors

A number of postfix *selectors* can be applied to a primary expression, a type cast or a composite literal to get a *designator*.

Syntax:

```
designator = (primary | typeCast | compositeLiteral) selectors.
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
indexSelector = "[" exprOrLit "]".
```

Examples:

```
b[5]
longVector[2 * i + 3 * j]
m["Hello"]
```

#### Field or method selector

The field or method selector `.`  accesses a field or method. For accessing a field, it can be applied to a structure that has this field or to a pointer to such a structure. For accessing a method, it can be applied to any declared type `T`  or `^T` such that `^T` is compatible with the type that implements this method. If neither field nor method with the specified name is found, an error is triggered.

Syntax:

```
fieldSelector = "." ident.
```

Examples:

```
v.x
data.print
```

#### Call selector

The call selector `(...)` calls a function and accesses its returned value. It can be applied to a value of a function type, including methods. All actual parameters are passed by value. If the number and the types of the actual parameters are not compatible with the function signature, an error is triggered. A variadic function, whose signature has the last parameter of type `..T`, can receive any number of actual parameters of types compatible with `T` in place of this parameter.

Syntax:

```
callSelector = actualParams.
actualParams = "(" [exprOrLit {"," exprOrLit}] ")".
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
exprOrLit     = expr | untypedLiteral.
expr          = logicalTerm {"||" logicalTerm}.
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
{"Hello", "World"}      // Literal type omitted
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
==  "Equal"             Ordinals, reals, pointers, strings, arrays, structures, functions
!=  "Not equal"         Ordinals, reals, pointers, strings, arrays, structures, functions
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

##### Operator precedence

```
*   /   %   <<   >>   &          Highest
+   -   |   ~
==  !=  <   <=   >   >=
&&
||                               Lowest
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
singleAssgnStmt = designator "=" exprOrLit.
listAssgnStmt   = designatorList "=" exprOrLitList.
designatorList  = designator {"," designator}.
exprOrLitList   = exprOrLit {"," exprOrLit}.
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
    printf("Got " + repr(x) + "\n")
} else {
    printf("Error\n")
}
```

### The `switch` statement

Executes one of several statement lists depending on the value of the given ordinal expression. If the expression is equal to any of the constant expressions attached by a `case` label to a statement list, this statement list is executed and the control is transferred past the end of the `switch` statement. If no statement list is selected, the optional `default` statement list is executed. An optional short variable declaration may precede the ordinal expression. Its scope encloses the expression and all the statement lists.

Syntax:

```
switchStmt = "switch" [shortVarDecl ";"] expr "{" {case} [default] "}".
case       = "case" expr {"," expr} ":" stmtList.
default    = "default" ":" stmtList.
```

Examples:

```
switch a {
    case 1, 3, 5, 7: printf(repr(a) + " is odd\n")
    case 2, 4, 6, 8: printf(repr(a) + " is even\n")
    default:         printf("I don't know")
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
    printf(repr(k) + '\n')
}
```

#### The `for...in` statement

Iterates through all items of an array, a dynamic array, a map or a string. Before each iteration, the index (or key) and the value of the next item are evaluated and assigned to the corresponding variables declared via an implicit short variable declaration in the statement header. The item variable may be omitted.

Syntax:

```
forInHeader = ident ["," ident] "in" expr.
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
returnStmt = "return" [exprOrLitList].
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

An Umka program consists of one or more source files, or modules. The main module should have the `main()` function with no parameters and no return values, which is the entry point of the program.

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
    std.println("Hello, World!")
}

import (
        utils1 = "lib1/utils.um"
        utils2 = "lib2/utils.um"
)
fn main() {
        utils1.f()
        utils2.f()
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
varDeclItem         = typedIdentList "=" exprOrLitList.
shortVarDecl        = declAssignmentStmt.
fnDecl              = "fn" [rcvSignature] ident exportMark signature [block].
rcvSignature        = "(" ident ":" type ")".
signature           = "(" [typedIdentList ["=" exprOrLit] {"," typedIdentList ["=" exprOrLit]}] ")" 
                      [":" (type | "(" type {"," type} ")")].
exportMark          = ["*"].
identList           = ident exportMark {"," ident exportMark}.
typedIdentList      = identList ":" [".."] type.
type                = qualIdent | ptrType | arrayType | dynArrayType | strType |
                      mapType | structType | interfaceType | fnType.
ptrType             = ["weak"] "^" type.
arrayType           = "[" expr "]" type.
dynArrayType        = "[" "]" type.
strType             = "str".
mapType             = "map" "[" type "]" type.
structType          = "struct" "{" {typedIdentList ";"} "}".
interfaceType       = "interface" "{" {(ident signature | qualIdent) ";"} "}".
fnType              = "fn" signature.
block               = "{" StmtList "}".
fnBlock             = block.
fnPrototype         = .
stmtList            = Stmt {";" Stmt}.
stmt                = decl | block | simpleStmt | ifStmt | switchStmt | forStmt |
                      breakStmt | continueStmt | returnStmt.
simpleStmt          = assignmentStmt | shortAssignmentStmt | incDecStmt | callStmt.
singleAssgnStmt     = designator "=" exprOrLit.
listAssgnStmt       = designatorList "=" exprOrLitList.
assignmentStmt      = singleAssgnStmt | listAssgnStmt.
shortAssignmentStmt = designator 
                      ("+=" | "-=" | "*=" | "/="  | "%=" |
                       "&=" | "|=" | "~=" | "<<=" | ">>=") expr.
singleDeclAssgnStmt = ident ":=" expr.
listDeclAssgnStmt   = identList ":=" exprOrLitList.
declAssignmentStmt  = singleDeclAssgnStmt | listDeclAssgnStmt.
incDecStmt          = designator ("++" | "--").
callStmt            = designator.
ifStmt              = "if" [shortVarDecl ";"] expr block ["else" (ifStmt | block)].
switchStmt          = "switch" [shortVarDecl ";"] expr "{" {case} [default] "}".
case                = "case" expr {"," expr} ":" stmtList.
default             = "default" ":" stmtList.
forStmt             = "for" (forHeader | forInHeader) block.
forHeader           = [shortVarDecl ";"] expr [";" simpleStmt].
forInHeader         = ident ["," ident] "in" expr.
breakStmt           = "break".
continueStmt        = "continue".
returnStmt          = "return" [exprOrLitList].
exprOrLitList       = exprOrLit {"," exprOrLit}.
exprOrLit           = expr | untypedLiteral.
expr                = logicalTerm {"||" logicalTerm}.
logicalTerm         = relation {"&&" relation}.
relation            = relationTerm 
                      [("==" | "!=" | "<" | "<=" | ">" | ">=") relationTerm].
relationTerm        = term {("+" | "-" | "|" | "~") term}.
term                = factor {("*" | "/" | "%" | "<<" | ">>" | "&") factor}.
factor              = designator | intNumber | realNumber | charLiteral | stringLiteral |
                      ("+" | "-" | "!" | "~") factor | "&" designator | "(" expr ")".
designatorList      = designator {"," designator}.
designator          = (primary | typeCast | compositeLiteral) selectors.
primary             = qualIdent | builtinCall.
qualIdent           = [ident "."] ident.
builtinCall         = qualIdent "(" [expr {"," expr}] ")".
selectors           = {derefSelector | indexSelector | fieldSelector | callSelector}.
derefSelector       = "^".
indexSelector       = "[" exprOrLit "]".
fieldSelector       = "." ident.
callSelector        = actualParams.
actualParams        = "(" [exprOrLit {"," exprOrLit}] ")".
compositeLiteral    = type untypedLiteral.
untypedLiteral      = arrayLiteral | dynArrayLiteral | mapLiteral | 
                      structLiteral | fnLiteral.
arrayLiteral        = "{" [exprOrLit {"," exprOrLit}] "}".
dynArrayLiteral     = arrayLiteral.
mapLiteral          = "{" exprOrLit ":" exprOrLit {"," exprOrLit ":" exprOrLit} "}".
structLiteral       = "{" [[ident ":"] exprOrLit {"," [ident ":"] exprOrLit}] "}".
fnLiteral           = fnBlock.
typeCast            = type "(" expr ")".
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

