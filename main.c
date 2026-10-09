/*
 * PSP Clarity  -  kernel plugin for PRO CFW (PSP-2000/3000)
 *
 *  - Sharpen filter (3x3 cross Laplacian / unsharp mask)
 *  - Pixel-overdrive to compensate LCD ghosting
 *  - On/Off + live tuning with the volume buttons, small on-screen display
 *
 *  Controls
 *    VOL+ and VOL- together ........ plugin ON / OFF
 *    SELECT held + VOL+ / VOL- ..... sharpness  +1 / -1   (0..8)
 *    START  held + VOL+ / VOL- ..... overdrive  +1 / -1   (0..8)
 *
 *  Settings are saved to ms0:/seplugins/psp_clarity.ini
 */

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspsdk.h>
#include <pspdebug.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("PSPClarity", PSP_MODULE_KERNEL, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

#define SCR_W            480
#define SCR_H            272
#define ROW_BYTES        (SCR_W * 3)
#define CFG_PATH         "ms0:/seplugins/psp_clarity.ini"
#define NID_SETFRAMEBUF  0x289D82FE      /* sceDisplaySetFrameBuf */
#define OSD_DURATION     180             /* frames (~3 s) */
#define LEVEL_MAX        8

/* Provided by systemctrl (PRO / ARK) */
u32  sctrlHENFindFunction(const char *modname, const char *libname, u32 nid);
void sctrlHENPatchSyscall(void *addr, void *newaddr);

typedef int (*SetFrameBufFn)(void *topaddr, int bufferwidth, int pixelformat, int sync);
static SetFrameBufFn orig_SetFrameBuf = NULL;

static struct {
    int enabled;
    int sharp;      /* 0..8 */
    int od;         /* 0..8 */
} cfg = { 1, 3, 3 };

static u16 *prev_frame = NULL;      /* previous frame, RGB565 */
static int  prev_valid = 0;
static int  osd_frames = 0;
static u32  last_buttons = 0;

static u8 buf_a[ROW_BYTES], buf_b[ROW_BYTES], buf_c[ROW_BYTES], out_row[ROW_BYTES];

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static inline int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static inline int clampl(int v) { return v < 0 ? 0 : (v > LEVEL_MAX ? LEVEL_MAX : v); }

static inline u16 pack565(int r, int g, int b)
{
    return (u16)((r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11));
}

static inline void unpack565(u16 c, int *r, int *g, int *b)
{
    int rr = c & 0x1F, gg = (c >> 5) & 0x3F, bb = (c >> 11) & 0x1F;
    *r = (rr << 3) | (rr >> 2);
    *g = (gg << 2) | (gg >> 4);
    *b = (bb << 3) | (bb >> 2);
}

static void load_row(u8 *dst, const void *vram, int bw, int pf, int y)
{
    int x;
    if (pf == PSP_DISPLAY_PIXEL_FORMAT_8888) {
        const u32 *s = (const u32 *)vram + y * bw;
        for (x = 0; x < SCR_W; x++) {
            u32 c = s[x];
            dst[0] = c & 0xFF;
            dst[1] = (c >> 8) & 0xFF;
            dst[2] = (c >> 16) & 0xFF;
            dst += 3;
        }
    } else {
        const u16 *s = (const u16 *)vram + y * bw;
        for (x = 0; x < SCR_W; x++) {
            int r, g, b;
            unpack565(s[x], &r, &g, &b);
            dst[0] = r; dst[1] = g; dst[2] = b;
            dst += 3;
        }
    }
}

static void store_row(void *vram, int bw, int pf, int y, const u8 *src)
{
    int x;
    if (pf == PSP_DISPLAY_PIXEL_FORMAT_8888) {
        u32 *d = (u32 *)vram + y * bw;
        for (x = 0; x < SCR_W; x++) {
            d[x] = 0xFF000000u | src[0] | (src[1] << 8) | (src[2] << 16);
            src += 3;
        }
    } else {
        u16 *d = (u16 *)vram + y * bw;
        for (x = 0; x < SCR_W; x++) {
            d[x] = pack565(src[0], src[1], src[2]);
            src += 3;
        }
    }
}

