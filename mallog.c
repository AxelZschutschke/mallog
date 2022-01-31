#define _GNU_SOURCE

#define WITH_STACK
#define WITH_FILTER

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <dlfcn.h>
#include <time.h>
#include <pthread.h>
#include <execinfo.h>
#include <string.h>

#include "mallog.h"

static void* (*real_malloc)(size_t)=NULL;
static void* (*real_realloc)(void*, size_t)=NULL;
static void* (*real_calloc)(size_t, size_t)=NULL;
static void (*real_free)(void*)=NULL;
FILE * outfile = NULL;

char buffer[8192];

pid_t recursive = 0;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

pid_t filter[100];
size_t n_filter = 0;

void add_filter() {
    if( n_filter >= sizeof(filter) )
        return;

    pid_t tid = gettid();
    int found = 0;
    for( size_t i = 0; i < n_filter; ++i )
      if( filter[i] == tid )
        return;

    filter[n_filter] = tid;
    n_filter++;
}

static void open_mallog(void) {
    outfile = fopen("log", "wb" );
    if (NULL == outfile) {
        fprintf(stderr, "Error opening log file\n");
    }
}

static void mtrace_init(void)
{
    pthread_mutex_lock(&init_mutex);
    fprintf(stderr, "Initializing mallog\n" );

    if (NULL == real_free) 
        real_free = dlsym(RTLD_NEXT, "free");
    if (NULL == real_free) 
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());

    if (NULL == real_calloc)
        real_calloc= dlsym(RTLD_NEXT, "calloc");
    if (NULL == real_calloc)
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
    if (NULL == real_realloc)
        real_realloc= dlsym(RTLD_NEXT, "realloc");
    if (NULL == real_realloc)
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
    if (NULL == real_malloc)
        real_malloc = dlsym(RTLD_NEXT, "malloc");
    if (NULL == real_malloc)
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
    fprintf(stderr, "Initialized\n" );
    pthread_mutex_unlock(&init_mutex);
}


void log_backtrace() {
    void* callstack[13];
    int frames = backtrace(callstack, 13);

    fwrite("#S", 1, 2, outfile);
    fwrite(&frames, sizeof(int), 1, outfile );
    fwrite("\n", 1, 1, outfile );

    fflush(outfile);
    int fd = fileno(outfile);
    backtrace_symbols_fd(callstack, frames, fd);
    //for (int i = 3; i < frames; ++i) { // start at 2 (mallog itself is not needed)
    //    size_t length = strnlen(strs[i], 50);
    //    fwrite("#s", 1, 2, outfile);
    //    fwrite(&length, 1, 2, outfile );
    //    fwrite(strs[i], 1, length, outfile);
    //    fwrite("\n", 1, 1, outfile );
    //}
    //real_free(strs);
}

void mallog(char const * const op, void * ptr, size_t size, int do_log_backtrace  )
{
    pid_t tid = gettid();

#   ifdef WITH_FILTER
    int found = 0;
    for( size_t i = 0; i < n_filter; ++i )
      if( filter[i] == tid )
          ++found;
    if( found == 0 )
        return;
#   endif

    if( recursive == tid ) return;
    pthread_mutex_lock(&log_mutex);
    recursive = tid;
    if( outfile == NULL ) {
        open_mallog();
    }

    time_t t = time(NULL);
    size_t length = sizeof(pid_t) + sizeof(time_t) + sizeof(void*) + sizeof(size_t) + 1;
    fwrite(op, 1, 2, outfile);
    fwrite(&length, sizeof(size_t), 1, outfile);
    fwrite(&tid,sizeof(pid_t),1,outfile);
    fwrite(&t,sizeof(time_t),1,outfile);
    fwrite(&ptr,sizeof(void*),1,outfile);
    fwrite(&size,sizeof(size_t),1,outfile);
    fwrite("\n", 1, 1, outfile);

#   ifdef WITH_STACK
    if( do_log_backtrace )
        log_backtrace();
#   endif

    recursive = 0;
    pthread_mutex_unlock(&log_mutex);
}

void free(void * ptr)
{
    if(real_free==NULL) {
        mtrace_init();
    }

    if( ptr == buffer )
        return;

    real_free(ptr);
    mallog( "#F", ptr, 0, 0);
}
void *malloc(size_t size)
{
    if(real_malloc==NULL) {
        mtrace_init();
    }

    size_t aligned = size; //(size / 128 + 1) * 128;
    void *p = real_malloc(aligned);
    mallog( "#A", p, size, 1);
    return p;
}
void *realloc(void * ptr, size_t size)
{
    if(real_realloc==NULL) {
        mtrace_init();
    }

    size_t aligned = size; //(size / 128 + 1) * 128;
    void *p = real_realloc(ptr, aligned);
    mallog( "#F", ptr, 0, 0);
    mallog( "#A", p, size, 1);
    return p;
}
void *calloc(size_t count, size_t size)
{
    if(real_calloc==NULL)
        return buffer;

    void *p = real_calloc(count,size);
    mallog( "#A", p, count * size, 1);
    return p;
}
