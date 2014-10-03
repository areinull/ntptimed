import ntptime,time

ntp=ntptime.ntptime()

while (True):
    print ntp.time()
    time.sleep(0.5)
