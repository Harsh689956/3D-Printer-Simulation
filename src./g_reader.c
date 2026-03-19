/*
 * g_reader.c 
 *
 * T1 - file_reader : reads instructions.gcode line by line into a buffer
 * T2 - msg_sender  : pulls from buffer, sends each command to motor_control
 *                    via MSG QUEUE. Sends "DONE" when file is finished.
 */

#include "common.h"
#include <errno.h>

static int shm_id, msg_id;
static PrinterSettings *shm;
static volatile int keep_running = 1;


#define BUF_LEN 128
static struct gcode_msg  buf[BUF_LEN];
static int buf_head  = 0;
static int buf_tail  = 0;
static int buf_count = 0;
static int file_done = 0;

static pthread_mutex_t buf_lock      = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  buf_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  buf_not_full  = PTHREAD_COND_INITIALIZER;

static void handle_sigusr1(int sig) {
    (void)sig;
    keep_running = 0;
    pthread_mutex_lock(&shm->shm_lock);
    shm->emergency_stop = 1;
    pthread_mutex_unlock(&shm->shm_lock);
    
    pthread_mutex_lock(&buf_lock);
    file_done = 1;
    pthread_cond_broadcast(&buf_not_empty);  
    pthread_cond_broadcast(&buf_not_full);   
    pthread_mutex_unlock(&buf_lock);
}

/* T1 - READ FILE */
static void *file_reader(void *arg) {
    (void)arg;
    FILE *fp = fopen(shm->gcode_file, "r");
    if (!fp) {
        perror("[g_reader] Cannot open instructions.gcode");
        pthread_mutex_lock(&buf_lock);
        file_done = 1;
        pthread_cond_broadcast(&buf_not_empty);
        pthread_mutex_unlock(&buf_lock);
        return NULL;
    }

    char line[64];
    while (keep_running && fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0' || line[0] == ';') continue;

        pthread_mutex_lock(&buf_lock);
        while (buf_count == BUF_LEN && keep_running)
            pthread_cond_wait(&buf_not_full, &buf_lock);

        if (!keep_running) { 
            pthread_mutex_unlock(&buf_lock); 
            break; 
        }

        buf[buf_head].msg_type = 1;
        strncpy(buf[buf_head].command, line, sizeof(buf[buf_head].command) - 1);
        buf[buf_head].command[sizeof(buf[buf_head].command)-1] = '\0';
        buf_head = (buf_head + 1) % BUF_LEN;
        buf_count++;
        pthread_cond_signal(&buf_not_empty);
        pthread_mutex_unlock(&buf_lock);
    }

    fclose(fp);

    pthread_mutex_lock(&buf_lock);
    file_done = 1;
    pthread_cond_broadcast(&buf_not_empty);
    pthread_mutex_unlock(&buf_lock);
    return NULL;
}

/* T2 - SEND MESSAGES TO MOTOR_CONTROL */
static void *msg_sender(void *arg) {
    (void)arg;

    while (1) {
        pthread_mutex_lock(&buf_lock);
        /* Wait for buffer to have data OR file to be done */
        while (buf_count == 0 && !file_done) {
            pthread_cond_wait(&buf_not_empty, &buf_lock);
            /* After waking, re-check keep_running */
            if (!keep_running) {
                pthread_mutex_unlock(&buf_lock);
                goto cleanup;
            }
        }

        /* Buffer empty and file done = we're finished */
        if (buf_count == 0) {
            pthread_mutex_unlock(&buf_lock);
            break;
        }

        struct gcode_msg m = buf[buf_tail];
        buf_tail = (buf_tail + 1) % BUF_LEN;
        buf_count--;
        pthread_cond_signal(&buf_not_full);
        pthread_mutex_unlock(&buf_lock);

        if (!keep_running) break;

        if (msgsnd(msg_id, &m, sizeof(m.command), IPC_NOWAIT) == -1) {
            if (errno == EAGAIN) {
                usleep(10000);
                buf_tail = (buf_tail - 1 + BUF_LEN) % BUF_LEN;
                buf_count++;
                continue;
            }
            perror("[g_reader] msgsnd");
            break;
        }
    }

cleanup:
    struct gcode_msg done = { .msg_type = 1 };
    strncpy(done.command, "DONE", sizeof(done.command));
    for (int i = 0; i < 3; i++) {
        if (msgsnd(msg_id, &done, sizeof(done.command), IPC_NOWAIT) == 0)
            break;
        usleep(100000);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) { fprintf(stderr, "Usage: g_reader <shm_id> <msg_id>\n"); exit(1); }
    shm_id = atoi(argv[1]);
    msg_id = atoi(argv[2]);

    shm = (PrinterSettings *)shmat(shm_id, NULL, 0);
    if (shm == (void *)-1) { perror("shmat"); exit(1); }

    signal(SIGUSR1, handle_sigusr1);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, file_reader, NULL);
    pthread_create(&t2, NULL, msg_sender,  NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    shmdt(shm);
    return 0;
}
