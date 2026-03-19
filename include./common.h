#ifndef  COMMON_H
#define  COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include <fcntl.h>
#include <time.h>
#include <math.h>

typedef struct{
  char material[32];
  char gcode_file[32];
  int spool_weight;
  int max_temp;
  int temp;
  float X, Y, Z;
  int is_running;
  int emergency_stop;
  int job_done;
  pid_t motor_pid;
  pid_t greader_pid;
  pthread_mutex_t shm_lock;
} PrinterSettings;

struct gcode_msg{
  long msg_type;
  char command[64];
};

#define GRID_SIZE 10
#define BED_MAX 30.0
#define PROJECT_KEY_ID 65
#define STR_MAX 32

#endif
