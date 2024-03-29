# Binary trees benchmark - Python version by Antoine Pitrou et al.

from __future__ import print_function

import time

# Python3 has time.perf_counter instead time.clock
try:
    time.clock = time.perf_counter
except:
    pass

# Map "range" to an efficient range in both Python 2 and 3.
try:
    range = xrange
except NameError:
    pass

def make_tree(item, depth):
    if not depth: return item, None, None
    item2 = item + item
    depth -= 1
    return item, make_tree(item2 - 1, depth), make_tree(item2, depth)

def check_tree(node):
    item, left, right = node
    if not left: return item
    return item + check_tree(left) - check_tree(right)

min_depth = 4
max_depth = 12
stretch_depth = max_depth + 1

start = time.clock()
print("stretch tree of depth %d check:" % stretch_depth, check_tree(make_tree(0, stretch_depth)))

long_lived_tree = make_tree(0, max_depth)

iterations = 2 ** max_depth
for depth in range(min_depth, stretch_depth, 2):

    check = 0
    for i in range(1, iterations + 1):
        check += check_tree(make_tree(i, depth)) + check_tree(make_tree(-i, depth))

    print("%d trees of depth %d check:" % (iterations * 2, depth), check)
    iterations //= 4

print("long lived tree of depth %d check:" % max_depth, check_tree(long_lived_tree))
print("elapsed: " + str(time.clock() - start))
