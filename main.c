#define _XOPEN_SOURCE 600
#include <ctype.h>
#include <pty.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "nhbot.h"
#include "qlearn.h"
#include "tmt.h"

// NetHack pid
pid_t pid;

// NetHack stdin
int master_write_fd;

// NetHack state
NetHackState nethack_state = {0};

// Kill NetHack
static int nhbot_shutdown(void)
{
    kill(pid, SIGTERM);
    return 0;
}

// Handle signals
static void handle_signal(int signum)
{
    switch (signum) {
    case SIGUSR1:
        break;
    case SIGINT:
    case SIGTERM:
        nhbot_shutdown();
        break;
    default:
        fprintf(stderr, "unhandled signal: %d", signum);
        break;
    }
}

// Random number in range of lower to upper
static inline int randrange (int lower, int upper)
{
    return (rand() % (upper - lower + 1)) + lower;
}

// Write a char to NetHack stdin
ssize_t nhbot_write(int fd, uint8_t *c, size_t len)
{
    ssize_t result = -1;
    check((result = write(fd, c, len)) == (ssize_t)len);
    return result;
error:
    return result;
}

// Write the ascii (ansi stripped) NetHack screen to stdout
static int write_output_nethack(NetHackState *nethack_state)
{
    return write(STDOUT_FILENO, nethack_state->ScreenChar,
                 VT_W*VT_H*sizeof(uint8_t));
}

// Extract color from TMT char
// Set vga bright bit if bold
static unsigned char tmt_char_color(TMTCHAR *c)
{
    unsigned char color;
    const int vga_bright = 8;
    const int vga_num_colors = 16;
    check(c->a.fg < TMT_COLOR_MAX);

    if (c->a.fg == TMT_COLOR_DEFAULT) {
        color = (c->c == ' ') ? TMT_COLOR_BLACK: (TMT_COLOR_BLACK|vga_bright);
    } else {
        color = c->a.bold ? (c->a.fg | vga_bright) : c->a.fg;
    }

    if (c->a.reverse) {
        color += vga_num_colors;
    }
    return color;
error:
    return TMT_COLOR_RED|vga_bright;
}

// Process a TMT char
static void tmt_callback_handle_char(NetHackState *nethack_state,
                                     size_t r, size_t c, TMTCHAR *tmt_c)
{
    nethack_state->ScreenChar[r * VT_W + c] = tmt_c->c & 0xff;
    nethack_state->ScreenColor[r * VT_W + c] = tmt_char_color(tmt_c);
}

// Called when we tmt_write()
static void nhbot_tmt_callback(tmt_msg_t m, TMT *vt, const void *a, void *p)
{
    size_t r, c;
    const TMTSCREEN *s = a;
    const TMTPOINT *cursor = tmt_cursor(vt);
    NetHackState *nethack_state = p;

    (void)p;

    switch (m) {
    case TMT_MSG_MOVED:
        nethack_state->CursorRow = (int)cursor->r;
        nethack_state->CursorCol = (int)cursor->c;
        break;
    case TMT_MSG_UPDATE:
        for (r = 0; r < s->nline; r++) {
            if(s->lines[r]->dirty) {
                for (c = 0; c < s->ncol; c++) {
                    TMTCHAR *tmt_c = &(s->lines[r]->chars[c]);
                    tmt_callback_handle_char(nethack_state, r, c, tmt_c);
                }
            }
        }
        nethack_state->ScreenChar[r * VT_W + c] = 0;
        nethack_state->ScreenColor[r * VT_W + c] = 0;
        tmt_clean(vt);
        break;
    default:
        break;
    }
}

// Check haystack for needle
static bool screen_text_exists(uint8_t *haystack, int hlen,
                        const char *needle, int nlen)
{
    if (nlen > hlen) {
        return false;
    }
    for (int i = 0; i < hlen - nlen; i++) {
        if (memcmp(haystack + i, needle, nlen) == 0) {
            return true;
        }
    }
    return false;
}

// Locate needle in haystack
static int screen_text_find(uint8_t *haystack, int hlen,
                     const char *needle, int nlen)
{
    if (nlen > hlen) {
        return false;
    }
    for (int i = 0; i < hlen - nlen; i++) {
        if (memcmp(haystack + i, needle, nlen) == 0) {
            return i;
        }
    }
    return -1;
}

