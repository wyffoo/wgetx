#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <strings.h>
#include <pthread.h>
#include <sys/stat.h>


#include "url.h"
#include "wgetX.h"

#define BUFFER_SIZE 8888888
#define MAX_DEPTH 3
#define THREAD_POOL_SIZE 4
#define MAX_QUEUE_SIZE 1000

// URL queue structure


// Global variables
url_queue_t url_queue;
pthread_mutex_t visited_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;
char **visited_urls = NULL;
int visited_count = 0;

// Function to free URL info structure
void free_url_info(url_info *info) {
    if (info) {
        free(info->protocol);
        free(info->host);
        free(info->path);
    }
}

// Find next line in buffer
char *next_line(char *buff, int len) {
    if (len == 0) {
        return NULL;
    }

    char *last = buff + len - 1;
    while (buff != last) {
        if (*buff == '\r' && *(buff + 1) == '\n') {
            return buff;
        }
        buff++;
    }
    return NULL;
}

// Function to rewrite URLs in HTML content
char* rewrite_html_urls(const char *content, size_t content_len, const char *base_url) {
    char *result = malloc(content_len * 2);  // Allocate more space for possible expansion
    char *write_ptr = result;
    const char *read_ptr = content;
    const char *end_ptr = content + content_len;
    
    while (read_ptr < end_ptr) {
        const char *href = strstr(read_ptr, "href=\"");
        if (!href || href >= end_ptr) {
            // Copy remaining content
            size_t remaining = end_ptr - read_ptr;
            memcpy(write_ptr, read_ptr, remaining);
            write_ptr += remaining;
            break;
        }
        
        // Copy content up to href
        size_t prefix_len = href - read_ptr;
        memcpy(write_ptr, read_ptr, prefix_len);
        write_ptr += prefix_len;
        
        // Copy href="
        memcpy(write_ptr, "href=\"", 6);
        write_ptr += 6;
        
        // Find URL end
        const char *url_start = href + 6;
        const char *url_end = strchr(url_start, '"');
        if (!url_end || url_end >= end_ptr) break;
        
        // Extract and rewrite URL
        size_t url_len = url_end - url_start;
        char url[1024];
        strncpy(url, url_start, url_len);
        url[url_len] = '\0';
        
        // Create local path
        char local_path[2048];
        snprintf(local_path, sizeof(local_path), "downloads/%s", url);
        
        // Write local path
        size_t local_len = strlen(local_path);
        memcpy(write_ptr, local_path, local_len);
        write_ptr += local_len;
        
        read_ptr = url_end;
    }
    
    *write_ptr = '\0';
    return result;
}

// Create directories recursively
void create_directories(const char *path) {
    char *path_copy = strdup(path);
    char *p = path_copy;
    
    while (*p != '\0') {
        if (*p == '/') {
            *p = '\0';
            mkdir(path_copy, 0755);
            *p = '/';
        }
        p++;
    }
    mkdir(path_copy, 0755);
    free(path_copy);
}

// Write data to file
void write_data(const char *path, const char *data, int len, int is_html) {
    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "downloads/%s", path);
    
    // Create necessary directories
    char *last_slash = strrchr(full_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        create_directories(full_path);
        *last_slash = '/';
    }
    
    FILE *file = fopen(full_path, "wb");
    if (!file) {
        fprintf(stderr, "Could not open file %s for writing: %s\n", full_path, strerror(errno));
        return;
    }
    
    if (is_html) {
        // Rewrite URLs in HTML content before saving
        char *rewritten = rewrite_html_urls(data, len, path);
        fwrite(rewritten, 1, strlen(rewritten), file);
        free(rewritten);
    } else {
        fwrite(data, 1, len, file);
    }
    
    fclose(file);
    fprintf(stderr, "Saved: %s\n", full_path);
}

