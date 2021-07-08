# Matrix multiplication benchmark - Python version

import time

# Python3 has time.perf_counter instead time.clock
try:
    time.clock = time.perf_counter
except:
    pass

size = 400

# Fill matrices
a = [[(3 * i + j) for j in range(size)] for i in range(size)]
b = [[(i - 3 * j) for j in range(size)] for i in range(size)]
c = [[0 for j in range(size)] for i in range(size)]

# Multiply matrices
start = time.clock()

for i in range(size):
    for j in range(size):
        s = 0.0
        for k in range(size): s += a[i][k] * b[k][j]
        c[i][j] = s

print("elapsed: " + str(time.clock() - start))

# Check result
check = 0.0
for i in range(size):
    for j in range(size): check += c[i][j]

print("check: " + str(check / size**2))