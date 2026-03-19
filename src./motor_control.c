/*
 * motor_control.c - FIXED VERSION
 *
 * T1 - msg_reader     : reads G0/G1 commands,
 *                       updates X Y Z in SHM, reduces spool weight.
 * T2 - temp_simulator : every second, increases temp toward max_temp
 *                       while printing, cools toward 250 when idle.
 */

#include "common.h"
#include <errno.h>

static int shm_id, msg_id;
static PrinterSettings *shm;
static volatile int keep_running = 1;

static void handle_sigusr1(int sig) {
    (void)sig;
    keep_running = 0;
    pthread_mutex_lock(&shm->shm_lock);
    shm->emergency_stop = 1;
    pthread_mutex_unlock(&shm->shm_lock);
}

static int parse_gcode(const char *cmd, float *x, float *y, float *z) {
    if ((cmd[0] != 'G' && cmd[0] != 'g')) return 0;
    int gnum = atoi(cmd + 1);
    if (gnum != 0 && gnum != 1) return 0;

    const char *p;
    if ((p = strchr(cmd, 'X')) || (p = strchr(cmd, 'x'))) *x = atof(p + 1);
    if ((p = strchr(cmd, 'Y')) || (p = strchr(cmd, 'y'))) *y = atof(p + 1);
    if ((p = strchr(cmd, 'Z')) || (p = strchr(cmd, 'z'))) *z = atof(p + 1);

    /* clamp to bed */
    if (*x < 0) *x = 0;
    if (*x > BED_MAX) *x = BED_MAX;
    if (*y < 0) *y = 0;
    if (*y > BED_MAX) *y = BED_MAX;
    if (*z < 0) *z = 0;
    return 1;
}

/* T1 - READ MESSAGES */
static void *msg_reader(void *arg) {
    (void)arg;
    struct gcode_msg m;

    while (keep_running) {
        if (msgrcv(msg_id, &m, sizeof(m.command), 1, IPC_NOWAIT) == -1) {
            if (errno == ENOMSG) {
                if (!keep_running) break;
                usleep(50000);
                continue;
            }
            if (keep_running) perror("[motor_control] msgrcv");
            break;
        }

        if (strcmp(m.command, "DONE") == 0) {
            pthread_mutex_lock(&shm->shm_lock);
            shm->job_done = 1;
            pthread_mutex_unlock(&shm->shm_lock);
            break;
        }

        pthread_mutex_lock(&shm->shm_lock);
        float nx = shm->X, ny = shm->Y, nz = shm->Z;
        pthread_mutex_unlock(&shm->shm_lock);

        if (parse_gcode(m.command, &nx, &ny, &nz)) {
            pthread_mutex_lock(&shm->shm_lock);
            shm->X = nx;
            shm->Y = ny;
            shm->Z = nz;
            if (shm->spool_weight > 0)
                shm->spool_weight--;
            pthread_mutex_unlock(&shm->shm_lock);
        }

        usleep(150000); /* 150 ms per step — slows print so UI is visible */
    }

    return NULL;
}

/* T2 - TEMPERATURE SIMULATION */
static void *temp_simulator(void *arg) {
    (void)arg;
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    while (keep_running) {
        sleep(1);

        pthread_mutex_lock(&shm->shm_lock);
        int running  = shm->is_running;
        int max_temp = shm->max_temp;
        int done     = shm->job_done;
        int estop    = shm->emergency_stop;

        if (estop || done) {
            pthread_mutex_unlock(&shm->shm_lock);
            break;
        }

        if (running) {
            int delta = (rand() % 5) - 1;  
            shm->temp += delta;
            if (shm->temp > max_temp) {
                fprintf(stderr, "[motor_control] Overtemp detected: %dC > %dC limit\n",
                        shm->temp, max_temp);
                shm->temp = max_temp;
                shm->emergency_stop = 1;
                pthread_mutex_unlock(&shm->shm_lock);
                keep_running = 0;
                break;
            }
            
            if (shm->temp < 200) shm->temp = 200;
        } else {
            if (shm->temp > 250) shm->temp--;
        }
        pthread_mutex_unlock(&shm->shm_lock);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) { fprintf(stderr, "Usage: motor_control <shm_id> <msg_id>\n"); exit(1); }
    shm_id = atoi(argv[1]);
    msg_id = atoi(argv[2]);

    shm = (PrinterSettings *)shmat(shm_id, NULL, 0);
    if (shm == (void *)-1) { perror("shmat"); exit(1); }

    signal(SIGUSR1, handle_sigusr1);

    pthread_mutex_lock(&shm->shm_lock);
    shm->is_running = 1;
    pthread_mutex_unlock(&shm->shm_lock);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, msg_reader,     NULL);
    pthread_create(&t2, NULL, temp_simulator, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    pthread_mutex_lock(&shm->shm_lock);
    shm->is_running = 0;
    pthread_mutex_unlock(&shm->shm_lock);

    shmdt(shm);
    return 0;
}
