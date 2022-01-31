

all : mallog.so

mallog.so : mallog.o
	gcc --shared -fPIC -o mallog.so mallog.o

mallog.o : mallog.c
	gcc -c -fPIC -o mallog.o mallog.c
