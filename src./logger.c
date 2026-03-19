/*
 * logger.c
 *
 * Single main loop — wakes every 500 ms via usleep.
 * Reads all fields from SHM and appends a timestamped line to printer.log.
 * Exits when emergency_stop or job_done is set.
 */

#include "common.h"
#include <sys/time.h>

static int shm_id;
static PrinterSettings *shm;
static volatile int keep_running = 1;

static void handle_sigusr1(int sig) {
    (void)sig;
    keep_running = 0;
}

static void timestamp(char *buf, size_t len) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", tm_info);
}

int main(int argc, char *argv[]) {
    if (argc < 3) { fprintf(stderr, "Usage: logger <shm_id> <msg_id>\n"); exit(1); }
    shm_id = atoi(argv[1]);

    shm = (PrinterSettings *)shmat(shm_id, NULL, 0);
    if (shm == (void *)-1) { perror("shmat"); exit(1); }

    signal(SIGUSR1, handle_sigusr1);

    FILE *fp = fopen("printer.log", "a");
    if (!fp) { perror("[logger] fopen printer.log"); shmdt(shm); exit(1); }

    while (keep_running) {
        usleep(500000);

        pthread_mutex_lock(&shm->shm_lock);
        float X     = shm->X;
        float Y     = shm->Y;
        float Z     = shm->Z;
        int   temp  = shm->temp;
        int   spool = shm->spool_weight;
        int   estop = shm->emergency_stop;
        int   done  = shm->job_done;
        int   run   = shm->is_running;
        char  mat[STR_MAX];
        strncpy(mat, shm->material, STR_MAX - 1); mat[STR_MAX-1] = '\0';
        pthread_mutex_unlock(&shm->shm_lock);

        char ts[32];
        timestamp(ts, sizeof(ts));

        fprintf(fp, "[%s] X=%.2f Y=%.2f Z=%.2f | Temp=%dC | Spool=%dgm | Mat=%s | Run=%d | EStop=%d | Done=%d\n", ts, X, Y, Z, temp, spool, mat, run, estop, done);
        fflush(fp);
        if (estop || done) break;
    }
    fclose(fp);
    shmdt(shm);
    return 0;
}
