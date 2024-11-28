#ifndef WGETX_H_
#define WGETX_H_

#include <pthread.h>
#include "url.h"

/* Structure for HTTP reply */
typedef struct http_reply {
    char *reply_buffer;
    int reply_buffer_length;
} http_reply;

/* Structure for queue items */
typedef struct queue_item {
    char *url;
    char *parent_url;  // For relative URL resolution
    int depth;
} queue_item_t;

/* Structure for synchronized queue */
typedef struct url_queue {
    queue_item_t *items;    // Array of queue items
    int front;              // Front of queue
    int rear;               // Rear of queue
    int size;               // Current size
    int capacity;           // Maximum capacity
    int active_threads;     // Number of active threads
    int should_shutdown;    // Shutdown flag
    pthread_mutex_t mutex;  // Mutex for thread safety
    pthread_cond_t not_empty;  // Condition for queue not empty
    pthread_cond_t not_full;   // Condition for queue not full
} url_queue_t;

/* Function declarations for HTTP operations */
char* http_get_request(url_info *info);
int find_headers_end(const char *buffer, int length);
char *read_http_reply(struct http_reply *reply);
int download_page(url_info *info, http_reply *reply, int redirect_count);
void write_data(const char *path, const char *data, int len, int is_html);

/* Function declarations for URL handling */
void free_url_info(url_info *info);
char *next_line(char *buff, int len);
void create_directories(const char *path);
int is_visited(const char *url);

/* Function declarations for queue operations */
void init_url_queue(void);
void cleanup_url_queue(void);
int enqueue_url(const char *url, const char *parent_url, int depth);
int dequeue_url(queue_item_t *item);

/* Function declarations for HTML processing */
char* rewrite_html_urls(const char *content, size_t content_len, const char *base_url);
void extract_urls(const char *html, size_t html_len, const char *base_url, int depth);

/* Worker thread function */
void *worker_thread(void *arg);

#endif /* WGETX_H_ */