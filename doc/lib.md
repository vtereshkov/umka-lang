# The Umka Standard Library Reference

All standard library modules are embedded into the Umka interpreter binary. They are not provided as standalone files.

## Basic library: `std.um`

### Error handling

#### Types

```
type ErrPos* = struct {
    file: str
    func: str
    line: int
}
```

Call stack trace position from where `error()` has been called.

```
type Err* = struct {
    code: int
    msg: str
    sender: any
    trace: []ErrPos
}
```

Error description. A zero `code` means no error.

```
type StdErr* = enum {
    ok     = 0           // No error
    buffer = 1           // Wrong buffer size
    ptr    = 2           // Pointers cannot be read or written
    nullf  = 3           // File is null
    eof    = 4           // End of file
}
```

Error codes set by the standard library functions in the `Err` structure. 

#### Functions

``` 
fn error*(code: int = 0, msg: str = "", sender: any = null): Err
```

Generates an error description with a call stack trace.

```
fn exitif*(err: Err)
```

If `err.code` is not zero, terminates the program, prints the error message and the call stack trace stored in `err`.

```
fn assert*(cond: bool, msg: str = "")
```

If `cond` is `false`, terminates the program, prints the error message `msg` and the call stack trace.

### Memory 

#### Functions

```
fn tobytes*(buf: any): []uint8
```

Copies the bytes of `buf` to a byte array.

```
fn frombytes*(buf: any, bytes: []uint8): Err
```

Copies the `bytes` array to `buf`. If unsuccessful, returns `StdErr.buffer` or `StdErr.ptr` in `Err`.

### Input/output

#### Types

```
type File* = ^struct {}
```

File handle.

```
type SeekFrom* = enum {
    begin                   // Beginning of file
    cur                     // Current position
    end                     // End of file
}
```

File seek origin. Used by `fseek()`. 

#### Functions

```
fn stdin* (): File
fn stdout*(): File
fn stderr*(): File
```

Return file handles for standard input, output, and error output devices.

```
fn fopen*(name: str, mode: str): (File, Err)
```

Opens the file specified by the `name` in the given `mode` (identical to C): 

```
"r"   read from a text file
"rb"  read from a binary file
"w"   write to a text file
"wb"  write to a binary file
```
Returns the file handle. If unsuccessful, returns `null` as a file handle and `StdErr.nullf` in `Err`.

```
fn fclose*(f: File): Err
```

Closes the file `f`. If unsuccessful, returns `StdErr.nullf` or `StdErr.eof` in `Err`.

```
fn fseek*(f: File, offset: int, origin: SeekFrom): Err
```

Sets the file pointer in the file `f` to the given `offset` from the `origin`. If unsuccessful, returns `StdErr.nullf` or `StdErr.eof` in `Err`.

```
fn ftell*(f: File): (int, Err)
```

Returns the file pointer position in the file `f`. If unsuccessful, returns `StdErr.nullf` or `StdErr.eof` in `Err`.

```
fn remove*(name: str): Err
```

Removes the file specified by the `name`. If unsuccessful, returns `StdErr.eof` in `Err`.

```
fn feof*(f: File): bool
```

Returns `true` if the file pointer position in the file `f` has reached the end of file.

```
fn fflush*(f: File): Err
```

Flushes the buffer for the file `f`. If unsuccessful, returns `StdErr.nullf` or `StdErr.eof` in `Err`.

```
fn fread*(f: File, buf: any): (int, Err)
```

Reads the `buf` variable from the file `f`. `buf` can be of any type that doesn't contain pointers, strings, dynamic arrays, interfaces, closures or fibers, except for `^[]int8`, `^[]uint8`, `^[]char`. Returns the number of bytes read. If unsuccessful, returns `StdErr.ptr`, `StdErr.nullf` or `StdErr.eof` in `Err`.

```
fn freadall*(f: File): ([]char, Err)
```

Reads all the contents of the file `f` to a dynamic array of `char` and returns this array. `f` should be opened in the `"rb"` mode. If unsuccessful, returns `StdErr.ptr`, `StdErr.nullf` or `StdErr.eof` in `Err`.

