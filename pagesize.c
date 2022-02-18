#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{
 int pageSize = getpagesize();
 
 printf("Page size on your system = %i bytes\n", pageSize);
 
 void * test1;
 void * test2;
 void * test3;

 test1 = malloc(pageSize*2/3);
 test2 = malloc(pageSize*2/3);
 test3 = malloc(pageSize*2/3);
 printf("%p %p %p\n", test1, test2, test3);
 printf("%ld %ld\n", test2 - test1, test3 - test2);
 free(test1);
 free(test2);
 free(test3);
 test1 = malloc(pageSize*1/3);
 test2 = malloc(pageSize*1/3);
 test3 = malloc(pageSize*1/3);
 printf("%p %p %p\n", test1, test2, test3);
 free(test1);
 free(test2);
 free(test3);
 return 0;
}
