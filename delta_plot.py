#!/bin/env python2
# coding: utf-8

import matplotlib.pyplot as pp

infile = '/tmp/deltas.txt'

points = []

with open(infile, 'r') as fd:
    for line in fd:
        t, d = line.rstrip('\n').split()
        t = eval(t)
        d = eval(d)
        points.append((t, d))
pp.plot([x[0] for x in points], [x[1] for x in points], '-xb')
pp.xlabel('time')
pp.ylabel('delta')
pp.grid(True)

pp.show()
exit()
