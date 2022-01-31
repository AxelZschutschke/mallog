

all : libmallog.so

libmallog.so : mallog.o
	gcc --shared -fPIC -o libmallog.so mallog.o

mallog.o : mallog.c
	gcc -c -fPIC -o mallog.o mallog.c
