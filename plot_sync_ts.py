#!/usr/bin/python
# -*- coding: utf-8 -*-

from pylab import *
from scipy import *
import struct
import os
import sys
import Ntptime as ntp

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
plot_sync_ts.py filename [x|X]
"""
  quit()

quantity=os.path.getsize(infile)/18
FH=open(infile,'rb')
COROUT=open(infile+'.corr','wb')
tss=list()
tsl=list()
tss_cor=list()
tsl_cor=list()
deviation=list()
step=list()
anotherts = []
anothertsl = []
a=[]
b=[]

for i in range(quantity):
  pack=FH.read(10)
  depack=struct.unpack(">B2IB",pack)
  FH.seek(-9,1)
  pack1=FH.read(8)
  FH.seek(1,1)
  depack1=struct.unpack("<Q",pack1)
  crcOK = ntp.crc8_ts(depack1[0]) == depack[3]
#  print ntp.crc8_ts(depack1[0]), depack[3], crcOK
  if depack[0]==0xFF:
    a.append(depack[1]+depack[2]/4294967296.0)
    if crcOK:
      anotherts.append(depack[1]+depack[2]/4294967296.0)
      COROUT.write(pack)
#    else:
#      print crcOK
  tss.append(depack[1]+depack[2]/4294967296.0)
  pack=FH.read(8)
  depack2=struct.unpack("<2I",pack)
  tsl.append(depack2[0]+depack2[1]/4294967296.0)
  if depack[0]==0xFF:
    b.append(depack2[0]+depack2[1]/4294967296.0)
    if crcOK:
      anothertsl.append(depack2[0]+depack2[1]/4294967296.0)
#  print len(anotherts), len(tss)
for i in range(quantity):
    tss_cor.append(tss[i]-tss[0])
    tsl_cor.append(tsl[i]-tsl[0])
#    tss_cor.append(tss[i])
#    tsl_cor.append(tsl[i])

for i in range(quantity):
  deviation.append(abs(tss_cor[i]-tsl_cor[i]))

for i in range(quantity-1):
  step.append(tss_cor[i+1]-tss_cor[i])

figure()
subplot(122)
text(0.05,0.05,'Total number of points '+str(quantity)+'\n\
Max tss deviation '+str(max(deviation))+'\n\
Average tss deviation '+str(mean(deviation))+'\n\
Max tss step '+str(max(step))+'\n\
Min tss step '+str(min(step))+'\n\
Average tss step '+str(mean(step))+'\n\
First tss '+str(tss[0])+'\n\
First tsl '+str(tsl[0]))

subplot(121)
if drawX:
  plot(tsl_cor,tss_cor,'x-')
else:
  plot(tsl_cor,tss_cor)
xlabel('tsl [sec]')
ylabel('tss [sec]')
title('timestamps local vs sync')
grid(True)

figure()
if drawX:
  plot(anothertsl,anotherts,'x-')
else:
  plot(anothertsl,anotherts)
title('Sync correct TS (0xff start of pack)')
grid(True)

figure()
tsdif = [anotherts[i]-anothertsl[i] for i in range(len(anotherts))]
c = [a[i]-b[i] for i in range(len(a))]
print 'CRC errors:', len(c)-len(tsdif)
if drawX:
  plot([anothertsl[i]-b[0] for i in range(len(anothertsl))],
       tsdif, 'x-')
#  plot([b[i]-b[0] for i in range(len(b))], c, '^-')
else:
  plot([anothertsl[i]-b[0] for i in range(len(anothertsl))], tsdif)
title('sync - local time')
xlabel('local time')
ylabel('dif')
grid(True)

#savefig(infile+'.png')
show()
