#define _GNU_SOURCE

//#define WITH_FILTER
#define WITH_ALIGN 128
#define WITH_LOG_MALLOC
#define WITH_LOG_SPAWN
//#define WITH_LOG_MALLOC_STACK


#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <dlfcn.h>
#include <time.h>
#include <pthread.h>
#include <execinfo.h>
#include <string.h>

//#include "mallog.h"

static void* (*real_malloc)(size_t)=NULL;
static void* (*real_realloc)(void*, size_t)=NULL;
static void* (*real_calloc)(size_t, size_t)=NULL;
static void (*real_free)(void*)=NULL;
static int (*real_pthread)(pthread_t *restrict thread, const pthread_attr_t *restrict attr, void *(*start_routine)(void *), void *restrict arg) = NULL;

char buffer[8192];

pthread_t recursive = 0;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

FILE * outfile = NULL;
char const * const outfileName = "./mallog.log";

#ifdef WITH_FILTER
pid_t filter[100];
size_t n_filter = 0;

void add_filter() 
{
    if( n_filter >= sizeof(filter) )
        return;

    pid_t tid = pthread_self();

    pthread_mutex_lock(&log_mutex);
    fwrite("#T", 1, 2, outfile);
    fwrite(&tid, sizeof(pid_t), 1, outfile);
    fwrite("\n", 1, 1, outfile);
    log_backtrace();
    pthread_mutex_unlock(&log_mutex);

    int found = 0;
    for( size_t i = 0; i < n_filter; ++i )
      if( filter[i] == tid )
        return;

    filter[n_filter] = tid;
    n_filter++;
}
#else
void add_filter() 
{
}
#endif

void log_backtrace()
{
    void* callstack[20];
    size_t length = sizeof(pthread_t) + sizeof(time_t) + sizeof(size_t) + 1;
    pthread_t tid = pthread_self();
    time_t t = time(NULL);
    size_t frames = backtrace(callstack, 20);
    fwrite("#S", 1, 2, outfile);
    fwrite(&length, sizeof(size_t), 1, outfile);
    fwrite(&tid,sizeof(pthread_t),1,outfile);
    fwrite(&t,sizeof(time_t),1,outfile);
    fwrite(&frames, sizeof(size_t), 1, outfile );
    fwrite("\n", 1, 1, outfile );

    char ** strs = backtrace_symbols(callstack, frames);
    for (int i = 2; i < frames; ++i) { // start at 2 (mallog itself is not needed)
        size_t length = strnlen(strs[i], 100) + 1;
        fwrite("#s", 1, 2, outfile);
        fwrite(&length, sizeof(size_t), 1, outfile );
        fwrite(strs[i], 1, length-1, outfile);
        fwrite("\n", 1, 1, outfile );
    }
    real_free(strs);
}

static void open_mallog(void) 
{
    pthread_t tid = pthread_self();
    outfile = fopen(outfileName, "wb" );
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
    if (NULL == real_pthread)
        real_pthread = dlsym(RTLD_NEXT, "pthread_create");
    if (NULL == real_pthread)
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
    pthread_mutex_unlock(&init_mutex);
    fprintf(stderr, "Initialized\n" );
}



void log_info(char const * const info, size_t len )
{
    pthread_mutex_lock(&log_mutex);
    len += 1;
    fwrite("#I", 1, 2, outfile);
    fwrite(&len, sizeof(size_t), 1, outfile);
    fwrite(info,1,len-1,outfile);
    fwrite("\n", 1, 1, outfile);
    pthread_mutex_unlock(&log_mutex);
}

void mallog(char const * const op, void * ptr, size_t size, int do_log_backtrace  )
{
    //fprintf(stderr, "mallog\n");
    pthread_t tid = pthread_self();
    //fprintf(stderr,"%ld\n", tid);

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

#   ifdef WITH_LOG_MALLOC
    time_t t = time(NULL);
    size_t length = sizeof(pthread_t) + sizeof(time_t) + sizeof(void*) + sizeof(size_t) + 1;
    fwrite(op, 1, 2, outfile);
    fwrite(&length, sizeof(size_t), 1, outfile);
    fwrite(&tid,sizeof(pthread_t),1,outfile);
    fwrite(&t,sizeof(time_t),1,outfile);
    fwrite(&ptr,sizeof(void*),1,outfile);
    fwrite(&size,sizeof(size_t),1,outfile);
    fwrite("\n", 1, 1, outfile);
#   endif

#   ifdef WITH_LOG_MALLOC_STACK
    if( do_log_backtrace )
        log_backtrace();
#   endif

    recursive = 0;
    pthread_mutex_unlock(&log_mutex);
}

void free(void * ptr)
{
    if( ptr == buffer )
        return;

    if(real_free==NULL) {
        mtrace_init();
    }

    real_free(ptr);
    mallog( "#F", ptr, 0, 0);
}
void *malloc(size_t size)
{
    if(real_malloc==NULL) {
        mtrace_init();
    }

#   ifdef  WITH_ALIGN
    size_t aligned = (size / WITH_ALIGN + 1) * WITH_ALIGN;
#   else
    size_t aligned = size;
#   endif
    void *p = real_malloc(aligned);
    mallog( "#A", p, size, 1);
    return p;
}
void *realloc(void * ptr, size_t size)
{
    if(real_realloc==NULL) {
        mtrace_init();
    }

#   ifdef  WITH_ALIGN
    size_t aligned = (size / WITH_ALIGN + 1) * WITH_ALIGN;
#   else
    size_t aligned = size;
#   endif
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

int pthread_create(pthread_t *restrict thread,
                   const pthread_attr_t *restrict attr,
                   void *(*start_routine)(void *),
                   void *restrict arg)
{
    pthread_t tid = pthread_self();
    if(real_pthread==NULL) {
        mtrace_init();
    }

    int ret = real_pthread(thread, attr, start_routine, arg);

#   ifdef WITH_LOG_SPAWN
    pthread_mutex_lock(&log_mutex);
    recursive = tid;
    time_t t = time(NULL);
    size_t length = sizeof(pthread_t) + sizeof(time_t) + sizeof(pthread_t) + 1;

    if( outfile == NULL ) {
        open_mallog();
    }
    fwrite("#T", 1, 2, outfile);
    fwrite(&length, sizeof(size_t), 1, outfile);
    fwrite(&tid,sizeof(pthread_t),1,outfile);
    fwrite(&t,sizeof(time_t),1,outfile);
    fwrite(thread, sizeof(pthread_t), 1, outfile);
    fwrite("\n", 1, 1, outfile);
    log_backtrace();

    pthread_mutex_unlock(&log_mutex);
#   endif

    return ret;
}

