/*
 * ui.c  - top and front view SIDE BY SIDE
 *
 * LEFT  : TOP VIEW   (X horizontal, Y vertical)
 * RIGHT : FRONT VIEW (X horizontal, Z vertical)
 *
 * '.' = filament placed   '@' = current head position
 */

#include "common.h"
#include <termios.h>

static int              shm_id;
static PrinterSettings *shm;
static volatile int     keep_running = 1;
static volatile int     paused       = 0;

/* ── grid dimensions ────────────────────────────────────────────── */
#define GCOLS  21
#define GROWS  21
#define GAP    4

/* persistent filament maps */
static char top_grid  [GROWS][GCOLS];
static char front_grid[GROWS][GCOLS];
static pthread_mutex_t grid_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── ANSI helpers ───────────────────────────────────────────────── */
#define GOTO(r,c)  printf("\033[%d;%dH", (r), (c))
#define CLEAR()    printf("\033[2J\033[H")

/* ── raw terminal ───────────────────────────────────────────────── */
static struct termios orig_termios;

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}
static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

/* ── coordinate mapping ─────────────────────────────────────────── */
static int map_x(float x) {
    int v = (int)((x / BED_MAX) * (GCOLS - 1));
    if (v < 0) v = 0; 
    if (v >= GCOLS) v = GCOLS-1;
    return v;
}
static int map_y(float y) {
    int v = (int)((y / BED_MAX) * (GROWS - 1));
    if (v < 0) v = 0;
    if (v >= GROWS) v = GROWS-1;
    return v;
}
static int map_z(float z) {
    int v = (int)((z / 25.0f) * (GROWS - 1));
    if (v < 0) v = 0;
    if (v >= GROWS) v = GROWS-1;
    return v;
}

/* ── mark filament ──────────────────────────────────────────────── */
static void mark_filament(float x, float y, float z) {
    int gx = map_x(x);
    int gy = map_y(y);
    int gz = map_z(z);
    pthread_mutex_lock(&grid_lock);
    top_grid  [GROWS-1-gy][gx] = '.';
    front_grid[GROWS-1-gz][gx] = '.';
    pthread_mutex_unlock(&grid_lock);
}

/*
 * Screen layout (all rows 1-indexed):
 *
 * Row 1 : title
 * Row 2 : status / temp / spool
 * Row 3 : position
 * Row 4 : legend
 * Row 5 : blank
 * Row 6 : column headers
 * Row 7 : top border    +---------+   +---------+
 * Row 8..28 : grid rows (GROWS=21)
 * Row 29: bottom border
 * Row 30: X label
 * Row 31: blank
 * Row 32: controls
 *
 * Left grid  left border  col = LC = 1
 * Right grid left border  col = RC = 1 + 1 + GCOLS + 1 + GAP = GCOLS+3+GAP
 */

#define ROW_TITLE  1
#define ROW_STATUS 2
#define ROW_POS    3
#define ROW_LEGEND 4
#define ROW_HDR    6
#define ROW_TBDR   7
#define ROW_GRID   8
#define ROW_BBDR   (ROW_GRID + GROWS)
#define ROW_XLABEL (ROW_BBDR + 1)
#define ROW_CTRL   (ROW_XLABEL + 2)

#define LC   1
#define RC   (LC + 1 + GCOLS + 1 + GAP)

static void draw_hborder(int row, int col) {
    GOTO(row, col);
    printf("+");
    for (int i = 0; i < GCOLS; i++) printf("-");
    printf("+");
}

static void redraw(float X, float Y, float Z,int temp, int spool,int estop, int done,const char *mat){
    int hx  = map_x(X);
    int hy  = map_y(Y);
    int hz  = map_z(Z);
    int tr  = GROWS - 1 - hy;   /* top view head row   */
    int fr  = GROWS - 1 - hz;   /* front view head row */

    /* title */
    GOTO(ROW_TITLE, LC);
    printf("=== MINIPRJLINUX 3D Printer === Mat:%-6s\n", mat);

    /* status */
    GOTO(ROW_STATUS, LC);
    printf("Status: %-20s | Temp:%3dC | Spool:%4dg   ",
           estop  ? "!! EMERGENCY STOP !!" :
           done   ? "JOB COMPLETE"         :
           paused ? "PAUSED"               : "PRINTING...",
           temp, spool);

    /* position */
    GOTO(ROW_POS, LC);
    printf("Head: X=%-5.1f Y=%-5.1f Z=%-5.1f                    ",
           X, Y, Z);

    /* legend */
    GOTO(ROW_LEGEND, LC);
    printf("@ = print head    . = filament placed               ");

    /* headers */
    GOTO(ROW_HDR, LC);
    printf("TOP VIEW (XY)");
    GOTO(ROW_HDR, RC);
    printf("FRONT VIEW (XZ)");

    /* borders */
    draw_hborder(ROW_TBDR, LC);
    draw_hborder(ROW_TBDR, RC);
    draw_hborder(ROW_BBDR, LC);
    draw_hborder(ROW_BBDR, RC);

    /* grid content */
    pthread_mutex_lock(&grid_lock);
    for (int r = 0; r < GROWS; r++) {
        int term_row = ROW_GRID + r;

        /* top view row */
        GOTO(term_row, LC);
        printf("|");
        for (int c = 0; c < GCOLS; c++) {
            if (r == tr && c == hx)
                printf("@");
            else
                printf("%c", top_grid[r][c] ? top_grid[r][c] : ' ');
        }
        printf("|");

        /* front view row */
        GOTO(term_row, RC);
        printf("|");
        for (int c = 0; c < GCOLS; c++) {
            if (r == fr && c == hx)
                printf("@");
            else
                printf("%c", front_grid[r][c] ? front_grid[r][c] : ' ');
        }
        printf("|");
    }
    pthread_mutex_unlock(&grid_lock);

    /* X axis labels */
    GOTO(ROW_XLABEL, LC);
    printf("0");
    for (int i = 1; i < GCOLS; i++) printf(" ");
    printf("X");

    GOTO(ROW_XLABEL, RC);
    printf("0");
    for (int i = 1; i < GCOLS; i++) printf(" ");
    printf("X");

    /* controls */
    GOTO(ROW_CTRL, LC);
    printf("[P] Pause   [R] Resume   [Q] Quit");

    fflush(stdout);
}

