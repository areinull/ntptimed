#!/usr/bin/python
# -*- coding: utf-8 -*-

from scipy import *
from pylab import *
import struct
import os
import sys

try:
  if len(sys.argv)==1: raise
  elif len(sys.argv)>3: raise
  infile = sys.argv[1]
  if len(sys.argv)==3:
    if sys.argv[2]=='x' or sys.argv[2]=='X':
      drawX = True
    else: raise
  else:
    drawX = False
except:
  print """\
Illegal arguments. Usage:
plot_ntptimed_ts.py filename [x|X]
"""
  quit()

quantity=os.path.getsize(infile)/16
FH=open(infile,'rb')
tsd=list()
tsl=list()
tsd_cor=list()
tsl_cor=list()
deviation=list()
step=list()

for i in range(quantity):
  pack=FH.read(16)
  depack=struct.unpack("<4I",pack)
  tsd.append(depack[0]+depack[1]/4294967296.0)
  tsl.append(depack[2]+depack[3]/4294967296.0)

for i in range(quantity):
    tsd_cor.append(tsd[i]-tsd[0])
    tsl_cor.append(tsl[i]-tsl[0])

for i in range(quantity):
  deviation.append(abs(tsd_cor[i]-tsl_cor[i]))

for i in range(quantity-1):
  step.append(tsd_cor[i+1]-tsd_cor[i])

subplot(122)
text(0.05,0.05,'Total number of points '+str(quantity)+'\n\
Max tsd deviation '+str(max(deviation))+'\n\
Average tsd deviation '+str(mean(deviation))+'\n\
Max tsd step '+str(max(step))+'\n\
Min tsd step '+str(min(step))+'\n\
Average tsd step '+str(mean(step)))

subplot(121)
if drawX:
    plot(tsl_cor,tsd_cor,'x-')
else:
    plot(tsl_cor,tsd_cor)
xlabel('tsl [sec]')
ylabel('tsd [sec]')
title('timestamps local vs ntptimed')
grid(True)
#savefig(infile+'.png')

show()
