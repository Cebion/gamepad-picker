#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "fonts/portmsg_bold.h"
#include "fonts/portmsg_regular.h"

#define MAX_CP 0x2200
#define AXIS_THRESHOLD 16000
#define REPRESENT_MS 100

typedef struct {
    SDL_Texture *tex;
    int w, h, xoff, yoff;
    float adv;
} Glyph;

typedef struct {
    stbtt_fontinfo *info;
    float px, scale;
    float ascent, line;
    Glyph *cache[MAX_CP];
} Face;

typedef struct {
    const char *s;
    int len;
} Line;

typedef struct {
    Line *v;
    int n, cap;
} Lines;

typedef struct {
    Uint8 r, g, b;
} Rgb;

static const Rgb COL_BG_TOP = {0x13, 0x16, 0x1C};
static const Rgb COL_BG_BOT = {0x0B, 0x0C, 0x10};
static const Rgb COL_CARD = {0x17, 0x1A, 0x21};
static const Rgb COL_BORDER = {0x29, 0x2E, 0x3A};
static const Rgb COL_TITLE = {0xF2, 0xF4, 0xF8};
static const Rgb COL_BODY = {0xB4, 0xBA, 0xC6};
static const Rgb COL_MUTED = {0x76, 0x7D, 0x8C};
static const Rgb COL_KEY = {0x2A, 0x2F, 0x3B};
static const Rgb COL_ACCENT = {0x5B, 0x9C, 0xF8};

static SDL_Renderer *R;
static stbtt_fontinfo font_regular, font_bold;

/* ---------- text ---------- */

static int utf8_next(const char **p, const char *end) {
    const unsigned char *s = (const unsigned char *)*p;
    int cp, n;
    if (s[0] < 0x80) {
        cp = s[0];
        n = 1;
    } else if ((s[0] & 0xE0) == 0xC0 && end - *p >= 2) {
        cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        n = 2;
    } else if ((s[0] & 0xF0) == 0xE0 && end - *p >= 3) {
        cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        n = 3;
    } else if ((s[0] & 0xF8) == 0xF0 && end - *p >= 4) {
        cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        n = 4;
    } else {
        cp = '?';
        n = 1;
    }
    *p += n;
    return cp;
}

static void face_init(Face *f, stbtt_fontinfo *info, float px) {
    int asc, desc, gap;
    memset(f, 0, sizeof(*f));
    f->info = info;
    f->px = px;
    f->scale = stbtt_ScaleForPixelHeight(info, px);
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    f->ascent = asc * f->scale;
    f->line = (asc - desc) * f->scale;
}

static void face_free(Face *f) {
    for (int i = 0; i < MAX_CP; i++) {
        if (f->cache[i]) {
            if (f->cache[i]->tex) {
                SDL_DestroyTexture(f->cache[i]->tex);
            }
            free(f->cache[i]);
        }
    }
}

static int face_cp(Face *f, int cp) {
    if (cp >= MAX_CP || !stbtt_FindGlyphIndex(f->info, cp)) {
        return '?';
    }
    return cp;
}

static Glyph *face_glyph(Face *f, int cp) {
    cp = face_cp(f, cp);
    if (f->cache[cp]) {
        return f->cache[cp];
    }
    Glyph *g = calloc(1, sizeof(Glyph));
    int adv, lsb;
    stbtt_GetCodepointHMetrics(f->info, cp, &adv, &lsb);
    g->adv = adv * f->scale;
    unsigned char *bmp = stbtt_GetCodepointBitmap(f->info, f->scale, f->scale, cp, &g->w, &g->h, &g->xoff, &g->yoff);
    if (bmp && g->w > 0 && g->h > 0) {
        Uint32 *px = malloc((size_t)g->w * g->h * 4);
        for (int i = 0; i < g->w * g->h; i++) {
            px[i] = ((Uint32)bmp[i] << 24) | 0xFFFFFF;
        }
        g->tex = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, g->w, g->h);
        SDL_UpdateTexture(g->tex, NULL, px, g->w * 4);
        SDL_SetTextureBlendMode(g->tex, SDL_BLENDMODE_BLEND);
        free(px);
    }
    stbtt_FreeBitmap(bmp, NULL);
    f->cache[cp] = g;
    return g;
}