```
fn fwrite*(f: File, buf: any): (int, Err)
```

Writes the `buf` variable to the file `f`. `buf` can be of any type that doesn't contain pointers, strings, dynamic arrays, interfaces, closures or fibers, except for `^[]int8`, `^[]uint8`, `^[]char`. Returns the number of bytes written. If unsuccessful, returns `StdErr.ptr`, `StdErr.nullf` or `StdErr.eof` in `Err`.

```
fn println*(s: str): int
fn fprintln*(f: File, s: str): int
```

Write the string `s`  followed by a newline character to the console or to the file `f`. Return the number of bytes written.

```
fn getchar*(): char
```

Returns a character read from the console.

### Strings

#### Functions

```
fn ltrim*(s: str): str                  // Trim leading spaces and tabs
fn rtrim*(s: str): str                  // Trim trailing spaces and tabs
fn trim*(s: str): str                   // Trim leading and trailing spaces and tabs
```

Trim all leading and/or trailing spaces and tabs from string `s`. Return a new string. 

### Conversions

#### Functions

```
fn atoi*(s: str): int                   // String to integer
fn atof*(s: str): real                  // String to real
fn itoa*(x: int): str                   // Integer to string
fn ftoa*(x: real, decimals: int): str   // Real to string with `decimals` decimal places 
```

### Math

#### Constants

```
const pi* = 3.14159265358979323846
const randMax* = /* Implementation-defined */
```

#### Functions

```
fn srand*(seed: int)
```

Initializes the pseudo-random number generator with `seed`.

```
fn rand*(): int
```

Returns an integer pseudo-random number between 0 and `randMax` inclusive.

```
fn frand*(): real
```

Returns a real pseudo-random number between 0 and 1 inclusive.

### Time

#### Types

```
type DateTime* = struct {
    second, minute, hour: int
    day, month, year: int
    dayOfWeek, dayOfYear: int
    isDST: bool
}
```

Date/time structure.

#### Functions

```
fn time*(): int
```

Returns the number of seconds since 00:00, January 1, 1970 UTC.

```
fn clock*(): real
```

Returns the number of seconds since the start of the program.

```
fn localtime*(t: int): DateTime
```

Converts the time `t` to a date/time structure. `t` is treated as local time.

```
fn gmtime*(t: int): DateTime
```

Converts the time `t` to a date/time structure. `t` is treated as UTC time.

```
fn mktime*(d: DateTime): int
```

Converts the date/time structure `d` to time.

```
fn timestr*(d: DateTime): str
```

Returns the string representation of the date/time structure `d`.

### Command line and environment

#### Functions

```
fn argc*(): int
```

Returns the number of command line parameters.

```
fn argv*(i: int): str
```

Returns the `i`-th command line parameter, where `i` should be between 0 and  `argc() - 1 ` inclusive.

```
fn getenv*(name: str): str
```

Returns the environment variable with the specified `name`.

```
fn system*(command: str): int
```

Invokes the command processor to execute a `command`. Returns a platform-specific result.

## Functional programming library: `fnc.um`

#### Types

```
type Array* = []any
```
Array of any items.

#### Functions

```
fn (a: ^Array) transform*(f: fn (x: any): any): Array
```
Computes the function `f` for each item `x` of the array `a^` and returns the array of results.

```
fn (a: ^Array) filter*(f: fn (x: any): bool): Array 
```
Computes the function `f` for each item `x` of the array `a^` and returns the array of all the items of `a^` for which `f` evaluates to `true`.

```
fn (a: ^Array) reduce*(f: fn (x, y: any): any): any
```
Computes the function `f` for each item `y` of the array `a^` starting from index `i = 1` to `len(a^) - 1`. The previous result of `f` computed for items 1 to `i - 1` is passed as `x`. For `i = 1` it is assumed that `x = a[0]`. Returns the result of `f` computed for the last item of `a^`.

Example:
```
import "fnc.um"

fn main() {
    data := []int{3, 7, 1, -4, 2, 5}
    printf("Array = %v\n", data)

    max := 30
     
    sqr  := fn (x: any): any        {return int(x) * int(x)}
    less := fn (x: any): bool |max| {return int(x) < max} 
    sum  := fn (x, y: any): any     {return int(x) + int(y)}     
    
    result := int(fnc.Array(data).transform(sqr).filter(less).reduce(sum))   
    printf("Sum of all squares less than %lld = %lld \n", max, result)     
}
```

