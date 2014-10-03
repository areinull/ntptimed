import Ntptime as ntp
a=ntp.ntptime_ref()
b=ntp.ntptime_shm_ref()
ntp.ntptime_shm_init(b)
ntp.ntptime_shm_getlastts(b,a)

print "sec: " + str(ntp.ntptime_get_sec(a))
print "psec: " + str(ntp.ntptime_get_psec(a))

print "cppb: " + str((ntp.ntptime_get_sec(a)<<32)+ntp.ntptime_get_psec(a))

ntp.ntptime_free(a)
ntp.ntptime_shm_free(b)