// 10 char random string
static void random_string10(char out[11])
{
    const char allow[] = "abcdefghijklmnopqrstuvwxyz1234567890";
    int i = 0;
    int c = 0;
    int len = sizeof(allow)-1;
    for(i=0;i<10;i++) {
        c = rand() % len;
        out[i] = allow[c];
    }
    out[10] = '\0';
}

// Find specified bottom line stat
static int screen_text_bl_int(int hstart, uint8_t *haystack, int hlen,
                       const char *needle, int nlen)
{
    char buf[32] = {0};
    (void)hstart;
    int screen_pos = screen_text_find(haystack, hlen,
                                needle, nlen);
    uint8_t *end;
    uint8_t *start = haystack + screen_pos + nlen;
    for(end=start; isdigit(*end); end++);
    memcpy(buf, start, end - start);
    return atoi(buf);
}

// Watch for text that requires user input,
// send input, if necessary
static void screen_text_process(NetHackState *nethack_state, int fd)
{
    char buf[64] = {0};

    const char *more_text = "--More--";
    const size_t more_text_len = strlen(more_text);

    const char *yn_text = "[yn";
    const size_t yn_text_len = strlen(yn_text);

    const char *hungry_text = "Hungry";
    const size_t hungry_text_len = strlen(hungry_text);

    const char *burdened_text = "Burdened";
    const size_t burdened_text_len = strlen(burdened_text);

    const char *stressed_text= "Stressed";
    const size_t stressed_text_len = strlen(stressed_text);

    const char *call_a_text = "Call a";
    const size_t call_a_text_len = strlen(call_a_text);

    const char *hello_stranger_text = "Hello stranger";
    const size_t hello_stranger_text_len = strlen(hello_stranger_text);

    const char *what_do_you_want_text = "What do you want";
    const size_t what_do_you_want_text_len = strlen(what_do_you_want_text);

    const char *you_are_required_text = "You are required";
    const size_t you_are_required_text_len = strlen(you_are_required_text);

    // "--More--"
    nethack_state->PromptMore = screen_text_exists(
                                nethack_state->ScreenChar,
                                VT_W*VT_H, more_text, more_text_len);

    // "[yn"
    nethack_state->PromptYn = screen_text_exists(
                              nethack_state->ScreenChar,
                              VT_W*VT_H, yn_text, yn_text_len);

    // "Hungry"
    nethack_state->StatusHungry = screen_text_exists(
                                  nethack_state->ScreenChar,
                                  VT_W*VT_H, hungry_text,
                                  hungry_text_len);

    // "Burdened"
    nethack_state->StatusBurdened = screen_text_exists(
                                    nethack_state->ScreenChar,
                                    VT_W*VT_H, burdened_text,
                                    burdened_text_len);

    // "Stressed"
    if (!nethack_state->StatusBurdened) {
        nethack_state->StatusBurdened = screen_text_exists(
                                        nethack_state->ScreenChar,
                                        VT_W*VT_H, stressed_text,
                                        stressed_text_len);
    }

    // "Call a"
    if (screen_text_exists(nethack_state->ScreenChar,
        VT_W*VT_H, call_a_text, call_a_text_len)) {
        char string10[11];
        random_string10(string10);
        nhbot_write(fd, (uint8_t*)string10, 10);
        nhbot_write(fd, (uint8_t*)"\n", 1);
    }
    // "Hello stranger"
    if (screen_text_exists(nethack_state->ScreenChar,
        VT_W*VT_H, hello_stranger_text, hello_stranger_text_len)) {
        char string10[11];
        random_string10(string10);
        nhbot_write(fd, (uint8_t*)string10, 10);
        nhbot_write(fd, (uint8_t*)"\n", 1);
    }
    // "You are required"
    if (screen_text_exists(nethack_state->ScreenChar,
        VT_W*VT_H, you_are_required_text, you_are_required_text_len)) {
        char string10[11];
        random_string10(string10);
        nhbot_write(fd, (uint8_t*)string10, 10);
        nhbot_write(fd, (uint8_t*)"\n", 1);
    }
    // "What do you want"
    if (screen_text_exists(nethack_state->ScreenChar,
        VT_W*VT_H, what_do_you_want_text, what_do_you_want_text_len)) {
        nhbot_write(fd, (uint8_t*)" ", 1);
        nhbot_write(fd, (uint8_t*)"\n", 1);
    }

    // "St:"
    int guess = 1;
    int offset = (VT_H-1) * VT_W + guess;
    nethack_state->BlStat.St = screen_text_bl_int(
                                   offset, nethack_state->ScreenChar,
                                   VT_H*VT_W, "St:", 3);
    // "Dx:"
    guess = 7;
    offset = (VT_H-1) * VT_W + guess;
    nethack_state->BlStat.Dx = screen_text_bl_int(
                                   offset, nethack_state->ScreenChar,
                                   VT_H*VT_W, "Dx:", 3);
    // "Co:"
    guess = 12;
    offset = (VT_H-1) * VT_W + guess;
    nethack_state->BlStat.Co = screen_text_bl_int(
                                   offset, nethack_state->ScreenChar,
                                   VT_H*VT_W, "Co:", 3);
    // "In:"
    guess = 18;
    offset = (VT_H-1) * VT_W + guess;
    nethack_state->BlStat.In = screen_text_bl_int(
                                   offset, nethack_state->ScreenChar,
                                   VT_H*VT_W, "In:", 3);
    // "Wi:"
    guess = 23;
    offset = (VT_H-1) * VT_W + guess;
    nethack_state->BlStat.Wi = screen_text_bl_int(
                                   offset, nethack_state->ScreenChar,
                                   VT_H*VT_W, "Wi:", 3);
    // "Ch:"
    guess = 29;
    offset = (VT_H-1) * VT_W + guess;
    nethack_state->BlStat.Ch = screen_text_bl_int(
                                   offset, nethack_state->ScreenChar,
                                   VT_H*VT_W, "Ch:", 3);
    // "Dlvl:"
    guess = 0;
    offset = (VT_H-0) * VT_W + guess;
    nethack_state->BlStat.Dlvl = screen_text_bl_int(
                                   offset, nethack_state->ScreenChar,
                                   VT_H*VT_W, "Dlvl:", 5);

    // "$:"
    guess = 6;
    offset = (VT_H-0) * VT_W + guess;
    nethack_state->BlStat.Money = screen_text_bl_int(
                                   offset, nethack_state->ScreenChar,
                                   VT_H*VT_W, "$:", 2);

    // "HP"
    guess = 11;
    offset = (VT_H-0) * VT_W + guess;
    nethack_state->BlStat.HP = screen_text_bl_int(
                                  offset, nethack_state->ScreenChar,
                                  VT_H*VT_W, "HP:", 3);
    // "Pw"
    guess = 18;
    offset = (VT_H-0) * VT_W + guess;
    nethack_state->BlStat.Pw = screen_text_bl_int(
                                  offset, nethack_state->ScreenChar,
                                  VT_H*VT_W, "Pw:", 3);
    // "Ac"
    guess = 27;
    offset = (VT_H-0) * VT_W + guess;
    nethack_state->BlStat.Ac = screen_text_bl_int(
                                  offset, nethack_state->ScreenChar,
                                  VT_H*VT_W, "Ac:", 3);
    // "Xp"
    guess = 32;
    offset = (VT_H-0) * VT_W + guess;
    nethack_state->BlStat.Xp = screen_text_bl_int(
                                  offset, nethack_state->ScreenChar,
                                  VT_H*VT_W, "Xp:", 3);
    // "T"
    guess = 38;
    offset = (VT_H-0) * VT_W + guess;
    nethack_state->BlStat.T = screen_text_bl_int(
                                  offset, nethack_state->ScreenChar,
                                  VT_H*VT_W, "T:", 2);

    nethack_state->PlayerRow = -1;
    nethack_state->PlayerCol = -1;
    for (int i = 0; i < VT_W*VT_H; i++) {
        if(nethack_state->ScreenChar[i] == '@'
            && nethack_state->ScreenColor[i]&0x08) {
            nethack_state->PlayerRow = i / VT_W;
            nethack_state->PlayerCol= i % VT_W;
        }
    }
}

