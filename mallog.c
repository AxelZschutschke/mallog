#define _GNU_SOURCE

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <dlfcn.h>
#include <time.h>
#include <pthread.h>

static void* (*real_malloc)(size_t)=NULL;
static void* (*real_realloc)(void*, size_t)=NULL;
static void* (*real_calloc)(size_t, size_t)=NULL;
static void (*real_free)(void*)=NULL;
FILE * outfile = NULL;

char buffer[8192];

pid_t recursive = 0;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

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


void mallog(char const * const op, void * ptr, size_t size )
{
    pid_t tid = gettid();
    if( recursive == tid ) return;
    pthread_mutex_lock(&log_mutex);
    recursive = tid;
    if( outfile == NULL ) {
        open_mallog();
    }

    time_t t = time(NULL);
    fwrite(&t,sizeof(time_t),1,outfile);
    fwrite(&ptr,sizeof(void*),1,outfile);
    fwrite(&size,sizeof(size_t),1,outfile);
    fwrite(op, 1, 1, outfile);
    fwrite("\n", 1, 1, outfile);

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
    mallog( "F", ptr, 0);
}
void *malloc(size_t size)
{
    if(real_malloc==NULL) {
        mtrace_init();
    }

    size_t aligned = size; //(size / 128 + 1) * 128;
    void *p = real_malloc(aligned);
    mallog( "A", p, size);
    return p;
}
void *realloc(void * ptr, size_t size)
{
    if(real_realloc==NULL) {
        mtrace_init();
    }

    size_t aligned = size; //(size / 128 + 1) * 128;
    void *p = real_realloc(ptr, aligned);
    mallog( "F", ptr, 0);
    mallog( "A", p, size);
    return p;
}
void *calloc(size_t count, size_t size)
{
    if(real_calloc==NULL)
        return buffer;

    void *p = real_calloc(count,size);
    mallog( "A", p, count * size);
    return p;
}
