%module Ntptime

%{
#include "ntptime.h"
#include "crc8.h"

/* Function for correct work python bindings */
ntptime_shm_con *ntptime_shm_ref()
{
        ntptime_shm_con *cont;
        cont = (ntptime_shm_con *) malloc (sizeof(ntptime_shm_con));
        return cont;
}

void ntptime_shm_free(ntptime_shm_con *cont)
{
        ntptime_shm_deinit(cont);
        free(cont);
}

ntptime_t *ntptime_ref()
{
        ntptime_t *cont;
        cont = (ntptime_t *) malloc (sizeof(ntptime_t));
        return cont;
}



void ntptime_free(ntptime_t *cont)
{
        free(cont);
}

ntptime_t ntptime_get_ntp(ntptime_t *cont)
{
   ntptime_t res;
   res.sec = cont->sec;
   res.psec = cont->psec;
   return res;
}

unsigned int ntptime_get_sec(ntptime_t *cont)
{
        return cont->sec;
}

unsigned int ntptime_get_psec(ntptime_t *cont)
{
        return cont->psec;
}

void ntptime_set_sec(ntptime_t *t, unsigned int val)
{
        t->sec = val;
}

void ntptime_set_psec(ntptime_t *t, unsigned int val)
{
        t->psec = val;
}

%}

%typemap(out) u64
{
    $result=PyLong_FromUnsignedLongLong($1);
}

%typemap(in) u64
{
    if      (PyLong_Check($input))
        $1 = PyLong_AsUnsignedLongLongMask($input);
    else if (PyInt_Check($input))
        $1 = PyInt_AsUnsignedLongLongMask($input);
    else
    {
        PyErr_SetString(PyExc_ValueError,"Can't transform python object to u64");
        return NULL;
    }
}

%typemap(in) u32 {$1 = (u32) PyLong_AsLong($input);}

%include "ntptime.h"

ntptime_shm_con *ntptime_shm_ref();
void ntptime_shm_free(ntptime_shm_con *cont);

void ntptime_free(ntptime_t *cont);
ntptime_t *ntptime_ref();

unsigned int ntptime_get_sec(ntptime_t *cont);
unsigned int ntptime_get_psec(ntptime_t *cont);
void ntptime_set_sec(ntptime_t *t, unsigned int val);
void ntptime_set_psec(ntptime_t *t, unsigned int val);
ntptime_t ntptime_get_ntp(ntptime_t *cont);

%inline
{
int crc8_ts(u64 t)
{
  return crc8_31_ff((u8*)&t,8);
}
}