// Construct a binary qmap
static void nhbot_make_qmap(NetHackState *nethack_state, uint8_t out[VT_W*VT_H])
{
    int start_x = 0;
    int start_y = 0;
    for (int i = 0; i < VT_W; ++i) {
        for (int j = 0; j < VT_H; ++j) {
            int screen_x = start_x + i;
            int screen_y = start_y + j;
            out[j * VT_W + i] = nethack_state->ScreenChar[screen_y * VT_W + screen_x];
        }
    }
}

// Called before writing an action to NetHack
static int action_prologue(NetHackActionEnum action, int fd)
{
    switch(action) {
    case Command_EAT:
        nhbot_write(fd, (uint8_t*)"m", sizeof(uint8_t));
        break;
    default:
        (void)fd;
        break;
    }
    return 0;
}

// Called after writing an action to NetHack
static int action_epilogue(NetHackActionEnum action, int fd)
{
    switch(action) {
    case Command_EAT:
        switch(randrange(0, 6)) {
        case 0:
            nhbot_write(fd, (uint8_t*)"f", sizeof(uint8_t));
            break;
        case 1:
            nhbot_write(fd, (uint8_t*)"g", sizeof(uint8_t));
            break;
        case 2:
            nhbot_write(fd, (uint8_t*)"h", sizeof(uint8_t));
            break;
        case 3:
            //nhbot_write(fd, (uint8_t*)"i", sizeof(uint8_t));
            break;
        case 4:
            nhbot_write(fd, (uint8_t*)"j", sizeof(uint8_t));
            break;
        }
        break;
    case Command_DROP:
        nhbot_write(fd, (uint8_t*)"A", sizeof(uint8_t));
        nhbot_write(fd, (uint8_t*)"\n", sizeof(uint8_t));
        break;
    default:
        (void)fd;
        break;
    }
    return 0;
}