static float text_width(Face *f, const char *s, int len) {
    const char *p = s, *end = s + len;
    float w = 0;
    int prev = 0;
    while (p < end) {
        int cp = face_cp(f, utf8_next(&p, end));
        if (prev) {
            w += stbtt_GetCodepointKernAdvance(f->info, prev, cp) * f->scale;
        }
        w += face_glyph(f, cp)->adv;
        prev = cp;
    }
    return w;
}

static void draw_text(Face *f, const char *s, int len, float x, float top, Rgb c, Uint8 alpha) {
    const char *p = s, *end = s + len;
    float base = top + f->ascent;
    int prev = 0;
    while (p < end) {
        int cp = face_cp(f, utf8_next(&p, end));
        if (prev) {
            x += stbtt_GetCodepointKernAdvance(f->info, prev, cp) * f->scale;
        }
        Glyph *g = face_glyph(f, cp);
        if (g->tex) {
            SDL_Rect dst = {(int)lroundf(x) + g->xoff, (int)lroundf(base) + g->yoff, g->w, g->h};
            SDL_SetTextureColorMod(g->tex, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(g->tex, alpha);
            SDL_RenderCopy(R, g->tex, NULL, &dst);
        }
        x += g->adv;
        prev = cp;
    }
}

static void lines_push(Lines *l, const char *s, int len) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = realloc(l->v, l->cap * sizeof(Line));
    }
    while (len > 0 && s[len - 1] == ' ') {
        len--;
    }
    l->v[l->n].s = s;
    l->v[l->n].len = len;
    l->n++;
}

/* Greedy word wrap. Paragraphs split on '\n'; overlong words break per character. */
static void wrap(Face *f, const char *text, float maxw, Lines *out) {
    out->n = 0;
    const char *para = text;
    for (;;) {
        const char *pend = strchr(para, '\n');
        if (!pend) {
            pend = para + strlen(para);
        }
        const char *ls = para;
        while (ls < pend && *ls == ' ') {
            ls++;
        }
        if (ls == pend) {
            lines_push(out, ls, 0);
        }
        while (ls < pend) {
            const char *p = ls, *last_fit = NULL;
            while (p < pend) {
                const char *we = p;
                while (we < pend && *we != ' ') {
                    we++;
                }
                if (text_width(f, ls, (int)(we - ls)) <= maxw) {
                    last_fit = we;
                    p = we;
                    while (p < pend && *p == ' ') {
                        p++;
                    }
                } else {
                    break;
                }
            }
            if (!last_fit) {
                const char *q = ls, *fit = NULL;
                while (q < pend) {
                    const char *n = q;
                    utf8_next(&n, pend);
                    if (fit && text_width(f, ls, (int)(n - ls)) > maxw) {
                        break;
                    }
                    fit = n;
                    q = n;
                }
                last_fit = fit;
                p = fit;
            }
            lines_push(out, ls, (int)(last_fit - ls));
            ls = p;
            while (ls < pend && *ls == ' ') {
                ls++;
            }
        }
        if (!*pend) {
            break;
        }
        para = pend + 1;
    }
}

/* ---------- shapes ---------- */