## 3D vector/matrix library: `mat.um`

### Vector operations

#### Types

```
type Vec* = [3]real
```
A 3D vector.

#### Functions

```
fn (u: ^Vec) add*(v: Vec): Vec         // Addition: u + v
fn (u: ^Vec) sub*(v: Vec): Vec         // Subtraction: u - v
fn (u: ^Vec) mul*(a: real): Vec        // Multiplication by a scalar: u * a 
fn (u: ^Vec) div*(a: real): Vec        // Division by a scalar: u / a
fn (u: ^Vec) dot*(v: Vec): real        // Dot product: u * v
fn (u: ^Vec) cross*(v: Vec): Vec       // Cross product: u x v
fn (u: ^Vec) elementwise*(v: Vec): Vec // Element-wise multiplication
fn (v: ^Vec) norm*(): real             // Norm: |v|
fn (v: ^Vec) normalize*(): Vec         // Normalized vector: v / |v|
```

### Matrix operations

#### Types

```
type Mat* = [3]Vec
```
A 3x3 matrix.

#### Functions

```
fn (m: ^Mat) add*(n: Mat): Mat         // Addition: m + n
fn (m: ^Mat) sub*(n: Mat): Mat         // Subtraction: m - n
fn (m: ^Mat) mul*(a: real): Mat        // Multiplication by a scalar: m * a 
fn (m: ^Mat) div*(a: real): Mat        // Division by a scalar: m / a
fn (m: ^Mat) mulv*(v: Vec): Vec        // Multiplication by a vector: m * v
fn (m: ^Mat) mulm*(n: Mat): Mat        // Multiplication by a matrix: m * n
fn identity*(): Mat                    // Identity matrix 
fn (m: ^Mat) transpose*(): Mat         // Transpose 
fn (m: ^Mat) normalize*(): Mat         // Orthogonalized matrix
```

### Rotations

#### Functions

```
fn (v: ^Vec) toRateMat*(): Mat
```
Returns the skew-symmetric angular rate matrix `o` given the angular rate vector `v`, such that for any attitude matrix `m`, its value after the infinitesimal time interval `dt` will be `m + m * o * dt`.

```
fn (v: ^Vec) toAttMat*(): Mat
```
Returns the attitude matrix `m` given the attitude angles (roll, pitch, yaw) formally combined into the vector `v`. Attitude angles are in radians. Any vector `u` can be transformed from the rotating to the non-rotating coordinate system as `m * u`.

```
fn (m: ^Mat) toAttAngles*(): Vec 
```
Returns the attitude angles (roll, pitch, yaw) formally combined into a vector given the attitude matrix `m`. Attitude angles are in radians. Any vector `u` can be transformed from the rotating to the non-rotating coordinate system as `m * u`.

## Unicode library: `utf8.um`

#### Types

```
type Rune* = int32                                
```
A rune, i.e., an integer value identifying a Unicode code point.

#### Constants

```
const (
    errRune* = Rune(0xFFFD)
    errStr* = "\xEF\xBF\xBD"                          
)
```
The invalid rune and its UTF-8 encoding. Returned from functions on encoding/decoding errors.

#### Functions

```
fn (r: ^Rune) size*(): int
```
Returns the size in bytes of the rune `r` encoded in UTF-8.

```
fn (r: ^Rune) encode*(): str
```
Returns the string encoded in UTF-8 corresponding to the rune `r`.

```
fn encode*(runes: []Rune): str
```
Returns the string encoded in UTF-8 corresponding to the array `runes`.

```
fn decodeRune*(chars: []char, pos: int): Rune
```
Returns the rune contained in the array `chars` starting at index `pos`.

```
fn decode*(s: str): []Rune
```
Returns the array of runes corresponding to the string `s` encoded in UTF-8.

```
fn runeCount*(s: str): int
```
Returns the number of runes contained in the string `s` encoded in UTF-8.
