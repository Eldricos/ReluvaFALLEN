/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Reluva contributors
 *
 * A stand-in for a game's vm/qagame.qvm, for testing the engine side of
 * the game logic's system calls (src/engines/idtech3/eliteforce/game_host.cpp)
 * with code the Quake III compiler generates, in Elite Force's numbering
 * (fake_game.asm) and with its entities' layout. Init reads the level's
 * entities the way the original game code does, token by token; brush
 * models get SetBrushModel, the rest a 32-unit box; all but the world
 * are linked. build.sh compiles it into fake_game_qvm.h. vmMain must be
 * the first function. */

typedef struct {
    int allsolid, startsolid;
    float fraction;
    float endpos[3];
    float normal[3];
    float dist;
    char type, signbits, pad[2];
    int surfaceFlags, contents, entityNum;
} trace_t; /* 56 bytes */

typedef struct {
    int serverTime;
    unsigned char buttons, weapon;
    int angles[3];
    signed char forwardmove, rightmove, upmove;
} usercmd_t; /* 24 bytes */

typedef struct {
    /* the entity state: 204 bytes in Elite Force */
    int number;
    char state1[156];
    int modelindex; /* at 160 */
    char state2[40];
    /* the part the engine shares, from 204: one field fewer than Quake
     * III's before the box (which one isn't known) */
    int linked, unknown1, unknown2, bmodel;
    float mins[3], maxs[3];
    int contents;
    float absmin[3], absmax[3];
    float currentOrigin[3], currentAngles[3];
    int ownerNum;
    /* the game's own */
    int inUse;
    char classname[32];
    char model[16];
} entity_t; /* 352 bytes */

void trap_Print(const char *text);
void trap_Error(const char *text);
int trap_Cvar_VariableIntegerValue(const char *name);
void trap_Cvar_VariableStringBuffer(const char *name, char *buffer, int size);
void trap_LocateGameData(entity_t *entities, int count, int entitySize, void *clients, int clientSize);
void trap_SetConfigstring(int index, const char *text);
void trap_GetConfigstring(int index, char *buffer, int size);
void trap_SetBrushModel(entity_t *ent, const char *name);
void trap_Trace(trace_t *results, const float *start, const float *mins, const float *maxs, const float *end,
                int passEntityNum, int contentMask);
int trap_PointContents(const float *point, int passEntityNum);
void trap_LinkEntity(entity_t *ent);
void trap_UnlinkEntity(entity_t *ent);
int trap_EntitiesInBox(const float *mins, const float *maxs, int *list, int maxCount);
int trap_GetEntityToken(char *buffer, int size);
void trap_GetUserinfo(int clientNum, char *buffer, int size);
void trap_SendServerCommand(int clientNum, const char *text);
void trap_GetUsercmd(int clientNum, usercmd_t *cmd);
float ceil(float f);

static int same(const char *a, const char *b);
static void copy(char *to, const char *from, int size);
static void readVector(const char *text, float *v);
static int spawnOne(void);

static entity_t entities[64];
static int clients[8][185]; /* Elite Force's gclient_t is 740 bytes, the player state first */
static int numEntities;
static int frames;
static int spawnPoint;
static char token[64];
static char key[64];
static char text[64];
static trace_t tr;
static char userinfo[256];
static int player = -1; /* the joined player's entity */
static usercmd_t ucmd;

