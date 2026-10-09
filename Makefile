TARGET = psp_clarity
OBJS   = main.o exports.o systemctrl_stub.o

PSP_FW_VERSION = 660
BUILD_PRX      = 1
PRX_EXPORTS    = exports.exp

USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

CFLAGS   = -O2 -G0 -Wall -fno-pic
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS  = $(CFLAGS)

INCDIR =
LIBDIR =

LDFLAGS = -nostartfiles
LIBS    = -lpspdebug -lpspdisplay -lpspctrl

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