// Call action prologue, write action char, call action epilogue
static int nhbot_perform_action(NetHackActionEnum action, int fd)
{
    int result = -1;

    if (action == Command_DoNothing) {
        return 0;
    }

    // Check for a valid action
    check(action >= 0 && action < NetHackActionEnum_Count);

    check(action_prologue(action, fd) != -1);

    // Write the action character to fd
    uint8_t *c = &NetHackActionLookup[action].ActionChar;
    size_t len = sizeof(uint8_t);
    check(nhbot_write(fd, c, len) != -1);

    check(action_epilogue(action, fd) != -1);

error:
    return result;
}

// Wrapper for nhbot_perform_action
static int nhbot_action(NetHackActionEnum actionId)
{
    nethack_state.Action = NetHackActionLookup[actionId];
    return nhbot_perform_action(actionId, master_write_fd);
}

// Main io loop
// * Wait for screen change
// * Process screen text
// * Genreate qmap
// * Proces qmap
// * Send resulting action to NetHack
static void nhbot_loop(void *params)
{
    int maxfd;
    ssize_t nread;
    fd_set readable;
    pos_t agent;
    int qmap[5*5];
    static char buf[BUFLEN];
    struct io_thread_params *p = (struct io_thread_params*)params;
    struct timeval tv = {0, 1000*100};
    NetHackState *nethack_state = p->nethack_state;

    struct PTY pty = p->pty;
    TMT *vt = p->vt;

    maxfd = pty.master;
    for(;;) {
        FD_ZERO(&readable);
        FD_SET(pty.master, &readable);
        if (select(maxfd + 1, &readable, NULL, NULL, &tv) == -1) {
            fprintf(stderr, "select():%s:%d ", __FILE__, __LINE__);
            continue;
        }

        if (kill(p->pid, 0) == -1) {
            return;
        }

        if (FD_ISSET(pty.master, &readable)) {
            if ((nread = read(pty.master, buf, BUFLEN)) <= 0) {
                fprintf(stderr, "read():%s:%d ", __FILE__, __LINE__);
                continue;
            }
            tmt_write(vt, buf, nread);
        }

        screen_text_process(nethack_state, pty.master);

        usleep(1000*10);
        if (nethack_state->PlayerRow != -1
                && nethack_state->PlayerCol != -1) {
            agent.y = nethack_state->PlayerRow;
            agent.x = nethack_state->PlayerCol;
            nhbot_make_qmap(nethack_state, nethack_state->ScreenChar);
            nhbot_qlearn_set_env(nethack_state->ScreenChar);
            nhbot_qlearn(&agent);
            switch(ChooseAgentAction(&agent, EXPLOIT)) {
            case 0:
                nhbot_action(CompassDirection_N);
                break;
            case 1:
                nhbot_action(CompassDirection_E);
                break;
            case 2:
                nhbot_action(CompassDirection_S);
                break;
            case 3:
                nhbot_action(CompassDirection_W);
                break;
            }

            if (nethack_state->PromptMore) {
                nhbot_action(TextCharacters_SPACE);
            }
            else if(nethack_state->PromptYn) {
                nhbot_action(TextCharacters_n);
            }
            else if (nethack_state->StatusHungry) {
                nhbot_action(Command_EAT);
            }
            else if (nethack_state->StatusBurdened) {
                nhbot_action(Command_DROP);
            }
        }

        write_output_nethack(nethack_state);
    }

    return;
}