/* ------------------------------------------------------------------ */
/* image processing: sharpen + overdrive in one pass                   */
/* ------------------------------------------------------------------ */

static void process_frame(void *vram, int bw, int pf)
{
    u8 *up = buf_a, *cur = buf_b, *dn = buf_c, *t;
    int od = prev_valid ? cfg.od : 0;
    int y, x, c;

    load_row(cur, vram, bw, pf, 0);
    memcpy(up, cur, ROW_BYTES);
    load_row(dn, vram, bw, pf, 1);

    for (y = 0; y < SCR_H; y++) {
        u16 *pp = prev_frame + y * SCR_W;

        for (x = 0; x < SCR_W; x++) {
            int xl = (x > 0) ? x - 1 : 0;
            int xr = (x < SCR_W - 1) ? x + 1 : x;
            int pv[3], qv[3];
            u16 pk = pack565(cur[x * 3], cur[x * 3 + 1], cur[x * 3 + 2]);

            unpack565(pp[x], &pv[0], &pv[1], &pv[2]);   /* previous frame  */
            unpack565(pk,    &qv[0], &qv[1], &qv[2]);   /* current (quant) */

            for (c = 0; c < 3; c++) {
                int v   = cur[x * 3 + c];
                int lap = 4 * v - up[x * 3 + c] - dn[x * 3 + c]
                               - cur[xl * 3 + c] - cur[xr * 3 + c];
                int o   = v + (lap * cfg.sharp) / 32;       /* sharpen   */
                o      += ((qv[c] - pv[c]) * od) / 16;      /* overdrive */
                out_row[x * 3 + c] = clamp8(o);
            }
            pp[x] = pk;     /* remember ORIGINAL frame for next time */
        }

        store_row(vram, bw, pf, y, out_row);

        t = up; up = cur; cur = dn; dn = t;
        if (y + 2 < SCR_H) load_row(dn, vram, bw, pf, y + 2);
        else               memcpy(dn, cur, ROW_BYTES);
    }
    prev_valid = 1;
}

/* ------------------------------------------------------------------ */
/* config                                                              */
/* ------------------------------------------------------------------ */

static void save_cfg(void)
{
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "enabled=%d\nsharp=%d\nod=%d\n",
                       cfg.enabled, cfg.sharp, cfg.od);
    SceUID fd = sceIoOpen(CFG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) { sceIoWrite(fd, buf, len); sceIoClose(fd); }
}

static void load_cfg(void)
{
    char buf[64];
    int e, s, o;
    SceUID fd = sceIoOpen(CFG_PATH, PSP_O_RDONLY, 0);
    if (fd < 0) return;
    memset(buf, 0, sizeof(buf));
    sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (sscanf(buf, "enabled=%d\nsharp=%d\nod=%d", &e, &s, &o) == 3) {
        cfg.enabled = e ? 1 : 0;
        cfg.sharp   = clampl(s);
        cfg.od      = clampl(o);
    }
}

/* ------------------------------------------------------------------ */
/* input + OSD                                                         */
/* ------------------------------------------------------------------ */