/* Anti-aliased rounded rectangle mask (white, coverage in alpha). feather > 1 gives a soft shadow. */
static SDL_Texture *make_rrect(int w, int h, float rad, float feather) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    Uint32 *px = malloc((size_t)w * h * 4);
    float hx = w * 0.5f, hy = h * 0.5f;
    float in = feather > 1 ? feather : 0;
    if (rad > hx - in) rad = hx - in;
    if (rad > hy - in) rad = hy - in;
    if (rad < 0) rad = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float qx = fabsf(x + 0.5f - hx) - (hx - in - rad);
            float qy = fabsf(y + 0.5f - hy) - (hy - in - rad);
            float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
            float d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - rad;
            float a;
            if (feather > 1) {
                float t = 1.0f - (d + feather) / (2 * feather);
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                a = t * t * (3 - 2 * t);
            } else {
                a = 0.5f - d;
                a = a < 0 ? 0 : a > 1 ? 1 : a;
            }
            px[y * w + x] = ((Uint32)(a * 255 + 0.5f) << 24) | 0xFFFFFF;
        }
    }
    SDL_Texture *t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_UpdateTexture(t, NULL, px, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    free(px);
    return t;
}

static void blit(SDL_Texture *t, int x, int y, Rgb c, Uint8 alpha) {
    int w, h;
    SDL_QueryTexture(t, NULL, NULL, &w, &h);
    SDL_Rect dst = {x, y, w, h};
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_RenderCopy(R, t, NULL, &dst);
}

static void fill(int x, int y, int w, int h, Rgb c, Uint8 alpha) {
    SDL_Rect r = {x, y, w, h};
    SDL_SetRenderDrawBlendMode(R, alpha == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, alpha);
    SDL_RenderFillRect(R, &r);
}

static Uint8 mul8(Uint8 a, float f) {
    return (Uint8)(a * f + 0.5f);
}

/* ---------- layout ---------- */

typedef struct {
    int W, H;
    int n;
    char **labels;
    Face body, sel, small, key;
    Lines *lines;
    int *row_y, *row_h;
    SDL_Texture **t_row;
    float B, pad, line_h;
    int row_pad, row_gap, row_inset;
    int card_x, card_y, card_w, card_h;
    int list_y, view_h, content_h, footer_y;
    int max_scroll;
    SDL_Texture *t_shadow, *t_border, *t_card, *t_key, *t_glow, *t_fade;
    int shadow_pad, glow_d, thumb_w;
} Ui;

/* Wraps every label to width and stacks the rows; returns the list height. */
static int ui_rows(Ui *u, float width) {
    int y = 0;
    for (int i = 0; i < u->n; i++) {
        wrap(&u->sel, u->labels[i], width, &u->lines[i]);
        u->row_y[i] = y;
        u->row_h[i] = (int)(u->lines[i].n * u->line_h - (u->line_h - u->body.line)) + 2 * u->row_pad;
        y += u->row_h[i] + (i + 1 < u->n ? u->row_gap : 0);
    }
    return y;
}

