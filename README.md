<img src="https://github.com/vtereshkov/umka-lang/blob/master/resources/logo.svg" width="320">

[![CI](https://github.com/vtereshkov/umka-lang/workflows/CI/badge.svg)](https://github.com/vtereshkov/umka-lang/actions)

# Welcome to Umka!
Umka is a statically typed embeddable scripting language. It combines the simplicity and flexibility needed for scripting with a compile-time protection against type errors. Its aim is to follow the Python Zen principle _Explicit is better than implicit_ more consistently than dynamically typed languages generally do.

[Go to Playground](https://vtereshkov.github.io/umka-lang)

## Features
* Clean syntax inspired by Go
* Cross-platform bytecode compiler and virtual machine
* Garbage collection
* Arrays and structures compatible with C
* Polymorphism via interfaces
* Multitasking based on fibers
* Type inference
* Distribution as a dynamic library with a simple C API
* C99 source

## Performance
Due to the efficient `for` loop implementation and support for static data structures, Umka demonstrates high performance in multidimensional array operations and similar tasks.

_400 x 400 matrix multiplication (AMD A4-3300M @ 1.9 GHz, Windows 7)_
![](resources/perf.png)

## Getting Started
* Try Umka in the [playground](https://vtereshkov.github.io/umka-lang)
* Download the [latest release](https://github.com/vtereshkov/umka-lang/releases) for Windows and Linux
* Take a [tour of Umka](https://github.com/vtereshkov/umka-lang/blob/master/README.md#a-tour-of-umka)
* Read the [language specification](https://github.com/vtereshkov/umka-lang/blob/master/spec.md)
* Explore the [raytracer](https://github.com/vtereshkov/umka-lang/blob/master/examples/raytracer/raytracer.um) example that demonstrates many language features like fibers, interfaces and dynamic arrays
* Play with the toy [Lisp interpreter](https://github.com/vtereshkov/umka-lang/blob/master/examples/lisp) written in Umka
* Try the more realistic [C](https://github.com/vtereshkov/umka-lang/blob/master/examples/3dcam/3dcam.c)+[Umka](https://github.com/vtereshkov/umka-lang/blob/master/examples/3dcam/3dcam.um) embedded scripting example (_Note:_ [raylib](https://www.raylib.com) is required to compile and run it)
* See what [real-life projects](https://github.com/vtereshkov/umka-lang/blob/master/README.md#projects-in-umka) successfully use Umka
* If you are familiar with Go, read about the [differences](https://github.com/vtereshkov/umka-lang/blob/master/README.md#umka-vs-go)

_Raytracer example_

![](examples/raytracer/raytracer.png)

_C+Umka 3D camera example_

![](examples/3dcam/3dcam.png)

## Projects in Umka

* [VDrift/Umka](https://github.com/vtereshkov/vdrift) - a racing simulator that lets you design, tune and test your own car autopilot

![](resources/vdrift.png)

* [tophat](https://github.com/marekmaskarinec/tophat) - a 2D game engine focused on minimalism

![](resources/tophat.png)

* TractorSim3D - a 6 DOF tractor dynamics simulator with a scriptable steering controller and [raylib](https://www.raylib.com)-based graphics

![](resources/tractor.png)

## A Tour of Umka
### Hello
```
fn main() {
    printf("Hello Umka!\n")
}
```
### Declarations
#### Constants
```
const a = 3
const b* = 2.38                         // Exported identifier
const (
    c = sin(b) / 5
    d = "Hello" + " World"
)
```
#### Types
```
type IntPtr = ^uint16                   // Pointer
type Arr = [a]real                      // Array
type (
    DynArr = [][5]int                   // Dynamic array
    String = str                        // String
    MyMap = map[str]real                // Map
    Quat = struct {                     // Structure
        q: [4]real
        normalized: bool
    }
    Printable = interface {             // Interface
        print(): int
    }
    ErrFn = fn(code: int)               // Function
)
```
#### Variables
```
var e: int
var f: String = d + "!"
var (
    g: Arr = [3]real{2.3, -4.1 / 2, b}
    h: DynArr
    m: MyMap
)
q := Quat{q: [4]real{1, 0, 0, 0}, normalized: true}
```
#### Functions
```
fn tan(x: real): real {return sin(x) / cos(x)}
fn getValue(): (int, bool) {return 42, true}
```
#### Methods
```
fn (a: ^Arr) print(): int {
    printf("Arr: %s\n", repr(a^))
    return 0
}
```
### Statements
#### Assignment
```
h = make([][5]int, 3)   // Dynamic arrays and maps are initialized with make()
m = make(MyMap)
m["Hello Umka"] = 3.14
```
#### Declaration via assignment (with type inference)
```
sum := 0.0
```
#### Function call
```
y := tan(30 * std.pi / 180)
h = append(h, [5]int{10, 20, 30, 40, 50})
h = delete(h, 1)
```
#### Method call
```
g.print()
```
#### Conditional
```
if x, ok := getValue(); ok {
    std.println("Got " + repr(x))
}
```
#### Switch
```
switch a {
    case 1, 3, 5, 7: std.println(std.itoa(a) + " is odd")
    case 2, 4, 6, 8: std.println(std.itoa(a) + " is even")
    default:         std.println("I don't know")
}
```
#### Loop
```
    for k := 1; k <= 128; k *= 2 {
        std.println(repr(k))
    }
    
    for i, x in g {
        if fabs(x) > 1e12 {break}
        if x < 0 {continue}
        sum += x
    }
```
### Multitasking
```
fn childFunc(parent: fiber, buf: ^int) {
    for i := 0; i < 5; i++ {
        std.println("Child : i=" + std.itoa(i) + " buf=" + std.itoa(buf^))
        buf^ = i * 3
        fibercall(parent)
    }
}

fn parentFunc() {
    a := 0
    child := fiberspawn(childFunc, &a)    
    for i := 0; i < 10; i++ {
        std.println("Parent: i=" + std.itoa(i) + " buf=" + std.itoa(a))
        a = i * 7
        if fiberalive(child) {
            fibercall(child)
        }
    }    
}
```
### Functional tools
```
import "../import/fnc.um"

fn main() {
    var data: fnc.AnyArray = [6]fnc.Any{3, 7, 1, -4, 2, 5}
    printf("Array = %s\n", repr(data))
     
    sqr  := fn (x, ctx: fnc.Any): fnc.Any    {p := int(x); return p * p}
    less := fn (x, ctx: fnc.Any): bool       {return int(x) < int(ctx)} 
    sum  := fn (x, y, ctx: fnc.Any): fnc.Any {return int(x) + int(y)}     
    
    const max = 30     
    result := data.mapEach(sqr, null).filter(less, max).reduce(sum, null)    
    printf("Sum of all squares less than %lld = %s\n", max, repr(result))       
}
```
## Umka vs Go
### Purpose
While Go is a compiled systems programming language with a complex runtime library and big output binaries, Umka is a scripting language with a lightweight interpreter that can be easily embedded into any application as a shared library.

### Syntax
Umka is very similar to Go syntactically. However, in some aspects it's different. It has shorter keywords: `fn` for `func`, `str` for `string`, `in` for `range`. For better readability, it requires a `:` between variable names and types in declarations. It doesn't follow the [unfortunate C tradition](https://blog.golang.org/declaration-syntax) of pointer dereferencing. Instead of `*p`, it uses the Pascal syntax `p^`. As the `*` character is no longer used for pointers, it becomes the export mark, like in Oberon, so that a programmer can freely use upper/lower case letters in identifiers according to his/her own style. Type assertions don't have any special syntax; they look like pointer type casts.

### Semantics
Umka allows implicit type casts and supports default parameters in function declarations. It doesn't have slices as separate data types. Instead, it supports dynamic arrays, which are declared like Go's slices and initialized by calling `make()`. Method receivers must be pointers. The multithreading model in Umka is inspired by Lua and Wren rather than Go. It offers lightweight threads called fibers instead of goroutines and channels. The garbage collection mechanism is based on reference counting, so Umka needs to support `weak` pointers. Closures and Unicode support are under development.
