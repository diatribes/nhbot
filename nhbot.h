#ifndef _LIBNHW_H_
#define _LIBNHW_H_

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include "tmt.h"


#define iswall(c) ((c) == '-' || (c) == '|')
#define VT_W 80
#define VT_H 24

#define BUFLEN ((VT_W * VT_H) * 2)

#define check(x)\
if (!(x)) {\
    if (errno) {\
        flockfile(stderr);\
        fprintf(stderr, "%s:%d " , __FILE__, __LINE__);\
        perror(NULL);\
        funlockfile(stderr);\
    }\
    errno = 0;\
    goto error;\
}

typedef enum {
	Black = 1,
	Red,
	Green,
	Brown,
	Blue,
	Magenta,
	Cyan,
	Gray,
	None,
} NetHackColor;


typedef enum {
    CompassDirection_N = 0,
    CompassDirection_E,
    CompassDirection_S,
    CompassDirection_W,
    CompassDirection_NE,
    CompassDirection_SE,
    CompassDirection_SW,
    CompassDirection_NW,
    Command_DROP,
    Command_EAT,
    TextCharacters_SPACE,
    TextCharacters_DOLLAR,
    TextCharacters_y,
    TextCharacters_n,
    TextCharacters_q,

    NetHackActionEnum_Count,

} NetHackActionEnum;

struct PTY {
    int master;
    int slave;
};

typedef struct {
    uint32_t St;
    uint32_t Dx;
    uint32_t Co;
    uint32_t In;
    uint32_t Wi;
    uint32_t Ch;

    uint32_t Dlvl;
    uint32_t Money;
    uint32_t HP;
    uint32_t Pw;
    uint32_t Ac;
    uint32_t Xp;
    uint32_t T;
} NetHackBlStat;

typedef struct {
    NetHackActionEnum ActionId;
    uint8_t ActionChar;
} NetHackAction;

typedef struct {
    uint8_t ScreenChar[VT_W*VT_H];
    uint8_t ScreenColor[VT_W*VT_H];
    int CursorRow;
    int CursorCol;
    int PlayerRow;
    int PlayerCol;
    bool PromptMore;
    bool PromptYn;
    bool StatusHungry;
    bool StatusBurdened;
    NetHackAction Action;
    NetHackBlStat BlStat;
    int QMap5x5[5*5];
} NetHackState;

struct io_params {
    pid_t pid;
    struct PTY pty;
    TMT *vt;
    NetHackState *nethack_state;
    const char *nethack_path;
    const char *nethack_username;
    const char *env_term;
    const char *env_nethackoptions;
};

static NetHackAction NetHackActionLookup[] = {
    { CompassDirection_N, 107},
    { CompassDirection_E, 108},
    { CompassDirection_S, 106},
    { CompassDirection_W, 104},
    { CompassDirection_NE, 117},
    { CompassDirection_SE, 110},
    { CompassDirection_SW, 98},
    { CompassDirection_NW, 121},
    { Command_DROP, 'D'},
    { Command_EAT, 101},
    { TextCharacters_SPACE, 32},
    { TextCharacters_DOLLAR, 36},
    { TextCharacters_y, 'y'},
    { TextCharacters_n, 'n'},
    { TextCharacters_q, 'q'},
};

#endif
