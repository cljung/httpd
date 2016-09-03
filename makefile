DEFS = -D__LINUX__ -DLINUX -DNOWIN32
INC = -I.
CFLAGS = -W

all:	app

httpd.o: httpd.cpp httpdef.h socketdef.h ver.h
	cc $(DEFS) $(INC) $(CFLAGS) httpd.cpp -o httpd
	chmod +x httpd

app: httpd.o