// Child process, i.e., NetHack 
static int fork_handle_child(struct PTY *pty, const char *nethack_path, const char **env)
{
    setsid();
    check(ioctl(pty->master, TIOCSCTTY, NULL) != -1);
    check(dup2(pty->slave, 0) != -1);
    check(dup2(pty->slave, 1) != -1);
    check(dup2(pty->slave, 2) != -1);
    check(close(pty->slave) != -1);
    check(execle(nethack_path, "", (char *)NULL, env) != -1);

    return 0;

error:
    return -1;
}

// Parent process, i.e., io loop
static int fork_handle_parent(struct io_thread_params *params)
{
    int wait_status;
    pthread_t io_thread_id;

    dup2(params->pty.master, master_write_fd);
    check(write(master_write_fd, " ", sizeof(char)) != -1)
    check(write(master_write_fd, " ", sizeof(char)) != -1)
    nhbot_loop(params);
    
    waitpid(pid, &wait_status, WUNTRACED);
    //pthread_join(io_thread_id, NULL);
    nhbot_shutdown();
    puts("nhbot: exiting...");

    exit(0);

error:
    return -1;
}

// Start the NetHack bot with some options
static int nhbot_run(const char *nethack_path, const char *env_term,
              const char *env_nethackoptions)
{
    // TMT virtual term
    TMT *tmt_vt;

    // The pty for nethack
    struct PTY pty;

    // Our env consists of term and nethack options
    const char *env[] = {
        env_term,
        env_nethackoptions,
        NULL
    };

    // Handle sigint to kill nethack
    check(signal(SIGINT, handle_signal) != SIG_ERR);

    // Create the pty descriptors
    check(openpty(&pty.master, &pty.slave, NULL, NULL, NULL) != -1);

    // Create the TMT virtual term
    check((tmt_vt = tmt_open(VT_H, VT_W, nhbot_tmt_callback, &nethack_state, NULL)));

    // Initialize thread params
    struct io_thread_params io_thread_params = {
        .nethack_path = nethack_path,
        .env_term = env_term,
        .env_nethackoptions = env_nethackoptions,
        .pty = pty,
        .vt = tmt_vt,
        .nethack_state = &nethack_state,
    };

    // Fork! ==E
    check((pid = fork()) != -1);
    if (pid == 0) {
        // Child process
        check(fork_handle_child(&pty, nethack_path, env) != -1);
    } else if (pid > 0) {
        // Parent process
        check(fork_handle_parent(&io_thread_params) != -1);
    }

    return 0;

error:
    fprintf(stderr, "Exited due to error.");
    return -1;
}


// Start the NetHack bot with some options
static void run(void)
{
    nhbot_run("/usr/bin/nethack",
            "TERM=ansi",
            "NETHACKOPTIONS=time:true,splash_screen:no,"
            "role:Knight,race:human,gender:male,align:lawful");
}

int main(void)
{
    run();
    return 0;
}

