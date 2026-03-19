/*
 * supervisor.c
 *
 * T1 - safety_polling  : every second checks temp and spool_weight.
 *                        Sends SIGUSR1 to motor and greader if limits breached.
 * T2 - trigger_monitor : watches emergency_stop and job_done flags in SHM.
 *                        Broadcasts SIGUSR1 to workers when either is set.
 *
 * Stops on SIGUSR1 from ui/init.
 */

#include "common.h"

static int              shm_id, msg_id;
static PrinterSettings *shm;
static volatile int     keep_running = 1;

static void handle_sigusr1(int sig) {
    (void)sig;
    keep_running = 0;
}

static void stop_workers(void) {
    pthread_mutex_lock(&shm->shm_lock);
    pid_t m = shm->motor_pid;
    pid_t g = shm->greader_pid;
    shm->emergency_stop = 1;
    pthread_mutex_unlock(&shm->shm_lock);
    if (m > 0) kill(m, SIGUSR1);
    if (g > 0) kill(g, SIGUSR1);
}

/* T1 */
static void *safety_polling(void *arg) {
    (void)arg;

    while (keep_running) {
        sleep(1);

        pthread_mutex_lock(&shm->shm_lock);
        int temp     = shm->temp;
        int max_temp = shm->max_temp;
        int spool    = shm->spool_weight;
        int estop    = shm->emergency_stop;
        int done     = shm->job_done;
        pthread_mutex_unlock(&shm->shm_lock);

        if (estop || done) break;

        if (temp > max_temp) {
            fprintf(stderr, "[supervisor] OVERTEMP %dC > %dC — stopping.\n", temp, max_temp);
            stop_workers();
            break;
        }

        if (spool <= 0) {
            fprintf(stderr, "[supervisor] Spool empty — stopping.\n");
            stop_workers();
            break;
        }
    }

    return NULL;
}

/* T2 */
static void *trigger_monitor(void *arg) {
    (void)arg;

    while (keep_running) {
        usleep(200000);  

        pthread_mutex_lock(&shm->shm_lock);
        int estop = shm->emergency_stop;
        int done  = shm->job_done;
        pthread_mutex_unlock(&shm->shm_lock);

        if (estop) {
            stop_workers();   /
            break;
        }

        if (done) {
            break;
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) { fprintf(stderr, "Usage: supervisor <shm_id> <msg_id>\n"); exit(1); }
    shm_id = atoi(argv[1]);
    msg_id = atoi(argv[2]);

    shm = (PrinterSettings *)shmat(shm_id, NULL, 0);
    if (shm == (void *)-1) { perror("shmat"); exit(1); }

    signal(SIGUSR1, handle_sigusr1);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, safety_polling,  NULL);
    pthread_create(&t2, NULL, trigger_monitor, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    shmdt(shm);
    return 0;
}
