/* Blockz background music (v0.21), included by blockz.c.
 * Owner's behaviour: if music is already playing when Blockz opens, leave it alone. Otherwise play a random
 * track from /_blockgame/ on the SD card and keep picking random tracks from that folder until Blockz exits,
 * then stop. No card / folder / file: start silently at once.
 *
 * Uses stock's file system and player the way the folder browser and Now Playing do (docs/HARDWARE.md,
 * "Starting music"). First attempt from static analysis: every step reports a status so a wrong guess shows
 * up as a plain label on screen (MUSIC panel) and as numbers on the INFO page, never as a silent failure. */

#define SYSC   0x03022894u      /* system config */
#define PREC   0x03022F14u      /* music source record */
#define PHAND  0x030232BCu      /* player thread handle */
#define PDESC  0x030361A4u      /* player thread descriptor (module 33) */
#define FILTER ((const void *)0x030056EBu)   /* "MP1MP2MP3WMA..." audio extension list */
#define FTYPE  6u               /* entry type the library scanner uses */

#define ThreadRunning(h, d)  STOCK_FN(0x03057C7C, uint32_t, uint32_t, uint32_t)(h, d)
#define ThreadDelete(hp)     STOCK_FN(0x03057C38, void, uint32_t)(hp)
#define ThreadCreate(hp, d, a) STOCK_FN(0x03057B96, void, uint32_t, uint32_t, void *)(hp, d, a)
#define ModuleLoad(n)        STOCK_FN(0x030684E4, uint32_t, uint32_t, uint32_t)(n, 7)
#define DiskSwitch(d)        STOCK_FN(0x03058526, void, uint32_t)(d)
#define FindFirst(f, it, e, t) STOCK_FN(0x0305BBF6, uint32_t, void *, void *, const void *, uint32_t)(f, it, e, t)
#define FindNext(f, it, e, t)  STOCK_FN(0x0305BBCA, uint32_t, void *, void *, const void *, uint32_t)(f, it, e, t)
#define ExName(x, i, t, o)   STOCK_FN(0x0305B2D0, uint32_t, void *, uint32_t, uint32_t, uint16_t *)(x, i, t, o)
#define FatName(c, i, t, o)  STOCK_FN(0x0305C364, uint32_t, uint32_t, uint32_t, uint32_t, uint16_t *)(c, i, t, o)
#define ExFiles(x, e, t)     STOCK_FN(0x0305BF7E, uint32_t, void *, const void *, uint32_t)(x, e, t)
#define ExDirs(x, t)         STOCK_FN(0x0305A79E, uint32_t, void *, uint32_t)(x, t)
#define FatFiles(c, e, t)    STOCK_FN(0x0305BDF6, uint32_t, uint32_t, const void *, uint32_t)(c, e, t)
#define FatDirs(c, t)        STOCK_FN(0x0305A6B4, uint32_t, uint32_t, uint32_t)(c, t)
#define RecPrepare()         STOCK_FN(0x03055304, void, void)()
#define AudioClose()         STOCK_FN(0x0308D86E, void, uint32_t)(1)
#define AudioControl(c, a)   STOCK_FN(0x0308BF84, uint32_t, uint32_t, uint32_t)(c, a)
#define FlashRead(o, n, b)   STOCK_FN(0x03068194, uint32_t, uint32_t, uint32_t, uint32_t, void *)(0x30001, o, n, b)
#define TotalFiles(f, m)     STOCK_FN(0x0305ADA8, uint32_t, const void *, uint32_t)(f, m)
#define MAX_TRACKS 64

enum { BGM_OFF, BGM_KEPT, BGM_NOCARD, BGM_NOFOLDER, BGM_NOFILE, BGM_PLAY, BGM_ERRFIND, BGM_ERRSTART };

typedef struct {                 /* the browser's iterator (BrowserUI listing, sp+0 .. sp+0x47) */
    uint32_t clus, index, count;
    uint8_t  pad[0x10];
    uint8_t  ex[0x2C];           /* exFAT directory info, 0x2A bytes used */
} fs_iter_t;

