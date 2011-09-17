/*
 *  Copyright (c) 2010 Luca Abeni
 *  Copyright (c) 2010 Csaba Kiraly
 *
 *  This is free software; see gpl-3.0.txt
 */
#include <sys/time.h>
#include <stdio.h>
#include <stdarg.h>

#include "dbg.h"

static struct timeval tnow;

int ftprintf(FILE *stream, const char *format, ...)
{
  va_list ap;
  int ret;
  
  gettimeofday(&tnow, NULL);
  fprintf(stream, "%ld.%03ld ", tnow.tv_sec, tnow.tv_usec/1000);

  va_start (ap, format);
  ret = vfprintf(stderr, format, ap);
  va_end (ap);
  
  return ret;
}
