#ifndef CONFIG_H
#define CONFIG_H

#ifdef WIN32
#include "os/win32.h"
#elif __CYGWIN__
#include "os/cygwin.h"
#else
#include "os/unix.h"
#endif // WIN32

#endif // CONFIG_H