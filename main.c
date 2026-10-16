#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define QUEUE_CAPACITY 16
#define REPORT_COUNT 30

typedef struct {
    unsigned id;
    struct timespec created_at, started_at, finished_at;
} Report;

typedef struct {
    Report items[QUEUE_CAPACITY];
    size_t head, tail, count, max_count;
    bool producer_done;
    pthread_mutex_t mutex;
    pthread_cond_t available;
    unsigned interval_ms, processing_ms, threshold_ms;
    unsigned created, processed, dropped, slow;
    double total_sum, total_max, wait_sum, wait_max;
} Simulation;

static void check_pthread(int error, const char *operation)
{
    if (error != 0) {
        fprintf(stderr, "%s: %s\n", operation, strerror(error));
        exit(EXIT_FAILURE);
    }
}

static struct timespec now(void)
{
    struct timespec result;
    if (clock_gettime(CLOCK_MONOTONIC, &result) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return result;
}

static void sleep_ms(unsigned milliseconds)
{
    struct timespec remaining = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000L
    };
    /* Tiếp tục ngủ phần thời gian còn lại nếu bị tín hiệu ngắt. */
    while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) {
            perror("nanosleep");
            exit(EXIT_FAILURE);
        }
    }
}

static double elapsed_ms(struct timespec end, struct timespec start)
{
    return (double)(end.tv_sec - start.tv_sec) * 1000.0
         + (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
}

static void *produce(void *argument)
{
    Simulation *s = argument;
    for (unsigned id = 1; id <= REPORT_COUNT; ++id) {
        /* Report đầu tiên được tạo ngay; các report sau cách nhau một nhịp. */
        if (id > 1)
            sleep_ms(s->interval_ms);
        Report report = {.id = id, .created_at = now()};
        bool dropped = false;
        check_pthread(pthread_mutex_lock(&s->mutex), "mutex_lock");
        ++s->created;
        if (s->count == QUEUE_CAPACITY) {
            ++s->dropped;
            dropped = true;
        } else {
            s->items[s->tail] = report;
            s->tail = (s->tail + 1) % QUEUE_CAPACITY;
            ++s->count;
            if (s->count > s->max_count)
                s->max_count = s->count;
            check_pthread(pthread_cond_signal(&s->available), "cond_signal");
        }
        check_pthread(pthread_mutex_unlock(&s->mutex), "mutex_unlock");
        if (dropped)
            printf("Report #%u | DROPPED (queue đầy)\n", id);
    }
    /* Đánh thức consumer kể cả khi queue rỗng, tránh chờ vô hạn. */
    check_pthread(pthread_mutex_lock(&s->mutex), "mutex_lock");
    s->producer_done = true;
    check_pthread(pthread_cond_signal(&s->available), "cond_signal");
    check_pthread(pthread_mutex_unlock(&s->mutex), "mutex_unlock");
    return NULL;
}

static void *consume(void *argument)
{
    Simulation *s = argument;
    for (;;) {
        check_pthread(pthread_mutex_lock(&s->mutex), "mutex_lock");
        /* Vòng while xử lý cả trường hợp condition variable thức giả. */
        while (s->count == 0 && !s->producer_done)
            check_pthread(pthread_cond_wait(&s->available, &s->mutex), "cond_wait");
        if (s->count == 0 && s->producer_done) {
            check_pthread(pthread_mutex_unlock(&s->mutex), "mutex_unlock");
            break;
        }
        Report report = s->items[s->head];
        report.started_at = now();
        s->head = (s->head + 1) % QUEUE_CAPACITY;
        --s->count;
        check_pthread(pthread_mutex_unlock(&s->mutex), "mutex_unlock");

        /* Không giữ mutex lúc giả lập xử lý/gửi; không có bản tin mạng. */
        sleep_ms(s->processing_ms);
        report.finished_at = now();
        double wait = elapsed_ms(report.started_at, report.created_at);
        double processing = elapsed_ms(report.finished_at, report.started_at);
        double total = elapsed_ms(report.finished_at, report.created_at);
        bool slow = total > s->threshold_ms;
        /* Chỉ consumer cập nhật thống kê xử lý; main đọc sau pthread_join. */
        ++s->processed;
        s->slow += slow;
        s->total_sum += total;
        s->wait_sum += wait;
        if (total > s->total_max)
            s->total_max = total;
        if (wait > s->wait_max)
            s->wait_max = wait;
        printf("Report #%u | queue=%.3f ms | processing=%.3f ms | total=%.3f ms | %s\n",
               report.id, wait, processing, total, slow ? "SLOW" : "OK");
    }
    return NULL;
}

int main(int argc, char **argv)
{
    /* Đẩy từng dòng ngay cả khi giao diện đọc stdout qua pipe. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc != 2 || (strcmp(argv[1], "normal") != 0 &&
                      strcmp(argv[1], "overload") != 0)) {
        fprintf(stderr, "Cách dùng: %s normal|overload\n", argv[0]);
        return EXIT_FAILURE;
    }
    bool overload = strcmp(argv[1], "overload") == 0;
    Simulation s = {
        .interval_ms = overload ? 50 : 200,
        .processing_ms = overload ? 200 : 50,
        .threshold_ms = 300
    };
    check_pthread(pthread_mutex_init(&s.mutex, NULL), "mutex_init");
    check_pthread(pthread_cond_init(&s.available, NULL), "cond_init");
    printf("Kịch bản %s | interval=%u ms | processing=%u ms | threshold=%u ms\n",
           argv[1], s.interval_ms, s.processing_ms, s.threshold_ms);
    pthread_t producer, consumer;
    check_pthread(pthread_create(&consumer, NULL, consume, &s), "create consumer");
    int error = pthread_create(&producer, NULL, produce, &s);
    if (error != 0) {
        /* Nếu không tạo được producer, vẫn cho consumer thoát sạch sẽ. */
        check_pthread(pthread_mutex_lock(&s.mutex), "mutex_lock");
        s.producer_done = true;
        check_pthread(pthread_cond_signal(&s.available), "cond_signal");
        check_pthread(pthread_mutex_unlock(&s.mutex), "mutex_unlock");
        check_pthread(pthread_join(consumer, NULL), "join consumer");
    } else {
        check_pthread(pthread_join(producer, NULL), "join producer");
        check_pthread(pthread_join(consumer, NULL), "join consumer");
    }
    check_pthread(pthread_cond_destroy(&s.available), "cond_destroy");
    check_pthread(pthread_mutex_destroy(&s.mutex), "mutex_destroy");
    check_pthread(error, "create producer");

    printf("\nTổng kết\ncreated=%u | processed=%u | dropped=%u | SLOW=%u\n",
           s.created, s.processed, s.dropped, s.slow);
    printf("total: avg=%.3f ms | max=%.3f ms\n",
           s.processed ? s.total_sum / s.processed : 0.0, s.total_max);
    printf("queue_wait: avg=%.3f ms | max=%.3f ms\n",
           s.processed ? s.wait_sum / s.processed : 0.0, s.wait_max);
    printf("queue_max=%zu (sức chứa=%d)\n", s.max_count, QUEUE_CAPACITY);
    bool valid = s.created == s.processed + s.dropped;
    printf("created = processed + dropped: %s\n", valid ? "PASS" : "FAIL");
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
