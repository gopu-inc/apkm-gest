#include "apkm.h"
#include <pthread.h>
#include <semaphore.h>
#include <tbb/tbb.h>

// Pool de threads optimisé
typedef struct {
    pthread_t* threads;
    sem_t* work_sem;
    sem_t* done_sem;
    pthread_mutex_t queue_mutex;
    work_item_t* work_queue;
    int queue_size;
    int queue_capacity;
    bool running;
} thread_pool_t;

static thread_pool_t pool;

// Initialisation du pool avec TBB
int init_thread_pool(int thread_count) {
    pool.threads = malloc(thread_count * sizeof(pthread_t));
    pool.work_sem = malloc(sizeof(sem_t));
    pool.done_sem = malloc(sizeof(sem_t));
    sem_init(pool.work_sem, 0, 0);
    sem_init(pool.done_sem, 0, 0);
    pthread_mutex_init(&pool.queue_mutex, NULL);
    
    pool.work_queue = malloc(1024 * sizeof(work_item_t));
    pool.queue_capacity = 1024;
    pool.queue_size = 0;
    pool.running = true;
    
    for (int i = 0; i < thread_count; i++) {
        pthread_create(&pool.threads[i], NULL, thread_worker, NULL);
    }
    
    return 0;
}

// Worker thread avec TBB
void* thread_worker(void* arg) {
    tbb::task_scheduler_init init(tbb::task_scheduler_init::automatic);
    
    while (pool.running) {
        sem_wait(pool.work_sem);
        
        pthread_mutex_lock(&pool.queue_mutex);
        if (pool.queue_size == 0) {
            pthread_mutex_unlock(&pool.queue_mutex);
            continue;
        }
        
        work_item_t item = pool.work_queue[--pool.queue_size];
        pthread_mutex_unlock(&pool.queue_mutex);
        
        // Exécution avec TBB pour le parallélisme imbriqué
        tbb::parallel_for(tbb::blocked_range<int>(0, item.subtasks),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i < r.end(); i++) {
                    item.function(item.data, i);
                }
            });
        
        sem_post(pool.done_sem);
    }
    
    return NULL;
}

// Téléchargement parallèle avec curl multi
int parallel_download(char** urls, int count, const char* dest_dir) {
    CURLM* multi_handle = curl_multi_init();
    CURL** easy_handles = malloc(count * sizeof(CURL*));
    
    int still_running = 0;
    
    // Ajout des transferts
    for (int i = 0; i < count; i++) {
        easy_handles[i] = curl_easy_init();
        
        char outfile[512];
        snprintf(outfile, sizeof(outfile), "%s/file_%d", dest_dir, i);
        FILE* fp = fopen(outfile, "wb");
        
        curl_easy_setopt(easy_handles[i], CURLOPT_URL, urls[i]);
        curl_easy_setopt(easy_handles[i], CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(easy_handles[i], CURLOPT_TIMEOUT, 30L);
        
        curl_multi_add_handle(multi_handle, easy_handles[i]);
    }
    
    // Boucle de transfert
    curl_multi_perform(multi_handle, &still_running);
    
    while (still_running) {
        struct timeval timeout;
        int rc;
        fd_set fdread, fdwrite, fdexcep;
        int maxfd = -1;
        
        FD_ZERO(&fdread);
        FD_ZERO(&fdwrite);
        FD_ZERO(&fdexcep);
        
        curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
        
        curl_multi_perform(multi_handle, &still_running);
    }
    
    // Nettoyage
    curl_multi_cleanup(multi_handle);
    for (int i = 0; i < count; i++) {
        curl_easy_cleanup(easy_handles[i]);
    }
    free(easy_handles);
    
    return 0;
}
