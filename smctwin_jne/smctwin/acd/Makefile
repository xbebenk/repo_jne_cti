#
# Makefile for ACD Module
#
# (c) 2005-2007 Aria Adriano
#

LibName    = libacd.a
HomeDir    = 
InstallDir = /home/smart/bin/

CC     = gcc
CFlags = -O2 -Wall -c -g -DLINUX
IncPaths  = -I../libSmartCENTER -I./include -I../../libSmartCENTER -I../include
LibPaths	= -L../libSmartCENTER
Libs	  = -lSmartCENTER -ldl -lpthread

Srcs		= acd.c
Objs 		= acd.o

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

