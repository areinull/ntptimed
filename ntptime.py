#!/usr/bin/env python
# -*- coding: utf-8 -*-

import Ntptime

################################################################################
class ntptime:
    """
    Simple API for ntptime source
    """
    ####################
    def __init__(self):
        self.t = Ntptime.ntptime_ref()
        self.cont = Ntptime.ntptime_shm_ref()
        err=Ntptime.ntptime_shm_init(self.cont)
        if (err!=0):
            Ntptime.ntptime_free(self.t)
            del self.t
            Ntptime.ntptime_shm_free(self.cont)
            del self.cont
            raise Exception("can't init ntptime shm context, err - " + str(err))
    ####################
    def __del__(self):
        if hasattr(self,"t"):
            Ntptime.ntptime_free(self.t)
            del self.t
        if hasattr(self,"cont"):
            Ntptime.ntptime_shm_free(self.cont)
            del self.cont

    ####################
    def time(self):
        """
        Return last ntp timestamp
        """
        err=Ntptime.ntptime_shm_getlastts(self.cont, self.t)
        if (err!=0):
            raise Exception("can't get last ts, err - " + str(err))

        return Ntptime.ntptime_to_u64(self.t)
