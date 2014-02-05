#
# Makefile for CTI Module
#
# (c) 2005-2007 Aria Adriano
#

LibName    = libcti.a
HomeDir    = 
InstallDir = /home/smart/bin/

CC     = gcc
CFlags = -O2 -Wall -c -g -DLINUX
IncPaths  = -I../libSmartCENTER -I./include -I../../libSmartCENTER -I../include
LibPaths	= -L../libSmartCENTER
Libs	  = -lSmartCENTER -ldl -lpthread

Srcs		= cti.c cti_asai.c
Objs 		= cti.o cti_asai.o

.c.o:
	$(CC) $(CFlags) $*.c $(IncPaths)

all: $(LibName)

$(LibName): $(Objs) 
	ar cr $(LibName) $(Objs)


clean:
	rm -f ~* *.o core core* $(LibName)	

install:
	strip $(ProgName)
	install $(ProgName) $(InstallDir)
	
distclean: clean	
	rm -f config.mak

# vi: set ts=2 sw=2:

