// Matrix multiplication benchmark - Wren version

var size = 400
var a = []
var b = []
var c = []

// Fill matrices
for (i in 0...size) {
    a.add([])
    b.add([])
    c.add([])
    for (j in 0...size) {
        a[i].add(3 * i + j)
        b[i].add(i - 3 * j)
        c[i].add(0)
    }
}

// Multiply matrices
var start = System.clock

for (i in 0...size) {
    for (j in 0...size) {
        var s = 0.0
        for (k in 0...size) s = s + a[i][k] * b[k][j]
        c[i][j] = s
    }
}

System.print("elapsed: %(System.clock - start)")

// Check result
var check = 0.0
for (i in 0...size) {
    for (j in 0...size) check = check + c[i][j]
}

System.print("check: %(check / size / size)")