int vmMain(int command, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8,
           int arg9, int arg10, int arg11) {
    int r, list[16];
    float start[3], end[3], mins[3], maxs[3];
    switch (command) {
    case 0: /* init(levelTime, randomSeed, restart) */
        trap_Print("------- Game Initialization -------\n");
        trap_Cvar_VariableStringBuffer("mapname", text, sizeof(text));
        trap_SetConfigstring(12, text);
        trap_LocateGameData(entities, 0, sizeof(entity_t), clients, sizeof(clients[0]));
        while ((r = spawnOne()) == 1)
            ;
        if (r < 0)
            trap_Error("broken entity text");
        if (!same(entities[0].classname, "worldspawn"))
            trap_Error("the first entity isn't worldspawn");
        trap_UnlinkEntity(&entities[0]); /* the world isn't linked */
        return numEntities;
    case 2: /* client connect(clientNum, firstTime, isBot): refused without a name */
        trap_GetUserinfo(arg0, userinfo, sizeof(userinfo));
        if (!userinfo[0] || userinfo[1] != 'n')
            return (int)"No name given";
        return 0;
    case 3: /* client begin(clientNum): at the spawn point, announced */
        player = numEntities++;
        trap_LocateGameData(entities, numEntities, sizeof(entity_t), clients, sizeof(clients[0]));
        entities[player].number = player;
        copy(entities[player].classname, "player", sizeof(entities[player].classname));
        entities[player].currentOrigin[0] = entities[spawnPoint].currentOrigin[0];
        entities[player].currentOrigin[1] = entities[spawnPoint].currentOrigin[1];
        entities[player].currentOrigin[2] = entities[spawnPoint].currentOrigin[2] + 9;
        entities[player].contents = 0x2000000;
        trap_LinkEntity(&entities[player]);
        trap_SendServerCommand(-1, "print \"Reluva entered the game\n\"");
        return 0;
    case 31: return player;
    case 7: /* client think(clientNum): its command moves it along x */
        trap_GetUsercmd(arg0, &ucmd);
        if (player >= 0)
            entities[player].currentOrigin[0] += ucmd.forwardmove;
        return 0;
    case 32: return ucmd.serverTime * 10 + ucmd.buttons;
    case 33: return ucmd.angles[0] + ucmd.angles[1] * 100000;
    case 34: return ucmd.weapon * 1000000 + (ucmd.forwardmove + 128) * 10000 + (ucmd.rightmove + 128) * 100 + ucmd.upmove + 50;
    case 8: /* run a frame(levelTime) */
        frames++;
        return frames;
    case 20: return numEntities;
    case 21: /* an entity's classname, back through a configstring */
        trap_SetConfigstring(300 + arg0, entities[arg0].classname);
        trap_GetConfigstring(300 + arg0, text, sizeof(text));
        return text[0] * 1000 + text[1];
    case 22: return trap_Cvar_VariableIntegerValue("sv_maxclients") * 100 + (int)ceil(2.25f);
    case 23: return frames;
    case 24: /* trace straight down 1000 from (arg0, arg1, arg2), a point or (arg3: half-size) a box: what's hit */
        start[0] = end[0] = arg0;
        start[1] = end[1] = arg1;
        start[2] = arg2;
        end[2] = arg2 - 1000;
        mins[0] = mins[1] = mins[2] = -arg3;
        maxs[0] = maxs[1] = maxs[2] = arg3;
        trap_Trace(&tr, start, arg3 ? mins : 0, arg3 ? maxs : 0, end, arg4, -1);
        return tr.entityNum;
    case 25: return (int)(tr.endpos[2] * 8); /* where the last trace stopped, in eighths */
    case 26: return tr.allsolid * 10 + tr.startsolid * 100 + (tr.fraction == 1) + (int)tr.normal[2] * 1000;
    case 27: return (int)entities[arg0].absmin[2] * 1000 + (int)entities[arg0].absmax[2];
    case 28:
        start[0] = arg0;
        start[1] = arg1;
        start[2] = arg2;
        return trap_PointContents(start, -1);
    case 29: /* entities near a point */
        mins[0] = arg0 - 1;
        mins[1] = arg1 - 1;
        mins[2] = arg2 - 1;
        maxs[0] = arg0 + 1;
        maxs[1] = arg1 + 1;
        maxs[2] = arg2 + 1;
        r = trap_EntitiesInBox(mins, maxs, list, 16);
        return r ? r * 100 + list[0] : 0;
    case 30: return entities[arg0].modelindex * 100 + entities[arg0].bmodel * 10 + entities[arg0].linked;
    }
    return -1;
}

static int same(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void copy(char *to, const char *from, int size) {
    int i;
    for (i = 0; i < size - 1 && from[i]; i++)
        to[i] = from[i];
    to[i] = 0;
}

/* "x y z", whole numbers */
static void readVector(const char *s, float *v) {
    int i, n, negative;
    for (i = 0; i < 3; i++) {
        while (*s == ' ')
            s++;
        negative = *s == '-';
        if (negative)
            s++;
        n = 0;
        while (*s >= '0' && *s <= '9')
            n = n * 10 + *s++ - '0';
        v[i] = negative ? -n : n;
    }
}

/* One { key value ... } block: 1 if there was one, 0 at the end, -1 if broken. */
static int spawnOne(void) {
    entity_t *ent;
    int i;
    if (!trap_GetEntityToken(token, sizeof(token)))
        return 0;
    if (!same(token, "{"))
        return -1;
    ent = &entities[numEntities];
    ent->number = numEntities;
    ent->inUse = 1;
    ent->classname[0] = 0;
    ent->model[0] = 0;
    while (1) {
        if (!trap_GetEntityToken(key, sizeof(key)))
            return -1;
        if (same(key, "}"))
            break;
        if (!trap_GetEntityToken(token, sizeof(token)))
            return -1;
        if (same(key, "classname"))
            copy(ent->classname, token, sizeof(ent->classname));
        else if (same(key, "model"))
            copy(ent->model, token, sizeof(ent->model));
        else if (same(key, "origin"))
            readVector(token, ent->currentOrigin);
    }
    if (ent->classname[0] == 'i' && ent->classname[1] == 'n')
        spawnPoint = numEntities; /* info_player_... */
    numEntities++;
    trap_LocateGameData(entities, numEntities, sizeof(entity_t), clients, sizeof(clients[0]));
    if (ent->model[0] == '*') {
        trap_SetBrushModel(ent, ent->model); /* links it */
    } else {
        for (i = 0; i < 3; i++) {
            ent->mins[i] = -16;
            ent->maxs[i] = 16;
        }
        ent->contents = 0x2000000; /* a body */
        trap_LinkEntity(ent);
    }
    return 1;
}
