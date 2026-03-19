/*
 * init.c
 * - Sets up SHM and MSG QUEUE
 * - Reads config.txt into SHM
 * - Forks and execs all child processes
 * - Waits for all children to finish
 * - Cleans up IPC resources
 */

#include "common.h"

void cleanup_old_ipc(void) {
    key_t key = ftok(".",PROJECT_KEY_ID);
    	if(key !=-1){
	   int old_msgid = msgget(key,0);
	   if(old_msgid !=-1){
		msgctl(old_msgid,IPC_RMID,NULL);
	    }
	}
}
int setup_shm(void) {
    int shm_id = shmget(IPC_PRIVATE, sizeof(PrinterSettings), IPC_CREAT | 0666);
    if (shm_id == -1) { perror("shmget"); exit(1); }
    return shm_id;
}

int setup_msg_queue(void) {
    key_t key = ftok(".", PROJECT_KEY_ID);
    if (key == -1) { perror("ftok"); exit(1); }
    int msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) { perror("msgget"); exit(1); }
    return msgid;
}

void read_config(PrinterSettings *s) {
    FILE *f = fopen("config.txt", "r");
    if (!f) { perror("fopen config.txt"); return; }
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        sscanf(line, "MATERIAL =%31s",  s->material);
	sscanf(line, "GCODE_FILE =%31s",s->gcode_file);
        sscanf(line, "SPOOL_WEIGHT =%d",&s->spool_weight);
        sscanf(line, "MAX_TEMP =%d", &s->max_temp);
       
    }
    fclose(f);
}

pid_t create_process(const char *path, int shm_id, int msg_id) {
    pid_t pid = fork();
    if (pid < 0)  { perror("fork"); exit(1); }
    if (pid == 0) {
        char s[20], m[20];
        sprintf(s, "%d", shm_id);
        sprintf(m, "%d", msg_id);
        execl(path, path, s, m, (char *)NULL);
        perror("execl");
        exit(1);
    }
    return pid;
}

int main(void) {
    cleanup_old_ipc();
    int shm_id = setup_shm();
    int msg_id = setup_msg_queue();

    PrinterSettings *shm = (PrinterSettings *)shmat(shm_id, NULL, 0);
    if (shm == (void *)-1) { perror("shmat"); exit(1); }

    
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shm->shm_lock, &attr);
    pthread_mutexattr_destroy(&attr);
    
    shm->X             = 0;
    shm->Y             = 0;
    shm->Z             = 0;
    shm->temp          = 220;
    shm->is_running    = 0;
    shm->emergency_stop= 0;
    shm->job_done      = 0;
    shm->spool_weight  = 0;
    shm->max_temp      = 200;
    shm->motor_pid     = 0;
    shm->greader_pid   = 0;
    memset(shm->material, 0, STR_MAX);

    read_config(shm);

    printf("[init] SHM id=%d  MSG id=%d\n", shm_id, msg_id);
    printf("[init] Material=%s Spool=%dg MaxTemp=%dC\n",shm->material, shm->spool_weight, shm->max_temp);

    shm->motor_pid   = create_process("./motor_control", shm_id, msg_id);
    shm->greader_pid = create_process("./g_reader",      shm_id, msg_id);
    pid_t supervisor_pid = create_process("./supervisor", shm_id, msg_id);
    pid_t logger_pid     = create_process("./logger",     shm_id, msg_id);
    pid_t ui_pid         = create_process("./ui",         shm_id, msg_id);

    
    waitpid(ui_pid, NULL, 0);

    
    pthread_mutex_lock(&shm->shm_lock);
    shm->emergency_stop = 1;
    pthread_mutex_unlock(&shm->shm_lock);

    kill(shm->motor_pid,   SIGUSR1);
    kill(shm->greader_pid, SIGUSR1);
    kill(supervisor_pid,   SIGUSR1);
    kill(logger_pid,       SIGUSR1);

    waitpid(logger_pid,      NULL, 0);
    waitpid(supervisor_pid,  NULL, 0);
    waitpid(shm->motor_pid,  NULL, 0);
    waitpid(shm->greader_pid,NULL, 0);
    printf("[init] All processes done. IPC cleaned up.\n");
    return 0;
}