static struct {
    uint8_t  status, mine;       /* mine: we started the player and own it until exit */
    uint8_t  fstype;
    uint32_t root, clus_fdt, clus_ex, clus, files, cur, seen_clus, seen_num, rnd;
    uint8_t  from_db;            /* file numbers came from the library database */
    uint16_t nums[MAX_TRACKS];   /* file numbers (1-based) of the folder's tracks, when from_db */
    uint8_t  ex[0x2C];           /* /_blockgame/'s directory info (exFAT) */
} M;

static uint32_t m_rd32(uint32_t a) { volatile uint8_t *p = (volatile uint8_t *)a; return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int name_is_blockgame(const uint16_t *s)
{
    static const char want[] = "_blockgame";
    for (int i = 0; i < 10; i++) {
        uint16_t c = s[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != (uint8_t)want[i]) return 0;
    }
    return s[10] == 0;
}

static uint32_t m_random(uint32_t n)
{
    M.rnd = M.rnd * 1664525u + 1013904223u;
    return n ? (M.rnd >> 8) % n : 0;
}

/* Library database (what Music uses): 2 KB records from sector sys+0x79 on the internal flash volume, record
 * +0 = folder cluster, +4 = file number. Collect the tracks whose folder is /_blockgame/. */
static void db_collect(void)
{
    uint32_t base = m_rd32(SYSC + 0x79), total = TotalFiles(FILTER, 3), rec[2];
    if (total > 20000) total = 20000;
    for (uint32_t r = 0; r < total && M.files < MAX_TRACKS; r++) {
        FlashRead((base << 9) + (r << 11), 8, rec);
        if (rec[0] == 0xFFFFFFFFu) break;
        if (rec[1] && (rec[0] == M.clus_ex || rec[0] == M.clus_fdt)) {
            M.clus = rec[0];
            M.nums[M.files++] = (uint16_t)rec[1];
        }
    }
    M.from_db = M.files != 0;
}

/* find /_blockgame/ in the root of the current disk; fills M.clus, M.ex, M.files */
static int find_folder(void)
{
    static fs_iter_t it;
    static uint8_t fdt[0x40];
    static uint16_t name[0x104];
    M.fstype = *(volatile uint8_t *)0x0307E710;
    M.root = m_rd32(0x0307E71C + 0x0D);
    for (unsigned i = 0; i < sizeof it; i++) ((uint8_t *)&it)[i] = 0;
    it.clus = M.root;
    if (M.fstype == 3) {                        /* root's exFAT info, as the browser seeds its root entry */
        it.ex[0] = (uint8_t)M.root; it.ex[1] = (uint8_t)(M.root >> 8);
        it.ex[2] = (uint8_t)(M.root >> 16); it.ex[3] = (uint8_t)(M.root >> 24);
        it.ex[0x0C] = 1;
        it.count = ExFiles(it.ex, FILTER, FTYPE) + ExDirs(it.ex, FTYPE);
    } else {
        it.count = FatFiles(M.root, FILTER, FTYPE) + FatDirs(M.root, FTYPE);
    }
    if (FindFirst(fdt, &it, FILTER, FTYPE) != 0) return BGM_ERRFIND;
    for (int n = 0; n < 4000; n++) {
        if (fdt[0x0B] & 0x10) {                 /* a folder: compare its long name */
            name[0] = 0;
            if (M.fstype == 3) ExName(it.ex, it.index - 1, FTYPE, name);
            else FatName(it.clus, it.index - 1, FTYPE, name);
            name[0x100] = 0;
            if (name_is_blockgame(name)) {
                M.clus_fdt = (uint32_t)(fdt[0x14] | fdt[0x15] << 8) << 16 | fdt[0x1A] | fdt[0x1B] << 8;
                M.clus_ex = it.ex[0] | it.ex[1] << 8 | it.ex[2] << 16 | (uint32_t)it.ex[3] << 24;
                for (int i = 0; i < 0x2C; i++) M.ex[i] = it.ex[i];
                M.clus = M.fstype == 3 ? M.clus_ex : M.clus_fdt;
                M.files = M.fstype == 3 ? ExFiles(M.ex, FILTER, FTYPE) : FatFiles(M.clus, FILTER, FTYPE);
                if (!M.files) db_collect();     /* stock's folder count can return 0 (owner's folder) */
                return M.files ? BGM_PLAY : BGM_NOFILE;
            }
        }
        if (FindNext(fdt, &it, FILTER, FTYPE) != 0) break;
    }
    return BGM_NOFOLDER;
}

static void point_record(uint32_t num)
{
    *(volatile uint16_t *)(PREC + 0xF2) = 0x63;  /* source as seen when playing from Folders */
    *(volatile uint32_t *)(PREC + 0x34) = M.clus;
    *(volatile uint32_t *)(PREC + 0x38) = num;
    M.cur = num; M.seen_clus = M.clus; M.seen_num = num;
}

static uint32_t pick_track(void)
{
    uint32_t i = m_random(M.files);
    uint32_t n = M.from_db ? M.nums[i] : i + 1;
    if (M.files > 1 && n == M.cur) {                       /* no immediate repeat */
        i = (i + 1) % M.files;
        n = M.from_db ? M.nums[i] : i + 1;
    }
    return n;
}

/* on Blockz open */
static void bgm_start(uint32_t seed)
{
    for (unsigned i = 0; i < sizeof M; i++) ((uint8_t *)&M)[i] = 0;
    M.rnd = seed | 1;
    if (ThreadRunning(m_rd32(PHAND), PDESC) == 1) { M.status = BGM_KEPT; return; }
    if (!*(volatile uint8_t *)(SYSC + 0x1F)) { M.status = BGM_NOCARD; return; }
    if (*(volatile uint8_t *)(SYSC + 0x0E) != 4) {   /* SD card is disk 4 */
        *(volatile uint8_t *)(SYSC + 0x0E) = 4;
        DiskSwitch(4);
    }
    M.status = (uint8_t)find_folder();
    if (M.status != BGM_PLAY) return;
    RecPrepare();                                  /* browser's play-request prep, then our file over it */
    point_record(pick_track());
    static uint16_t arg[2];
    arg[0] = 1; arg[1] = 0x46;
    ThreadDelete(PHAND);
    ModuleLoad(33);
    ThreadCreate(PHAND, PDESC, arg);
    if (ThreadRunning(m_rd32(PHAND), PDESC) != 1) { M.status = BGM_ERRSTART; return; }
    M.mine = 1;
}

/* every service call while Blockz runs: keep the player inside /_blockgame/ */
static void bgm_poll(void)
{
    if (!M.mine) return;
    if (ThreadRunning(m_rd32(PHAND), PDESC) != 1) { M.mine = 0; M.status = BGM_ERRSTART; return; }
    uint32_t c = *(volatile uint32_t *)(PREC + 0x34), n = *(volatile uint32_t *)(PREC + 0x38);
    if (c == M.seen_clus && n == M.seen_num) return;
    /* the player moved on by itself (track ended): next random track from the folder */
    AudioClose();
    point_record(pick_track());
    AudioControl(0xE5, 0);
}

/* on Blockz exit: stop what we started */
static void bgm_stop(void)
{
    if (M.mine) ThreadDelete(PHAND);
    M.mine = 0;
}

static const char *bgm_label(void)
{
    switch (M.status) {
    case BGM_KEPT:     return "ON";
    case BGM_NOCARD:   return "NO SD";
    case BGM_NOFOLDER: return "NO DIR";
    case BGM_NOFILE:   return "EMPTY";
    case BGM_PLAY:     return "PLAY";
    case BGM_ERRFIND:  return "ERR 1";
    case BGM_ERRSTART: return "ERR 2";
    default:           return "OFF";
    }
}