static void handle_input(void)
{
    SceCtrlData pad;
    u32 b, pressed;
    int d = 0, changed = 0;

    memset(&pad, 0, sizeof(pad));
    if (sceCtrlPeekBufferPositive(&pad, 1) <= 0) return;

    b = pad.Buttons;
    pressed = b & ~last_buttons;
    last_buttons = b;

    /* VOL+ & VOL- together -> toggle */
    if ((b & PSP_CTRL_VOLUP) && (b & PSP_CTRL_VOLDOWN)) {
        if (pressed & (PSP_CTRL_VOLUP | PSP_CTRL_VOLDOWN)) {
            cfg.enabled = !cfg.enabled;
            prev_valid = 0;
            changed = 1;
        }
    } else {
        if (pressed & PSP_CTRL_VOLUP)        d = +1;
        else if (pressed & PSP_CTRL_VOLDOWN) d = -1;

        if (d) {
            if (b & PSP_CTRL_SELECT)      { cfg.sharp = clampl(cfg.sharp + d); changed = 1; }
            else if (b & PSP_CTRL_START)  { cfg.od    = clampl(cfg.od + d);    changed = 1; }
        }
    }

    if (changed) {
        osd_frames = OSD_DURATION;
        save_cfg();
    }
}

static void osd_puts(int x, int y, const char *s)
{
    for (; *s; s++, x += 8)
        pspDebugScreenPutChar(x, y, 0xFFFFFFFF, (u8)*s);
}

static void draw_osd(void *vram, int pf)
{
    char line[64];

    pspDebugScreenInitEx(vram, pf, 0);          /* no mode change */
    pspDebugScreenSetBackColor(0xFF000000);
    pspDebugScreenEnableBackColor(1);

    snprintf(line, sizeof(line), " PSP Clarity: %s ", cfg.enabled ? "ON " : "OFF");
    osd_puts(4, 4, line);
    snprintf(line, sizeof(line), " Sharp:%d/%d  Overdrive:%d/%d ",
             cfg.sharp, LEVEL_MAX, cfg.od, LEVEL_MAX);
    osd_puts(4, 14, line);
    osd_puts(4, 24, " VOL+&-:on/off SEL+VOL:sharp START+VOL:od ");
}

/* ------------------------------------------------------------------ */
/* the hook                                                            */
/* ------------------------------------------------------------------ */

static int hook_SetFrameBuf(void *topaddr, int bufferwidth, int pixelformat, int sync)
{
    u32 k1 = pspSdkSetK1(0);

    handle_input();

    if (topaddr && bufferwidth >= SCR_W &&
        (pixelformat == PSP_DISPLAY_PIXEL_FORMAT_565 ||
         pixelformat == PSP_DISPLAY_PIXEL_FORMAT_8888))
    {
        u32 a = (u32)topaddr & 0x07FFFFFF;
        if (a >= 0x04000000 && a < 0x04200000) {
            void *vram = (void *)(a | 0x40000000);      /* uncached VRAM */

            if (cfg.enabled)
                process_frame(vram, bufferwidth, pixelformat);

            if (osd_frames > 0) {
                draw_osd(vram, pixelformat);
                osd_frames--;
            }
        }
    }

    pspSdkSetK1(k1);
    return orig_SetFrameBuf(topaddr, bufferwidth, pixelformat, sync);
}

/* ------------------------------------------------------------------ */
/* module entry                                                        */
/* ------------------------------------------------------------------ */

int module_start(SceSize args, void *argp)
{
    u32 addr;
    SceUID blk = sceKernelAllocPartitionMemory(PSP_MEMORY_PARTITION_KERNEL,
                    "clarity_prev", PSP_SMEM_Low, SCR_W * SCR_H * sizeof(u16), NULL);
    if (blk < 0) return 1;

    prev_frame = (u16 *)sceKernelGetBlockHeadAddr(blk);
    memset(prev_frame, 0, SCR_W * SCR_H * sizeof(u16));

    load_cfg();

    addr = sctrlHENFindFunction("sceDisplay_Service", "sceDisplay", NID_SETFRAMEBUF);
    if (!addr) return 1;

    orig_SetFrameBuf = (SetFrameBufFn)addr;
    sctrlHENPatchSyscall((void *)addr, (void *)hook_SetFrameBuf);

    sceKernelDcacheWritebackAll();
    sceKernelIcacheClearAll();

    osd_frames = OSD_DURATION;      /* show status once at start */
    return 0;
}