// 改进的线程池和队列操作
void init_url_queue(void) {
    url_queue.capacity = MAX_QUEUE_SIZE;
    url_queue.items = calloc(MAX_QUEUE_SIZE, sizeof(queue_item_t));
    url_queue.size = 0;
    url_queue.front = 0;
    url_queue.rear = -1;
    url_queue.active_threads = 0;
    url_queue.should_shutdown = 0;
    pthread_mutex_init(&url_queue.mutex, NULL);
    pthread_cond_init(&url_queue.not_empty, NULL);
    pthread_cond_init(&url_queue.not_full, NULL);
}

void cleanup_url_queue(void) {
    for (int i = 0; i < url_queue.size; i++) {
        int idx = (url_queue.front + i) % url_queue.capacity;
        free(url_queue.items[idx].url);
        free(url_queue.items[idx].parent_url);
    }
    free(url_queue.items);
    pthread_mutex_destroy(&url_queue.mutex);
    pthread_cond_destroy(&url_queue.not_empty);
    pthread_cond_destroy(&url_queue.not_full);
}

int enqueue_url(const char *url, const char *parent_url, int depth) {
    pthread_mutex_lock(&url_queue.mutex);
    
    while (url_queue.size >= url_queue.capacity && !url_queue.should_shutdown) {
        pthread_cond_wait(&url_queue.not_full, &url_queue.mutex);
    }
    
    if (url_queue.should_shutdown) {
        pthread_mutex_unlock(&url_queue.mutex);
        return -1;
    }
    
    url_queue.rear = (url_queue.rear + 1) % url_queue.capacity;
    url_queue.items[url_queue.rear].url = strdup(url);
    url_queue.items[url_queue.rear].parent_url = parent_url ? strdup(parent_url) : NULL;
    url_queue.items[url_queue.rear].depth = depth;
    url_queue.size++;
    
    pthread_cond_signal(&url_queue.not_empty);
    pthread_mutex_unlock(&url_queue.mutex);
    return 0;
}