static void ui_layout(Ui *u) {
    float m = (float)(u->W < u->H ? u->W : u->H);
    float B = m * 0.047f;
    B = B < 14 ? 14 : B > 40 ? 40 : B;
    u->B = B;
    face_init(&u->body, &font_regular, B);
    face_init(&u->sel, &font_bold, B);
    face_init(&u->small, &font_regular, B * 0.82f);
    face_init(&u->key, &font_bold, B * 0.62f);

    u->pad = roundf(B * 1.15f);
    u->line_h = roundf(B * 1.45f);
    u->row_pad = (int)roundf(B * 0.45f);
    u->row_gap = (int)roundf(B * 0.25f);
    u->row_inset = (int)roundf(B * 0.7f);
    u->thumb_w = (int)fmaxf(3, roundf(B * 0.2f));

    float cw = u->W * 0.9f;
    if (cw > B * 30) cw = B * 30;
    u->card_w = (int)cw;
    float inner = u->card_w - 2 * u->pad;

    int div_gap = (int)roundf(B * 0.95f);
    int footer_h = (int)roundf(B * 1.5f);
    int fixed = (int)(2 * u->pad) + div_gap + footer_h;
    int max_h = (int)(u->H * 0.88f);

    u->content_h = ui_rows(u, inner - 2 * u->row_inset);
    if (fixed + u->content_h > max_h) {
        u->content_h = ui_rows(u, inner - 2 * u->row_inset - u->thumb_w - u->pad * 0.5f);
    }
    u->view_h = u->content_h;
    if (fixed + u->view_h > max_h) {
        u->view_h = max_h - fixed;
        if (u->view_h < u->line_h) u->view_h = (int)u->line_h;
    }
    u->max_scroll = u->content_h - u->view_h;
    if (u->max_scroll < 0) u->max_scroll = 0;

    u->card_h = fixed + u->view_h;
    u->card_x = (u->W - u->card_w) / 2;
    u->card_y = (u->H - u->card_h) / 2;
    u->list_y = (int)u->pad;
    u->footer_y = u->list_y + u->view_h + div_gap;

    float rad = B * 0.9f;
    int bw = (int)fmaxf(1, roundf(B / 22));
    u->shadow_pad = (int)roundf(B * 1.6f);
    u->t_shadow = make_rrect(u->card_w + 2 * u->shadow_pad, u->card_h + 2 * u->shadow_pad, rad + u->shadow_pad * 0.5f, u->shadow_pad * 0.5f);
    u->t_border = make_rrect(u->card_w, u->card_h, rad, 1);
    u->t_card = make_rrect(u->card_w - 2 * bw, u->card_h - 2 * bw, rad - bw, 1);
    int row_w = (int)inner - (u->max_scroll > 0 ? u->thumb_w + (int)(u->pad * 0.5f) : 0);
    for (int i = 0; i < u->n; i++) {
        u->t_row[i] = make_rrect(row_w, u->row_h[i], B * 0.55f, 1);
    }
    int kd = (int)roundf(B * 1.15f);
    u->t_key = make_rrect(kd, kd, kd * 0.5f, 1);
    u->glow_d = (int)(m * 1.1f);
    u->t_glow = make_rrect(u->glow_d, u->glow_d, u->glow_d * 0.5f, u->glow_d * 0.5f);

    int fh = (int)roundf(B * 1.6f);
    Uint32 *px = malloc(fh * 4);
    for (int i = 0; i < fh; i++) {
        float t = (float)i / (fh - 1);
        px[i] = ((Uint32)(t * t * 255) << 24) | 0xFFFFFF;
    }
    u->t_fade = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, fh);
    SDL_UpdateTexture(u->t_fade, NULL, px, 4);
    SDL_SetTextureBlendMode(u->t_fade, SDL_BLENDMODE_BLEND);
    free(px);
}

static void ui_free(Ui *u) {
    SDL_Texture *ts[] = {u->t_shadow, u->t_border, u->t_card, u->t_key, u->t_glow, u->t_fade};
    for (size_t i = 0; i < sizeof(ts) / sizeof(ts[0]); i++) {
        if (ts[i]) SDL_DestroyTexture(ts[i]);
    }
    for (int i = 0; i < u->n; i++) {
        if (u->t_row[i]) SDL_DestroyTexture(u->t_row[i]);
        free(u->lines[i].v);
    }
    Face *fs[] = {&u->body, &u->sel, &u->small, &u->key};
    for (size_t i = 0; i < sizeof(fs) / sizeof(fs[0]); i++) {
        face_free(fs[i]);
    }
}

/* Scroll offset that keeps the selected row inside the list view, clear of the edge fades. */
static int ui_follow(Ui *u, int scroll, int selected) {
    int fh;
    SDL_QueryTexture(u->t_fade, NULL, NULL, NULL, &fh);
    int margin = (u->view_h - u->row_h[selected]) / 2;
    if (margin > fh) margin = fh;
    if (margin < 0) margin = 0;
    int top = u->row_y[selected] - margin, bottom = u->row_y[selected] + u->row_h[selected] + margin;
    if (top < scroll) scroll = top;
    if (bottom > scroll + u->view_h) scroll = bottom - u->view_h;
    if (scroll < 0) scroll = 0;
    if (scroll > u->max_scroll) scroll = u->max_scroll;
    return scroll;
}

