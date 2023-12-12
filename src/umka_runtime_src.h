#ifndef UMKA_RUNTIME_SRC_H_INCLUDED
#define UMKA_RUNTIME_SRC_H_INCLUDED

// This is an automatically generated file. Do not edit it

static const char *runtimeModuleNames[] = {"std.um", "fnc.um", "mat.um", "utf8.um"};

static const char *runtimeModuleSources[] = {

// std.um

"// Umka standard library\n"
"\n"
"// Memory\n"
"\n"
"fn rtlmemcpy(dest, src: ^void, count: int)\n"
"\n"
"fn tobytes*(buf: any): []uint8 {\n"
"    if buf.__self == null {\n"
"        error(\"Buffer is null\")\n"
"    }\n"
"    bytes := make([]uint8, sizeofself(buf))\n"
"    rtlmemcpy(&bytes[0], buf.__self, sizeofself(buf))\n"
"    return bytes\n"
"}\n"
"\n"
"fn frombytes*(buf: any, bytes: []uint8) {\n"
"    if buf.__self == null {\n"
"        error(\"Buffer is null\")\n"
"    }    \n"
"    if selfhasptr(buf) {\n"
"        error(\"Cannot access reference types\")\n"
"    }\n"
"    if sizeofself(buf) != len(bytes) {\n"
"        error(\"Illegal buffer size\")\n"
"    }\n"
"    rtlmemcpy(buf.__self, &bytes[0], sizeofself(buf))    \n"
"}\n"
"\n"
"// File I/O\n"
"\n"
"type File* = ^struct {}\n"
"\n"
"const (\n"
"    seekBegin* = 0\n"
"    seekCur*   = 1\n"
"    seekEnd*   = 2\n"
")\n"
"\n"
"fn rtlstdin(): File\n"
"fn stdin*(): File {return rtlstdin()}\n"
"\n"
"fn rtlstdout(): File\n"
"fn stdout*(): File {return rtlstdout()}\n"
"\n"
"fn rtlstderr(): File\n"
"fn stderr*(): File {return rtlstderr()}\n"
"\n"
"fn rtlfopen  (name: str, mode: str): File  \n"
"fn fopen*    (name: str, mode: str): File {return rtlfopen(name, mode)}\n"
"\n"
"fn rtlfclose (f: File): int\n"
"fn fclose*   (f: File): int {return rtlfclose(f)}\n"
"\n"
"fn rtlfread  (buf: ^void, size, cnt: int, f: File): int\n"
"fn fread*    (f: File, buf: any): int {\n"
"    if f == null {\n"
"        error(\"File is null\")\n"
"    }\n"
"    if bytes := ^[]int8(buf); bytes != null {\n"
"        return rtlfread(&bytes[0], len(bytes^), 1, f)\n"
"    }\n"
"    if ubytes := ^[]uint8(buf); ubytes != null {\n"
"        return rtlfread(&ubytes[0], len(ubytes^), 1, f)\n"
"    }        \n"
"    if chars := ^[]char(buf); chars != null {\n"
"        return rtlfread(&chars[0], len(chars^), 1, f)\n"
"    }\n"
"    if selfhasptr(buf) {\n"
"        error(\"Cannot read reference types except ^[]int8, ^[]uint8, ^[]char\")\n"
"    }\n"
"    return rtlfread(buf.__self, sizeofself(buf), 1, f)\n"
"}\n"
"\n"
"fn rtlfwrite (buf: ^void, size, cnt: int, f: File): int\n"
"fn fwrite*   (f: File, buf: any): int {\n"
"    if f == null {\n"
"        error(\"File is null\")\n"
"    }\n"
"    if bytes := ^[]int8(buf); bytes != null {\n"
"        return rtlfwrite(&bytes[0], len(bytes^), 1, f)\n"
"    }\n"
"    if ubytes := ^[]uint8(buf); ubytes != null {\n"
"        return rtlfwrite(&ubytes[0], len(ubytes^), 1, f)\n"
"    } \n"
"    if chars := ^[]char(buf); chars != null {\n"
"        return rtlfwrite(&chars[0], len(chars^), 1, f)\n"
"    }             \n"
"    if selfhasptr(buf) {\n"
"        error(\"Cannot write reference types except ^[]int8, ^[]uint8, ^[]char\")\n"
"    }\n"
"    return rtlfwrite(buf.__self, sizeofself(buf), 1, f)\n"
"}\n"
"\n"
"fn rtlfseek  (f: File, offset, origin: int): int\n"
"fn fseek*    (f: File, offset, origin: int): int {\n"
"    if f == null {\n"
"        error(\"File is null\")\n"
"    }\n"
"    return rtlfseek(f, offset, origin)\n"
"}\n"
"\n"
"fn rtlftell (f: File): int\n"
"fn ftell*   (f: File): int {\n"
"    if f == null {\n"
"        error(\"File is null\")\n"
"    }\n"
"    return rtlftell(f)\n"
"}\n"
"\n"
"fn rtlremove (name: str): int\n"
"fn remove*   (name: str): int {return rtlremove(name)}\n"
"\n"
"fn rtlfeof  (f: File): int\n"
"fn feof*    (f: File): bool {\n"
"    if f == null {\n"
"        error(\"File is null\")\n"
"    }                \n"
"    return bool(rtlfeof(f))\n"
"}\n"
"\n"
"fn rtlfflush (f: File): int\n"
"fn fflush*   (f: File): bool { return bool(rtlfflush(f)) }\n"
"\n"
"// I/O utilities\n"
"\n"
"fn println*(s: str): int {return printf(\"%s\\n\", s)}\n"
"fn fprintln*(f: File, s: str): int {return fprintf(f, \"%s\\n\", s)}\n"
"\n"
"fn getchar*(): char {\n"
"    var c: char\n"
"    scanf(\"%c\", &c)\n"
"    return c\n"
"}\n"
"\n"
"// Strings\n"
"\n"
"fn ltrim*(s: str): str {\n"
"    start := -1\n"
"    slen := len(s)\n"
"    for i := 0; i < slen; i++ {\n"
"        if s[i] > \' \' {\n"
"            start = i\n"
"            break\n"
"        } \n"
"    }\n"
"    if start == -1 {return \"\"}\n"
"    return slice(s, start)\n"
"}\n"
"\n"
"fn rtrim*(s: str): str {\n"
"    stop := -1\n"
"    slen := len(s)\n"
"    for i := slen - 1; i >= 0; i-- {\n"
"        if s[i] > \' \' {\n"
"            stop = i\n"
"            break\n"
"        } \n"
"    }\n"
"    if stop == -1 {return \"\"}\n"
"    return slice(s, 0, stop + 1)\n"
"}\n"
"\n"
"fn trim*(s: str): str {\n"
"    return ltrim(rtrim(s))\n"
"}\n"
"\n"
"// Conversions\n"
"\n"
"fn atoi*(s: str): int {\n"
"    var x: int\n"
"    sscanf(s, \"%lld\", &x)\n"
"    return x\n"
"}\n"
"\n"
"fn atof*(s: str): real {\n"
"    var x: real\n"
"    sscanf(s, \"%lf\", &x)\n"
"    return x\n"
"}\n"
"\n"
"fn itoa*(x: int): str {\n"
"    s := sprintf(\"%lld\", x)\n"
"    return s\n"
"}\n"
"\n"
"fn ftoa*(x: real, decimals: int): str {\n"
"    fmt := sprintf(\"%%.%lldlf\", decimals)\n"
"    s := sprintf(fmt, x)\n"
"    return s\n"
"}\n"
"\n"
"// Math\n"
"\n"
"const pi* = 3.14159265358979323846\n"
"\n"
"// Random numbers\n"
"\n"
"const (\n"
"    junkBits = 8\n"
"    randSeedMax = 0xFFFFFFFF\n"
"    randMax* = randSeedMax >> junkBits\n"
")\n"
"\n"
"randSeed := 0\n"
"\n"
"fn srand*(seed: int) {randSeed = seed}\n"
"\n"
"fn rand*(): int {\n"
"    randSeed = (1664525 * randSeed + 1013904223) & randSeedMax\n"
"    return randSeed >> junkBits\n"
"}\n"
"\n"
"fn frand*(): real {return real(rand()) / randMax}\n"
"\n"
"// Time\n"
"\n"
"type DateTime* = struct {\n"
"    second, minute, hour: int\n"
"    day, month, year: int\n"
"    dayOfWeek, dayOfYear: int\n"
"    isDST: bool\n"
"}\n"
"\n"
"fn rtltime(): int\n"
"fn time*(): int {return rtltime()}\n"
"\n"
"fn rtlclock(): real\n"
"fn clock*(): real {return rtlclock()}\n"
"\n"
"fn rtllocaltime(t: int): DateTime\n"
"fn localtime*(t: int): DateTime {return rtllocaltime(t)}\n"
"\n"
"fn rtlgmtime(t: int): DateTime\n"
"fn gmtime*(t: int): DateTime {return rtlgmtime(t)}\n"
"\n"
"fn rtlmktime(d: ^DateTime): int\n"
"fn mktime*(d: DateTime): int {return rtlmktime(&d)}\n"
"\n"
"fn timestr*(d: DateTime): str {\n"
"    return sprintf(\"%4d-%02d-%02d %02d:%02d:%02d\", \n"
"                   d.year, d.month, d.day,\n"
"                   d.hour, d.minute, d.second)\n"
"}\n"
"\n"
"// Command line and environment\n"
"\n"
"fn argc*(): int {return len(rtlargv)}\n"
"\n"
"fn argv*(i: int): str {return rtlargv[i]}\n"
"\n"
"fn rtlgetenv(name: str): str\n"
"fn getenv*(name: str): str {return rtlgetenv(name)}\n"
"\n"
"fn rtlsystem(command: str): int\n"
"fn system*(command: str): int {return rtlsystem(command)}\n"
" ",

// fnc.um

"// Umka functional programming tools\n"
"\n"
"type Array* = []any\n"
"\n"
"fn (a: ^Array) transform*(f: fn (x: any): any): Array {\n"
"    res := make(Array, len(a^))\n"
"    for i := 0; i < len(a^); i++ {\n"
"        res[i] = f(a[i])\n"
"    }\n"
"    return res\n"
"}\n"
"\n"
"fn (a: ^Array) filter*(f: fn (x: any): bool): Array {\n"
"    res := make(Array, 0)\n"
"    for i := 0; i < len(a^); i++ {\n"
"        if f(a[i]) {\n"
"            res = append(res, a[i])\n"
"        }\n"
"    }\n"
"    return res\n"
"}\n"
"\n"
"fn (a: ^Array) reduce*(f: fn (x, y: any): any): any {\n"
"    res := a[0]\n"
"    for i := 1; i < len(a^); i++ {\n"
"        res = f(res, a[i])\n"
"    }\n"
"    return res\n"
"}\n"
" ",

// mat.um

"// Umka 3D vector/matrix library\n"
"\n"
"type (\n"
"    Vec* = [3]real\n"
"    Mat* = [3]Vec\n"
")\n"
"\n"
"\n"
"// Vector operations\n"
"\n"
"fn (u: ^Vec) add*(v: Vec): Vec         {return {u[0] + v[0], u[1] + v[1], u[2] + v[2]}}\n"
"fn (u: ^Vec) sub*(v: Vec): Vec         {return {u[0] - v[0], u[1] - v[1], u[2] - v[2]}}\n"
"fn (u: ^Vec) mul*(a: real): Vec        {return {u[0] * a, u[1] * a, u[2] * a}}\n"
"fn (u: ^Vec) div*(a: real): Vec        {return {u[0] / a, u[1] / a, u[2] / a}}\n"
"fn (u: ^Vec) dot*(v: Vec): real        {return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]}\n"
"fn (u: ^Vec) cross*(v: Vec): Vec       {return {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]}}\n"
"fn (u: ^Vec) elementwise*(v: Vec): Vec {return {u[0] * v[0], u[1] * v[1], u[2] * v[2]}}\n"
"fn (v: ^Vec) norm*(): real             {return sqrt(v.dot(v^))}\n"
"fn (v: ^Vec) normalize*(): Vec         {return v.div(v.norm())}\n"
"\n"
"\n"
"// Matrix operations\n"
"\n"
"fn (m: ^Mat) add*(n: Mat): Mat         {return {m[0].add(n[0]), m[1].add(n[1]), m[2].add(n[2])}}\n"
"fn (m: ^Mat) sub*(n: Mat): Mat         {return {m[0].sub(n[0]), m[1].sub(n[1]), m[2].sub(n[2])}}\n"
"fn (m: ^Mat) mul*(a: real): Mat        {return {m[0].mul(a), m[1].mul(a), m[2].mul(a)}}\n"
"fn (m: ^Mat) div*(a: real): Mat        {return {m[0].div(a), m[1].div(a), m[2].div(a)}}\n"
"fn (m: ^Mat) mulv*(v: Vec): Vec        {return {m[0].dot(v), m[1].dot(v), m[2].dot(v)}}\n"
"\n"
"fn (m: ^Mat) mulm*(n: Mat): Mat {\n"
"    var res: Mat\n"
"    for i := 0; i < 3; i++ {\n"
"        for j := 0; j < 3; j++ {\n"
"            for k := 0; k < 3; k++ {\n"
"                res[i][j] += m[i][k] * n[k][j]\n"
"            }\n"
"        }\n"
"    }\n"
"    return res\n"
"}\n"
"\n"
"fn identity*(): Mat {\n"
"    var res: Mat\n"
"    for i := 0; i < 3; i++ {\n"
"        res[i][i] = 1\n"
"    }\n"
"    return res\n"
"}\n"
"\n"
"fn (m: ^Mat) transpose*(): Mat {\n"
"    var res: Mat\n"
"    for i := 0; i < 3; i++ {\n"
"        for j := 0; j < 3; j++ {\n"
"            res[i][j] = m[j][i]\n"
"        }\n"
"    }\n"
"    return res\n"
"}\n"
"\n"
"fn (m: ^Mat) normalize*(): Mat {\n"
"    return m.add(m.mulm(identity().sub(m.transpose().mulm(m^))).mul(0.5))\n"
"}\n"
"\n"
"\n"
"// Rotations\n"
"\n"
"fn (v: ^Vec) toRateMat*(): Mat {\n"
"    return {{ 0,    -v[2],  v[1]},\n"
"            { v[2],  0,    -v[0]},\n"
"            {-v[1],  v[0],  0   }}\n"
"}\n"
"\n"
"fn (v: ^Vec) toAttMat*(): Mat {\n"
"    // v = {roll, pitch, yaw}\n"
"\n"
"    sr := sin(v[0]);  cr := cos(v[0])\n"
"    sp := sin(v[1]);  cp := cos(v[1])\n"
"    sy := sin(v[2]);  cy := cos(v[2])\n"
"\n"
"    rollMat  := Mat{{ 1,   0,   0 },\n"
"                    { 0,   cr, -sr},\n"
"                    { 0,   sr,  cr}}\n"
"\n"
"    pitchMat := Mat{{ cp,  0,   sp},\n"
"                    { 0,   1,   0 },\n"
"                    {-sp,  0,   cp}}\n"
"\n"
"    yawMat   := Mat{{ cy, -sy,  0 },\n"
"                    { sy,  cy,  0 },\n"
"                    { 0,   0,   1 }}\n"
"\n"
"    return yawMat.mulm(pitchMat).mulm(rollMat)\n"
"}\n"
"\n"
"fn (m: ^Mat) toAttAngles*(): Vec {\n"
"    roll  := atan2( m[2][1], m[2][2])\n"
"    pitch := atan2(-m[2][0], sqrt(m[2][1] * m[2][1] + m[2][2] * m[2][2]))\n"
"    yaw   := atan2( m[1][0], m[0][0])\n"
"    return {roll, pitch, yaw}\n"
"} ",

// utf8.um

"// UTF-8 utilities\n"
"\n"
"type Rune* = int32 \n"
"\n"
"const (\n"
"    errRune* = Rune(0xFFFD)\n"
"    errStr* = \"\\xEF\\xBF\\xBD\"\n"
"\n"
"    headerOnes = [6]uint8{0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8}                               \n"
")\n"
"\n"
"fn (r: ^Rune) size*(): int {\n"
"    if r^ <= 0x7F     {return 1}\n"
"    if r^ <= 0x7FF    {return 2}\n"
"    if r^ <= 0xFFFF   {return 3}\n"
"    if r^ <= 0x10FFFF {return 4}\n"
"    return -1\n"
"}\n"
"\n"
"fn (r: ^Rune) encode*(): str {\n"
"    size := r.size()\n"
"\n"
"    if size <= 0 {return errStr} \n"
"    if size == 1 {return str(char(r^))}\n"
"\n"
"    chars := make([]char, size + 1)\n"
"\n"
"    shift := (size - 1) * 6\n"
"    chars[0] = char(((r^ >> shift & 0xFF) | headerOnes[size]) & 0xFF)\n"
"\n"
"    for i := 1; i < size; i++ {\n"
"        shift -= 6\n"
"        chars[i] = char(r^ >> shift & 0x3F | 0x80)\n"
"    }\n"
"\n"
"    return str(chars)\n"
"}\n"
"\n"
"fn encode*(runes: []Rune): str {\n"
"    s := \"\"\n"
"    for _, r in runes {\n"
"        s += r.encode()\n"
"    }\n"
"    return s\n"
"}\n"
"\n"
"fn decodeRune*(chars: []char, pos: int): Rune {\n"
"    // Process header byte\n"
"    header := uint8(chars[pos])\n"
"\n"
"    if header & 0x80 == 0 {\n"
"        return Rune(header) // ASCII character\n"
"    }\n"
"\n"
"    size := 0\n"
"\n"
"    if header & headerOnes[3] == headerOnes[2] {\n"
"        size = 2\n"
"    } else if header & headerOnes[4] == headerOnes[3] {\n"
"        size = 3\n"
"    } else if header & headerOnes[5] == headerOnes[4] {\n"
"        size = 4\n"
"    }\n"
"\n"
"    if size == 0 {\n"
"        return errRune\n"
"    }\n"
"     \n"
"    shift := (size - 1) * 6\n"
"    rune := Rune(header & ~headerOnes[size + 1]) << shift\n"
"    \n"
"    // Process continuation bytes\n"
"    for i := 1; i < size; i++ {\n"
"        if pos + i >= len(chars) {\n"
"            return errRune\n"
"        }\n"
"\n"
"        byte := uint8(chars[pos + i])\n"
"        if byte & 0xC0 != 0x80 {\n"
"            return errRune\n"
"        }\n"
"\n"
"        shift -= 6        \n"
"        rune |= Rune(byte & 0x3F) << shift\n"
"    }\n"
"\n"
"    return rune\n"
"}\n"
"\n"
"fn decode*(s: str): []Rune {\n"
"    chars := []char(s)\n"
"    runes := []Rune{}\n"
"\n"
"    for pos := 0; pos < len(chars) {\n"
"        rune := decodeRune(chars, pos)\n"
"        if rune == errRune {\n"
"            break\n"
"        }\n"
"        runes = append(runes, rune)\n"
"        pos += rune.size()\n"
"    }\n"
"\n"
"    return runes\n"
"}\n"
"\n"
"fn runeCount*(s: str): int {\n"
"    chars := []char(s)    \n"
"    count := 0\n"
"\n"
"    for pos := 0; pos < len(chars) {\n"
"        rune := decodeRune(chars, pos)\n"
"        if rune == errRune {\n"
"            break\n"
"        }\n"
"        count++\n"
"        pos += rune.size()\n"
"    }\n"
"\n"
"    return count\n"
"}\n"
" "
};

#endif // UMKA_RUNTIME_SRC_H_INCLUDED