int dequeue_url(queue_item_t *item) {
    pthread_mutex_lock(&url_queue.mutex);
    
    while (url_queue.size == 0 && !url_queue.should_shutdown) {
        // Wait for new items or shutdown signal
        pthread_cond_wait(&url_queue.not_empty, &url_queue.mutex);
    }
    
    if (url_queue.size == 0 && url_queue.should_shutdown) {
        pthread_mutex_unlock(&url_queue.mutex);
        return -1;
    }
    
    // Copy item data
    *item = url_queue.items[url_queue.front];
    url_queue.front = (url_queue.front + 1) % url_queue.capacity;
    url_queue.size--;
    
    pthread_cond_signal(&url_queue.not_full);
    pthread_mutex_unlock(&url_queue.mutex);
    return 0;
}
// Check if URL has been visited
int is_visited(const char *url) {
    pthread_mutex_lock(&visited_mutex);
    for (int i = 0; i < visited_count; i++) {
        if (strcmp(visited_urls[i], url) == 0) {
            pthread_mutex_unlock(&visited_mutex);
            return 1;
        }
    }
    
    visited_urls = realloc(visited_urls, (visited_count + 1) * sizeof(char*));
    visited_urls[visited_count++] = strdup(url);
    pthread_mutex_unlock(&visited_mutex);
    return 0;
}
void extract_urls(const char *html, size_t html_len, const char *base_url, int depth) {
    const char *ptr = html;
    const char *end = html + html_len;
    
    while ((ptr = strstr(ptr, "href=\"")) && ptr < end) {
        ptr += 6;
        const char *quote_end = strchr(ptr, '"');
        if (!quote_end || quote_end >= end) break;
        
        size_t url_len = quote_end - ptr;
        char *url = malloc(url_len + 1);
        strncpy(url, ptr, url_len);
        url[url_len] = '\0';
        
        // Handle relative URLs
        if (url[0] == '/') {
            url_info base_info;
            if (parse_url((char*)base_url, &base_info) == 0) {
                char *absolute_url = malloc(strlen(base_info.host) + strlen(url) + 10);
                sprintf(absolute_url, "%s://%s%s", base_info.protocol, base_info.host, url);
                free(url);
                url = absolute_url;
                free_url_info(&base_info);
            }
        }
        
        // Process all URLs that haven't been visited yet
        if (!is_visited(url) && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0)) {
            fprintf(stderr, "Found new URL: %s (depth: %d)\n", url, depth + 1);
            enqueue_url(url, base_url, depth + 1);
        } else {
            fprintf(stderr, "URL already visited: %s\n", url);
        }
        
        free(url);
        ptr = quote_end + 1;
    }
}
// 修改 worker_thread 函数来改进线程池行为
void *worker_thread(void *arg) {
    pthread_mutex_lock(&url_queue.mutex);
    url_queue.active_threads++;
    pthread_mutex_unlock(&url_queue.mutex);
    
    while (1) {
        queue_item_t item;
        if (dequeue_url(&item) != 0) {
            break;
        }
        
        if (item.depth > MAX_DEPTH) {
            fprintf(stderr, "Thread %p: Skipping URL due to depth > %d: %s\n", 
                    (void*)pthread_self(), MAX_DEPTH, item.url);
            free(item.url);
            free(item.parent_url);
            continue;
        }
        
        fprintf(stderr, "Thread %p processing URL: %s (depth: %d)\n", 
                (void*)pthread_self(), item.url, item.depth);
        
        url_info info;
        if (parse_url(item.url, &info) == 0) {
            http_reply reply = {0};
            if (download_page(&info, &reply, 0) == 0) {
                char *response = read_http_reply(&reply);
                if (response) {
                    // Check content type
                    int is_html = 0;
                    char *content_type = strcasestr(reply.reply_buffer, "Content-Type:");
                    if (content_type) {
                        is_html = (strcasestr(content_type, "text/html") != NULL);
                    }
                    
                    // Create local path and save file
                    char *filename = malloc(strlen(info.host) + strlen(info.path) + 2);
                    if (strlen(info.path) == 0) {
                        sprintf(filename, "%s/index.html", info.host);
                    } else {
                        sprintf(filename, "%s/%s", info.host, info.path);
                    }
                    
                    size_t content_len = reply.reply_buffer_length - (response - reply.reply_buffer);
                    write_data(filename, response, content_len, is_html);
                    
                    // Extract and queue new URLs if HTML
                    if (is_html) {
                        fprintf(stderr, "Thread %p: Extracting URLs from %s\n", 
                                (void*)pthread_self(), item.url);
                        extract_urls(response, content_len, item.url, item.depth);
                    }
                    
                    free(filename);
                }
                free(reply.reply_buffer);
            } else {
                fprintf(stderr, "Thread %p: Failed to download %s\n", 
                        (void*)pthread_self(), item.url);
            }
            free_url_info(&info);
        }
        
        free(item.url);
        free(item.parent_url);
        
        // Update active tasks count
        pthread_mutex_lock(&url_queue.mutex);
        url_queue.active_threads--;
        if (url_queue.active_threads == 0 && url_queue.size == 0) {
            // Signal shutdown when all tasks are complete
            url_queue.should_shutdown = 1;
            pthread_cond_broadcast(&url_queue.not_empty);
        }
        pthread_mutex_unlock(&url_queue.mutex);
    }
    
    return NULL;
}