/* ── T1 : renderer ─────────────────────────────────────────────── */
static void *renderer(void *arg) {
    (void)arg;
    float last_x = -1, last_y = -1, last_z = -1;

    while (keep_running) {
        usleep(200000);

        pthread_mutex_lock(&shm->shm_lock);
        float X     = shm->X;
        float Y     = shm->Y;
        float Z     = shm->Z;
        int   temp  = shm->temp;
        int   spool = shm->spool_weight;
        int   estop = shm->emergency_stop;
        int   done  = shm->job_done;
        char  mat[STR_MAX];
        strncpy(mat,  shm->material, STR_MAX-1); mat[STR_MAX-1]  = '\0';
        pthread_mutex_unlock(&shm->shm_lock);

        if (X != last_x || Y != last_y || Z != last_z) {
            if (last_x >= 0)
                mark_filament(X, Y, Z);
            last_x = X; last_y = Y; last_z = Z;
        }

        redraw(X, Y, Z, temp, spool, estop, done, mat);

        if (estop || done)
            keep_running = 0;
    }

    return NULL;
}

/* ── T2 : input scanner ────────────────────────────────────────── */
static void *input_scan(void *arg) {
    (void)arg;

    while (keep_running) {
        char ch = 0;
        if (read(STDIN_FILENO, &ch, 1) <= 0) {
            usleep(50000);
            continue;
        }

        pthread_mutex_lock(&shm->shm_lock);
        pid_t mpid = shm->motor_pid;
        pid_t gpid = shm->greader_pid;
        pthread_mutex_unlock(&shm->shm_lock);

        if (ch == 'p' || ch == 'P') {
            if (!paused) {
                paused = 1;
                if (mpid > 0 && kill(mpid, 0) == 0) kill(mpid, SIGSTOP);
                if (gpid > 0 && kill(gpid, 0) == 0) kill(gpid, SIGSTOP);
                pthread_mutex_lock(&shm->shm_lock);
                shm->is_running = 0;
                pthread_mutex_unlock(&shm->shm_lock);
            }
        } else if (ch == 'r' || ch == 'R') {
            if (paused) {
                paused = 0;
                if (mpid > 0 && kill(mpid, 0) == 0) kill(mpid, SIGCONT);
                if (gpid > 0 && kill(gpid, 0) == 0) kill(gpid, SIGCONT);
                pthread_mutex_lock(&shm->shm_lock);
                shm->is_running = 1;
                pthread_mutex_unlock(&shm->shm_lock);
            }
        } else if (ch == 'q' || ch == 'Q') {
            pthread_mutex_lock(&shm->shm_lock);
            shm->emergency_stop = 1;
            pthread_mutex_unlock(&shm->shm_lock);
            if (mpid > 0 && kill(mpid, 0) == 0) kill(mpid, SIGUSR1);
            if (gpid > 0 && kill(gpid, 0) == 0) kill(gpid, SIGUSR1);
            keep_running = 0;
        }
    }

    return NULL;
}

/* ── main ──────────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc < 3) { fprintf(stderr, "Usage: ui <shm_id> <msg_id>\n"); exit(1); }
    shm_id = atoi(argv[1]);

    shm = (PrinterSettings *)shmat(shm_id, NULL, 0);
    if (shm == (void *)-1) { perror("shmat"); exit(1); }

    memset(top_grid,   0, sizeof(top_grid));
    memset(front_grid, 0, sizeof(front_grid));

    /* hide cursor, clear screen */
    printf("\033[?25l");
    CLEAR();
    fflush(stdout);

    enable_raw_mode();

    pthread_t t1, t2;
    pthread_create(&t1, NULL, renderer,   NULL);
    pthread_create(&t2, NULL, input_scan, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    disable_raw_mode();
    printf("\033[?25h");
    CLEAR();
    GOTO(1,1);
    printf("Printer session ended.\n");
    fflush(stdout);

    shmdt(shm);
    return 0;
}
