DEFS = -D__LINUX__ -DLINUX -DNOWIN32
INC = -I.
CFLAGS = -W

all:	app

httpd.o: httpd.cpp httpdef.h socketdef.h ver.h
	gcc $(DEFS) $(INC) $(CFLAGS) -c httpd.cpp -o httpd

app: httpd.o