char* http_get_request(url_info *info) {
    char *request_buffer = malloc(512 + strlen(info->path) + strlen(info->host));
    if (request_buffer == NULL) {
        fprintf(stderr, "Memory allocation error\n");
        return NULL;
    }
    
    snprintf(request_buffer, 512 + strlen(info->path) + strlen(info->host),
             "GET /%s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) Firefox/123.0\r\n"
             "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n"
             "Accept-Language: en-US,en;q=0.5\r\n"
             "Accept-Encoding: identity\r\n"
             "Connection: close\r\n"
             "\r\n",
             info->path, info->host);

    fprintf(stderr, "Sending request to %s://%s:\n%s", 
            info->protocol, info->host, request_buffer);
    return request_buffer;
}
char *read_http_reply(struct http_reply *reply) {
    char *status_line = next_line(reply->reply_buffer, reply->reply_buffer_length);
    if (status_line == NULL) {
        fprintf(stderr, "Could not find status\n");
        return NULL;
    }

    char *headers_end = strstr(reply->reply_buffer, "\r\n\r\n");
    if (headers_end == NULL) {
        fprintf(stderr, "Could not find end of headers\n");
        return NULL;
    }

    // Debug: print response headers
    fprintf(stderr, "Response headers:\n%.*s\n", 
            (int)(headers_end - reply->reply_buffer), reply->reply_buffer);

    return headers_end + 4;
}
int download_page(url_info *info, http_reply *reply, int redirect_count) {
    struct addrinfo hints, *res;
    int sockfd;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", info->port);

    int status = getaddrinfo(info->host, port_str, &hints, &res);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return -1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        fprintf(stderr, "Could not create socket: %s\n", strerror(errno));
        freeaddrinfo(res);
        return -1;
    }

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        fprintf(stderr, "Could not connect to server: %s\n", strerror(errno));
        close(sockfd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    char *request = http_get_request(info);
    if (request == NULL) {
        close(sockfd);
        return -1;
    }

    if (write(sockfd, request, strlen(request)) < 0) {
        fprintf(stderr, "Could not send request: %s\n", strerror(errno));
        free(request);
        close(sockfd);
        return -1;
    }

    free(request);

    reply->reply_buffer = malloc(BUFFER_SIZE);
    if (reply->reply_buffer == NULL) {
        fprintf(stderr, "Memory allocation error\n");
        close(sockfd);
        return -1;
    }

    reply->reply_buffer_length = 0;
    int total_bytes = 0;
    int bytes_received;

    while ((bytes_received = recv(sockfd, reply->reply_buffer + total_bytes, 
                                BUFFER_SIZE - total_bytes, 0)) > 0) {
        total_bytes += bytes_received;
        if (total_bytes >= BUFFER_SIZE) {
            char *temp = realloc(reply->reply_buffer, total_bytes + BUFFER_SIZE);
            if (temp == NULL) {
                fprintf(stderr, "Memory allocation error\n");
                free(reply->reply_buffer);
                close(sockfd);
                return -1;
            }
            reply->reply_buffer = temp;
        }
    }

    reply->reply_buffer_length = total_bytes;
    close(sockfd);

    // Check status code
    int status_code;
    sscanf(reply->reply_buffer, "HTTP/1.1 %d", &status_code);
    fprintf(stderr, "Received status code: %d\n", status_code);

    if (status_code == 301 || status_code == 302) {
        if (redirect_count >= MAX_DEPTH) {
            fprintf(stderr, "Too many redirects\n");
            return -1;
        }

        char *location = strstr(reply->reply_buffer, "Location: ");
        if (location) {
            location += 10;
            char *end_line = strchr(location, '\r');
            if (end_line) {
                *end_line = '\0';
                fprintf(stderr, "Redirecting to: %s\n", location);
                if (update_url(info, location) == 0) {
                    free(reply->reply_buffer);
                    return download_page(info, reply, redirect_count + 1);
                }
            }
        }
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <URL>\n", argv[0]);
        return 1;
    }
    
    // Create downloads directory
    mkdir("downloads", 0755);
    
    // Initialize queue and thread pool
    init_url_queue();
    
    // Add initial URL
    fprintf(stderr, "Adding initial URL: %s\n", argv[1]);
    enqueue_url(argv[1], NULL, 0);
    
    // Create worker threads
    pthread_t threads[THREAD_POOL_SIZE];
    fprintf(stderr, "Starting %d worker threads\n", THREAD_POOL_SIZE);
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_join(threads[i], NULL);
    }
    
    fprintf(stderr, "All threads completed. Cleaning up...\n");
    
    // Cleanup
    cleanup_url_queue();
    
    return 0;
}