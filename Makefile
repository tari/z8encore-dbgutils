#################################################################
# Makefile for Z8 Encore! command line debugger and flashutil
#
#################################################################

CC = gcc
CXX = g++
LD = gcc
AR = ar

CFLAGS = 
CPPFLAGS = 
CXXFLAGS = 
LDFLAGS = 

# warnings
CPPFLAGS += -Wall

# debug info
CPPFLAGS += -g

# optimization
#CPPFLAGS += -O2

# strip symbols
#LDFLAGS += -s

# profile
#CPPFLAGS += -pg
#LDFLAGS += -pg

# coverage
#CPPFLAGS += -fprofile-arcs
#CPPFLAGS += -ftest-coverage

#################################################################
# Object files to include in libraries

LIBOBJS = serialport.o ocd_serial.o ocd_parport.o ocd_tcpip.o \
	  sockstream.o ez8ocd.o crc.o hexfile.o \
	  ez8dbg.o ez8dbg_trce.o ez8dbg_flash.o ez8dbg_brk.o \
	  dump.o md5c.o xmalloc.o err_msg.o timer.o

OBJS = ez8mon.o cfg.o setup.o monitor.o trace.o disassembler.o \
	opcodes.o server.o

#################################################################

all: libocd.a ez8mon flashutil crcgen
.PHONY: all

depend:
	$(CC) $(CPPFLAGS) -M *.cpp *.c >depend

ifneq ($(MAKECMDGOALS),clean)
include	depend
endif

#################################################################

libport.a: getdelim.o getline.o
	$(AR) -r $@ $^

libocd.a: $(LIBOBJS) libport.a
	$(AR) -r $@ $^

libocd.so: $(LIBOBJS) libport.a
	$(LD) $(CFLAGS) -shared -o$@ $^ 

version.o: $(OBJS) $(LIBOBJS) flashutil.o crcgen.o

# zilog test facilities
ifeq "$(wildcard test.cpp)" "test.cpp"
OBJS += test.o psi.o 
ifeq "$(wildcard param.cpp)" "param.cpp" 
OBJS += param.o
test.o: CXXFLAGS += -DPARAM
endif
monitor.o: CXXFLAGS += -DTEST
setup.o: CXXFLAGS += -DTEST
endif


ifdef COMSPEC
  LIBS += -lreadline -lws2_32 -liberty
else
  OSTYPE:=$(shell uname)
  ifeq "$(findstring Sun,$(OSTYPE))" "Sun"
    LIBS += -lresolv -Xlinker -Bstatic -lreadline -Xlinker -Bdynamic -ltermcap -lsocket  
  else
    ifeq "$(findstring Linux,$(OSTYPE))" "Linux"
      LIBS += -lreadline -ltermcap 
    endif
  endif
endif
 
ez8mon-static: $(OBJS) version.o libocd.a libport.a
	$(CXX) $(LDFLAGS) -o$@ $^ $(LIBS)

ez8mon: $(OBJS) version.o libocd.a libport.a
	$(CXX) $(LDFLAGS) -o$@ $^ $(LIBS)

flashutil: flashutil.o version.o libocd.a libport.a 
	$(CXX) $(LDFLAGS) -o$@ $^ $(LIBS)

crcgen: crcgen.o version.o hexfile.o crc.o 
	$(LD) $(LDFLAGS) -o$@ $^ $(LIBS)

gencrctable: gencrctable.o
	$(LD) $(LDFLAGS) -o$@ $^

endurance: endurance.o version.o libocd.a libport.a 
	$(CXX) $(LDFLAGS) -o$@ $^ $(LIBS)

md5: md5c.o mddriver.o
	$(LD) $(LDFLAGS) -o$@ $^

#################################################################

#clean: clean-profile
#clean: clean-coverage
clean:
	$(RM) *.o *.a *.so depend core core.* a.out \
	    ez8mon flashutil crcgen gencrctable endurance md5 \
	    ez8mon.exe flashutil.exe crcgen.exe

clean-profile: 
	$(RM) gmon.out

clean-coverage:
	$(RM) *.gcov *.bb *.bbg *.da

#################################################################