/* Key chip with a label to its right; returns the x where the chip starts. */
static float draw_chip(Ui *u, const char *key, const char *label, float right, int fy, int fline, Uint8 A) {
    int kd;
    SDL_QueryTexture(u->t_key, NULL, NULL, &kd, NULL);
    float lw = text_width(&u->small, label, (int)strlen(label));
    float lx = right - lw;
    draw_text(&u->small, label, (int)strlen(label), lx, fy + (fline - u->small.line) / 2, COL_MUTED, A);
    int kx = (int)(lx - u->B * 0.45f - kd);
    int ky = fy + (fline - kd) / 2;
    blit(u->t_key, kx, ky, COL_KEY, A);
    float aw = text_width(&u->key, key, (int)strlen(key));
    draw_text(&u->key, key, (int)strlen(key), kx + (kd - aw) / 2, ky + (kd - u->key.ascent) / 2 - u->key.px * 0.03f, COL_TITLE, A);
    return (float)kx;
}

static void ui_draw(Ui *u, float appear, int scroll, int selected) {
    for (int y = 0; y < u->H; y++) {
        float t = (float)y / u->H;
        SDL_SetRenderDrawColor(R,
            (Uint8)(COL_BG_TOP.r + (COL_BG_BOT.r - COL_BG_TOP.r) * t),
            (Uint8)(COL_BG_TOP.g + (COL_BG_BOT.g - COL_BG_TOP.g) * t),
            (Uint8)(COL_BG_TOP.b + (COL_BG_BOT.b - COL_BG_TOP.b) * t), 255);
        SDL_RenderDrawLine(R, 0, y, u->W - 1, y);
    }

    float e = 1 - (1 - appear) * (1 - appear) * (1 - appear);
    Uint8 A = (Uint8)(255 * e);
    int cx = u->card_x;
    int cy = u->card_y + (int)roundf((1 - e) * u->B * 0.6f);

    blit(u->t_glow, (u->W - u->glow_d) / 2, cy - u->glow_d / 2, COL_ACCENT, mul8(A, 0.07f));
    blit(u->t_shadow, cx - u->shadow_pad, cy - u->shadow_pad + (int)(u->B * 0.35f), (Rgb){0, 0, 0}, mul8(A, 0.55f));
    blit(u->t_border, cx, cy, COL_BORDER, A);
    int cw;
    SDL_QueryTexture(u->t_card, NULL, NULL, &cw, NULL);
    int bw = (u->card_w - cw) / 2;
    blit(u->t_card, cx + bw, cy + bw, COL_CARD, A);

    int px = cx + (int)u->pad;
    int inner = u->card_w - 2 * (int)u->pad;

    /* list: one rounded row per choice, the selected one tinted with the accent */
    int vy = cy + u->list_y;
    SDL_Rect clip = {px, vy, inner + (int)u->pad / 2, u->view_h};
    SDL_RenderSetClipRect(R, &clip);
    for (int i = 0; i < u->n; i++) {
        int ry = vy + u->row_y[i] - scroll;
        if (ry + u->row_h[i] < vy) continue;
        if (ry > vy + u->view_h) break;
        int sel = i == selected;
        if (sel) {
            blit(u->t_row[i], px, ry, COL_ACCENT, mul8(A, 0.18f));
        }
        Face *f = sel ? &u->sel : &u->body;
        for (int l = 0; l < u->lines[i].n; l++) {
            draw_text(f, u->lines[i].v[l].s, u->lines[i].v[l].len, px + u->row_inset,
                ry + u->row_pad + l * u->line_h, sel ? COL_TITLE : COL_BODY, A);
        }
    }
    SDL_RenderSetClipRect(R, NULL);

    if (u->max_scroll > 0) {
        int fw, fh;
        SDL_QueryTexture(u->t_fade, NULL, NULL, &fw, &fh);
        SDL_SetTextureColorMod(u->t_fade, COL_CARD.r, COL_CARD.g, COL_CARD.b);
        SDL_SetTextureAlphaMod(u->t_fade, A);
        if (scroll < u->max_scroll) {
            SDL_Rect d = {px, vy + u->view_h - fh, inner, fh};
            SDL_RenderCopy(R, u->t_fade, NULL, &d);
        }
        if (scroll > 0) {
            SDL_Rect d = {px, vy, inner, fh};
            SDL_RenderCopyEx(R, u->t_fade, NULL, &d, 0, NULL, SDL_FLIP_VERTICAL);
        }
        int track_x = cx + u->card_w - (int)(u->pad * 0.5f) - u->thumb_w / 2;
        int th = (int)((float)u->view_h * u->view_h / u->content_h);
        if (th < u->B) th = (int)u->B;
        int ty = vy + (int)((float)(u->view_h - th) * scroll / u->max_scroll);
        fill(track_x, vy, u->thumb_w, u->view_h, COL_BORDER, mul8(A, 0.6f));
        fill(track_x, ty, u->thumb_w, th, COL_MUTED, A);
    }

    /* footer: divider, navigation hint left, A/B key chips right */
    int fy = cy + u->footer_y;
    int dh = (int)fmaxf(1, roundf(u->B / 22));
    fill(px, fy - (int)roundf(u->B * 0.95f / 2), inner, dh, COL_BORDER, A);
    int fline = (int)roundf(u->B * 1.5f);
    float right = px + inner;
    right = draw_chip(u, "B", "Back", right, fy, fline, A) - u->B * 1.1f;
    draw_chip(u, "A", "Select", right, fy, fline, A);
    const char *hint = "\xE2\x86\x91\xE2\x86\x93 Choose";
    draw_text(&u->small, hint, (int)strlen(hint), px, fy + (fline - u->small.line) / 2, COL_MUTED, mul8(A, 0.85f));
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    const char *snapshot = NULL;
    int snap_w = 640, snap_h = 480, snap_sel = 0;
    int first = 1;
    while (first < argc) {
        if (!strcmp(argv[first], "--snapshot") && first + 1 < argc) {
            snapshot = argv[first + 1];
            first += 2;
        } else if (!strcmp(argv[first], "--size") && first + 1 < argc) {
            sscanf(argv[first + 1], "%dx%d", &snap_w, &snap_h);
            first += 2;
        } else if (!strcmp(argv[first], "--snapshot-selected") && first + 1 < argc) {
            snap_sel = atoi(argv[first + 1]);
            first += 2;
        } else {
            break;
        }
    }

    if (argc - first < 1) {
        fprintf(stderr, "usage: %s <label>[|command] [label[|command]...]\n", argv[0]);
        return 2;
    }

    int choice_count = argc - first;
    char **labels = malloc(choice_count * sizeof(char *));
    char **commands = malloc(choice_count * sizeof(char *));
    for (int i = 0; i < choice_count; i++) {
        char *arg = argv[first + i];
        char *pipe = strchr(arg, '|');
        if (pipe) {
            *pipe = '\0';
            labels[i] = arg;
            commands[i] = pipe + 1;
        } else {
            labels[i] = arg;
            commands[i] = arg;
        }
    }

    Ui u;
    memset(&u, 0, sizeof(u));
    u.n = choice_count;
    u.labels = labels;
    u.lines = calloc(choice_count, sizeof(Lines));
    u.row_y = calloc(choice_count, sizeof(int));
    u.row_h = calloc(choice_count, sizeof(int));
    u.t_row = calloc(choice_count, sizeof(SDL_Texture *));

    stbtt_InitFont(&font_regular, portmsg_regular_ttf, 0);
    stbtt_InitFont(&font_bold, portmsg_bold_ttf, 0);

    if (snapshot) {
        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, snap_w, snap_h, 32, SDL_PIXELFORMAT_ARGB8888);
        R = SDL_CreateSoftwareRenderer(surf);
        if (!R) {
            fprintf(stderr, "snapshot renderer failed: %s\n", SDL_GetError());
            return 2;
        }
        u.W = snap_w;
        u.H = snap_h;
        ui_layout(&u);
        if (snap_sel < 0) snap_sel = 0;
        if (snap_sel >= choice_count) snap_sel = choice_count - 1;
        ui_draw(&u, 1.0f, ui_follow(&u, 0, snap_sel), snap_sel);
        SDL_RenderPresent(R);
        SDL_SaveBMP(surf, snapshot);
        ui_free(&u);
        SDL_DestroyRenderer(R);
        SDL_FreeSurface(surf);
        return 0;
    }

    const char *output_path = getenv("GAMEPAD_PICKER_OUTPUT");
    if (output_path) {
        FILE *out = fopen(output_path, "w");
        if (out) {
            fclose(out);
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }

    SDL_DisplayMode display_mode;
    if (SDL_GetCurrentDisplayMode(0, &display_mode) != 0) {
        display_mode.w = 640;
        display_mode.h = 480;
    }

    SDL_Window *window = SDL_CreateWindow("Picker",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        display_mode.w, display_mode.h,
        SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    R = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!R) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    SDL_ShowCursor(SDL_DISABLE);
    SDL_GetRendererOutputSize(R, &u.W, &u.H);
    ui_layout(&u);

    SDL_GameController *controller = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) {
                break;
            }
        }
    }

    int selected = 0;
    int scroll = 0;
    int axis_neutral = 1;
    int running = 1;
    int accepted = 0;
    int dirty = 1;
    Uint32 start = SDL_GetTicks();
    Uint32 last_present = 0;

    while (running) {
        SDL_Event event;
        int got = SDL_WaitEventTimeout(&event, 16);
        while (got) {
            switch (event.type) {
                case SDL_QUIT:
                    running = 0;
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        selected = (selected - 1 + choice_count) % choice_count;
                    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        selected = (selected + 1) % choice_count;
                    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                        accepted = 1;
                        running = 0;
                    } else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                        running = 0;
                    }
                    dirty = 1;
                    break;
                case SDL_CONTROLLERAXISMOTION:
                    if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                        if (axis_neutral && event.caxis.value < -AXIS_THRESHOLD) {
                            selected = (selected - 1 + choice_count) % choice_count;
                            axis_neutral = 0;
                        } else if (axis_neutral && event.caxis.value > AXIS_THRESHOLD) {
                            selected = (selected + 1) % choice_count;
                            axis_neutral = 0;
                        } else if (event.caxis.value > -AXIS_THRESHOLD && event.caxis.value < AXIS_THRESHOLD) {
                            axis_neutral = 1;
                        }
                        dirty = 1;
                    }
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_UP) {
                        selected = (selected - 1 + choice_count) % choice_count;
                    } else if (event.key.keysym.sym == SDLK_DOWN) {
                        selected = (selected + 1) % choice_count;
                    } else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE) {
                        accepted = 1;
                        running = 0;
                    } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                        running = 0;
                    }
                    dirty = 1;
                    break;
                case SDL_WINDOWEVENT:
                    dirty = 1;
                    break;
            }
            got = SDL_PollEvent(&event);
        }

        Uint32 now = SDL_GetTicks();
        float appear = (now - start) / 220.0f;
        if (appear < 1) dirty = 1;
        if (appear > 1) appear = 1;
        if (now - last_present >= REPRESENT_MS) {
            dirty = 1;
        }
        if (dirty && running) {
            scroll = ui_follow(&u, scroll, selected);
            last_present = now;
            ui_draw(&u, appear, scroll, selected);
            SDL_RenderPresent(R);
            dirty = 0;
        }
    }

    if (controller) {
        SDL_GameControllerClose(controller);
    }
    ui_free(&u);
    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (accepted) {
        printf("%s\n", commands[selected]);
        if (output_path) {
            FILE *out = fopen(output_path, "w");
            if (out) {
                fprintf(out, "%s\n", commands[selected]);
                fclose(out);
            }
        }
    }

    free(labels);
    free(commands);
    free(u.lines);
    free(u.row_y);
    free(u.row_h);
    free(u.t_row);

    return accepted ? 0 : 1;
}
