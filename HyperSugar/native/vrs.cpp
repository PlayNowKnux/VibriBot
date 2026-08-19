#include "vrs.h"
#include "r5900_float.h"
#include "vrs_tables.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define strdup _strdup
#define strtok_r strtok_s
#endif

/*
 * This is basically the engine for the TTS.
 * It matches agaisnt the game PCM
 * from PCSX2 byte for byte (at least for stuff i tested.)
 * so voice generation *should* be accurate
 */

const char VRS_PHONEMES[] = "@AIUEOaiueonyPpTtKkxSsHhFBDGgZzMNRYWc#Q*";
const char VRS_CLASS1[] = ".VVVVVLLLLLHSCCCCCCCCCCCCCCCCCCCCCSSC#Qs";
const char VRS_CLASS2[] = ".VVVVVVVVVVVVUUUUUUUUUUUVVVVVVVVVVVVU#QU";

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint16_t rdle16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static int16_t rdles16(const uint8_t *p) {
    return (int16_t)rdle16(p);
}
static uint32_t rdle32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t rdles32(const uint8_t *p) {
    return (int32_t)rdle32(p);
}
static void le16(FILE *f, uint16_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    fwrite(b, 1, 2, f);
}
static void le32(FILE *f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}
static int16_t s16wrap(int64_t v) {
    return (int16_t)(uint16_t)v;
}
static int clampi(int x, int a, int b) {
    return x < a ? a : x > b ? b : x;
}

// Model files
static VrsBlob load_blob(const char *path) {
    VrsBlob b{};
    FILE *f = fopen(path, "rb");
    long n;
    if (!f)
        return b;
    if (fseek(f, 0, SEEK_END) || ((n = ftell(f)) < 0) || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return b;
    }
    b.data = (uint8_t *)malloc((size_t)n);
    if (!b.data) {
        fclose(f);
        return b;
    }
    b.size = (size_t)n;
    if (fread(b.data, 1, b.size, f) != b.size) {
        free(b.data);
        b.data = NULL;
        b.size = 0;
    }
    fclose(f);
    return b;
}
static void free_blob(VrsBlob *b) {
    free(b->data);
    b->data = NULL;
    b->size = 0;
}
static int joinpath(char *out, size_t cap, const char *dir, const char *name) {
    size_t n = strlen(dir);
    const char *sep = (n && (dir[n - 1] == '/' || dir[n - 1] == '\\')) ? "" : "/";
    return snprintf(out, cap, "%s%s%s", dir, sep, name) < (int)cap;
}

static int finish_model_load(VrsModel *m) {
    size_t off = 0;
    int s;
    for (s = 0; s < 4; s++) {
        if (off + 4 > m->map_blob.size)
            goto fail;
        uint32_t count = be32(m->map_blob.data + off);
        off += 4;
        if (off + (size_t)count * 3 > m->map_blob.size)
            goto fail;
        m->map[s].count = count;
        m->map[s].entries = m->map_blob.data + off;
        off += (size_t)count * 3;
    }
    if (m->dic_blob.size % 16) {
        fprintf(stderr, "error: DIC size isn't multiple of 16\n");
        goto fail;
    }
    if (m->bpm_blob.size < 4)
        goto fail;
    m->bpm_count = be32(m->bpm_blob.data);
    if (m->bpm_blob.size != 4u + (size_t)m->bpm_count * 2u)
        goto fail;
    return 1;
fail:
    vrs_model_free(m);
    return 0;
}

static int copy_blob(VrsBlob *dst, const uint8_t *src, size_t n) {
    dst->data = (uint8_t *)malloc(n ? n : 1);
    if (!dst->data)
        return 0;
    if (n)
        memcpy(dst->data, src, n);
    dst->size = n;
    return 1;
}

int vrs_model_load_memory(VrsModel *m, const uint8_t *map_data, size_t map_size,
                          const uint8_t *dic_data, size_t dic_size, const uint8_t *bpm_data,
                          size_t bpm_size, const uint8_t *apr_data, size_t apr_size) {
    memset(m, 0, sizeof(*m));
    if (!map_data || !dic_data || !bpm_data || !apr_data)
        goto fail;
    if (!copy_blob(&m->map_blob, map_data, map_size))
        goto fail;
    if (!copy_blob(&m->dic_blob, dic_data, dic_size))
        goto fail;
    if (!copy_blob(&m->bpm_blob, bpm_data, bpm_size))
        goto fail;
    if (!copy_blob(&m->apr_blob, apr_data, apr_size))
        goto fail;
    return finish_model_load(m);
fail:
    vrs_model_free(m);
    return 0;
}

int vrs_model_load_files(VrsModel *m, const char *map_path, const char *dic_path,
                         const char *bpm_path, const char *apr_path) {
    memset(m, 0, sizeof(*m));
    m->map_blob = load_blob(map_path);
    if (!m->map_blob.data) {
        fprintf(stderr, "error: couldn't load %s\n", map_path);
        goto fail;
    }
    m->dic_blob = load_blob(dic_path);
    if (!m->dic_blob.data) {
        fprintf(stderr, "error: couldn't load %s\n", dic_path);
        goto fail;
    }
    m->bpm_blob = load_blob(bpm_path);
    if (!m->bpm_blob.data) {
        fprintf(stderr, "error: couldn't load %s\n", bpm_path);
        goto fail;
    }
    m->apr_blob = load_blob(apr_path);
    if (!m->apr_blob.data) {
        fprintf(stderr, "error: couldn't load %s\n", apr_path);
        goto fail;
    }
    return finish_model_load(m);
fail:
    vrs_model_free(m);
    return 0;
}

int vrs_model_load(VrsModel *m, const char *dir) {
    char map_path[1024], dic_path[1024], bpm_path[1024], apr_path[1024];
    if (!joinpath(map_path, sizeof map_path, dir, "fsw04.map") ||
        !joinpath(dic_path, sizeof dic_path, dir, "fsw22m04.dic") ||
        !joinpath(bpm_path, sizeof bpm_path, dir, "fsw22m04.bpm") ||
        !joinpath(apr_path, sizeof apr_path, dir, "fsw22m04.apr"))
        return 0;
    return vrs_model_load_files(m, map_path, dic_path, bpm_path, apr_path);
}
void vrs_model_free(VrsModel *m) {
    free_blob(&m->map_blob);
    free_blob(&m->dic_blob);
    free_blob(&m->bpm_blob);
    free_blob(&m->apr_blob);
    memset(m, 0, sizeof(*m));
}

// Text and VSP input
static int phid_of(char c) {
    const char *p = strchr(VRS_PHONEMES, c);
    return p ? (int)(p - VRS_PHONEMES) : -1;
}
static int push_event(VrsLine *l, VrsEvent e) {
    VrsEvent *n = (VrsEvent *)realloc(l->events, (size_t)(l->count + 1) * sizeof(*n));
    if (!n)
        return 0;
    l->events = n;
    l->events[l->count++] = e;
    return 1;
}
static int parse_event_spec(const char *tok, int defpitch, int defpower, VrsEvent *e) {
    char tmp[128], *parts[4] = {0};
    int np = 0;
    size_t n = strlen(tok);
    if (n >= sizeof tmp)
        return 0;
    memcpy(tmp, tok, n + 1);
    char *p = tmp;
    parts[np++] = p;
    while (*p && np < 4) {
        if (*p == ':') {
            *p = 0;
            parts[np++] = p + 1;
        }
        p++;
    }
    if (strlen(parts[0]) != 1)
        return 0;
    int id = phid_of(parts[0][0]);
    if (id < 0 || id == 0)
        return 0;
    memset(e, 0, sizeof(*e));
    e->phid = id;
    e->symbol = parts[0][0];
    e->duration_ms = 0;
    e->pitch_millihz = defpitch;
    e->power_raw = defpower;
    if (np > 1 && parts[1] && *parts[1]) {
        e->duration_ms = atoi(parts[1]);
        e->duration_was_explicit = 1;
        if (e->duration_ms < 0)
            return 0;
    }
    if (np > 2 && parts[2] && *parts[2]) {
        double hz = strtod(parts[2], 0);
        if (hz <= 0)
            return 0;
        e->pitch_millihz = (int)llround(hz * 1000.0);
        e->pitch_was_explicit = 1;
    }
    if (np > 3 && parts[3] && *parts[3]) {
        e->power_raw = atoi(parts[3]);
        e->power_was_explicit = 1;
    }
    return 1;
}

/* Input: exact VRS symbols. Whitespace is optional for plain symbol strings
   '|' and '#' mark a phrase. A colon-form '#:MS:PITCH:POWER' is
   an timed event. Q and * are real controls. */
int vrs_parse_line(const char *text, VrsLine *line, int default_pitch_millihz,
                   int default_power_raw) {
    char *buf, *save = 0, *tok;
    memset(line, 0, sizeof(*line));
    buf = strdup(text);
    if (!buf)
        return 0;
    for (tok = strtok_r(buf, " \t\r\n", &save); tok; tok = strtok_r(NULL, " \t\r\n", &save)) {
        if (!strcmp(tok, "|") || !strcmp(tok, "#")) {
            if (line->count)
                line->events[line->count - 1].phrase_end = 1;
            continue;
        }
        if (strchr(tok, ':')) {
            VrsEvent e;
            if (!parse_event_spec(tok, default_pitch_millihz, default_power_raw, &e)) {
                fprintf(stderr, "error: bad token '%s'\n", tok);
                free(buf);
                vrs_line_free(line);
                return 0;
            }
            if (!push_event(line, e)) {
                free(buf);
                vrs_line_free(line);
                return 0;
            }
            continue;
        }
        for (size_t i = 0; tok[i]; i++) {
            char c = tok[i];
            if (c == '|' || c == '#') {
                if (line->count)
                    line->events[line->count - 1].phrase_end = 1;
                continue;
            }
            int id = phid_of(c);
            if (id < 0 || id == 0) {
                fprintf(stderr, "error: unknown/reserved VRS symbol '%c'\n", c);
                free(buf);
                vrs_line_free(line);
                return 0;
            }
            VrsEvent e;
            memset(&e, 0, sizeof e);
            e.phid = id;
            e.symbol = c;
            e.pitch_millihz = default_pitch_millihz;
            e.power_raw = default_power_raw;
            if (!push_event(line, e)) {
                free(buf);
                vrs_line_free(line);
                return 0;
            }
        }
    }
    free(buf);
    if (!line->count)
        return 0;
    line->events[line->count - 1].phrase_end = 1;
    return 1;
}
static int range_ok(size_t size, uint32_t off, size_t need) {
    return (uint64_t)off + (uint64_t)need <= (uint64_t)size;
}

static char symbol_for_phid(int phid) {
    if (phid >= 0 && phid < (int)strlen(VRS_PHONEMES))
        return VRS_PHONEMES[phid];
    if (phid >= 0x20 && phid <= 0x7e)
        return (char)phid;
    return '?';
}

/* Parse a real version-2 SPD/VSP file. This flattens each line's phrase array
   into one VrsLine and preserves phrase_end markers for the MAP selector. */
int vrs_parse_spd_file(const char *path, VrsLine **out_lines, int *out_line_count) {
    VrsBlob b = load_blob(path);
    VrsLine *lines = NULL;
    if (!b.data || b.size < 0x14 || b.data[0] != 'S' || b.data[1] != 'P') {
        fprintf(stderr, "error: %s is not an SPD/VSP image\n", path);
        free_blob(&b);
        return 0;
    }
    if (b.data[2] != 2) {
        fprintf(stderr, "error: unsupported SPD version %u\n", (unsigned)b.data[2]);
        free_blob(&b);
        return 0;
    }
    uint32_t image_size = rdle32(b.data + 4);
    uint32_t line_count = rdle32(b.data + 8);
    uint32_t line_off = rdle32(b.data + 0x0c);
    if (image_size < 0x14 || image_size > b.size ||
        !range_ok(image_size, line_off, (size_t)line_count * 8)) {
        fprintf(stderr, "error: malformed SPD header/pointers\n");
        free_blob(&b);
        return 0;
    }
    lines = (VrsLine *)calloc(line_count ? line_count : 1, sizeof(VrsLine));
    if (!lines) {
        free_blob(&b);
        return 0;
    }

    for (uint32_t li = 0; li < line_count; li++) {
        const uint8_t *lr = b.data + line_off + li * 8;
        uint16_t phrase_count = rdle16(lr);
        uint32_t phrase_off = rdle32(lr + 4);
        if (!range_ok(image_size, phrase_off, (size_t)phrase_count * 0x9c))
            goto bad;
        for (uint32_t pi = 0; pi < phrase_count; pi++) {
            const uint8_t *pr = b.data + phrase_off + pi * 0x9c;
            uint16_t nids = rdle16(pr + 0x90);
            uint32_t phoff = rdle32(pr + 0x98);
            if (!range_ok(image_size, phoff, (size_t)nids * 0x20))
                goto bad;
            int phrase_first = lines[li].count;
            for (uint32_t k = 0; k < nids; k++) {
                const uint8_t *ph = b.data + phoff + k * 0x20;
                int phid = rdles16(ph + 0x00);
                int pc = rdles16(ph + 0x04);
                uint32_t pvo = rdle32(ph + 0x08), ppo = rdle32(ph + 0x0c);
                int dur = rdles16(ph + 0x10);
                int qc = rdles16(ph + 0x14);
                uint32_t qvo = rdle32(ph + 0x18), qpo = rdle32(ph + 0x1c);
                if (pc < 0 || qc < 0 || dur < 0 || !range_ok(image_size, pvo, (size_t)pc * 2) ||
                    !range_ok(image_size, ppo, (size_t)pc * 2) ||
                    !range_ok(image_size, qvo, (size_t)qc * 4) ||
                    !range_ok(image_size, qpo, (size_t)qc * 2))
                    goto bad;
                VrsEvent e;
                memset(&e, 0, sizeof(e));
                e.phid = phid;
                e.symbol = symbol_for_phid(phid);
                e.duration_ms = dur;
                e.duration_was_explicit = 1;
                e.power_count = pc;
                e.pitch_count = qc;
                if (pc) {
                    e.power_values_raw = (int16_t *)malloc((size_t)pc * 2);
                    e.power_positions_ms = (int16_t *)malloc((size_t)pc * 2);
                    if (!e.power_values_raw || !e.power_positions_ms) {
                        free(e.power_values_raw);
                        free(e.power_positions_ms);
                        goto bad;
                    }
                    for (int j = 0; j < pc; j++) {
                        e.power_values_raw[j] = rdles16(b.data + pvo + j * 2);
                        e.power_positions_ms[j] = rdles16(b.data + ppo + j * 2);
                    }
                    e.power_raw = e.power_values_raw[0];
                }
                if (qc) {
                    e.pitch_values_millihz = (int32_t *)malloc((size_t)qc * 4);
                    e.pitch_positions_ms = (int16_t *)malloc((size_t)qc * 2);
                    if (!e.pitch_values_millihz || !e.pitch_positions_ms) {
                        free(e.power_values_raw);
                        free(e.power_positions_ms);
                        free(e.pitch_values_millihz);
                        free(e.pitch_positions_ms);
                        goto bad;
                    }
                    for (int j = 0; j < qc; j++) {
                        e.pitch_values_millihz[j] = rdles32(b.data + qvo + j * 4);
                        e.pitch_positions_ms[j] = rdles16(b.data + qpo + j * 2);
                    }
                    e.pitch_millihz = e.pitch_values_millihz[0];
                }
                if (!push_event(&lines[li], e)) {
                    free(e.power_values_raw);
                    free(e.power_positions_ms);
                    free(e.pitch_values_millihz);
                    free(e.pitch_positions_ms);
                    goto bad;
                }
            }
            if (lines[li].count > phrase_first)
                lines[li].events[lines[li].count - 1].phrase_end = 1;
        }
    }
    free_blob(&b);
    *out_lines = lines;
    *out_line_count = (int)line_count;
    return 1;
bad:
    fprintf(stderr, "error: malformed SPD/VSP structure in %s\n", path);
    vrs_lines_free(lines, (int)line_count);
    free_blob(&b);
    return 0;
}

void vrs_line_free(VrsLine *l) {
    if (!l)
        return;
    for (int i = 0; i < l->count; i++) {
        free(l->events[i].power_values_raw);
        free(l->events[i].power_positions_ms);
        free(l->events[i].pitch_values_millihz);
        free(l->events[i].pitch_positions_ms);
    }
    free(l->events);
    l->events = NULL;
    l->count = 0;
}

void vrs_lines_free(VrsLine *lines, int count) {
    if (!lines)
        return;
    for (int i = 0; i < count; i++)
        vrs_line_free(&lines[i]);
    free(lines);
}

int vrs_prepare_models(VrsLine *l) {
    if (!l)
        return 0;
    for (int i = 0; i < l->count; i++) {
        VrsEvent *e = &l->events[i];
        if (e->duration_ms < 0)
            return 0;
        if (e->power_count <= 0) {
            e->power_count = 2;
            e->power_values_raw = (int16_t *)malloc(2 * sizeof(int16_t));
            e->power_positions_ms = (int16_t *)malloc(2 * sizeof(int16_t));
            if (!e->power_values_raw || !e->power_positions_ms)
                return 0;
            e->power_values_raw[0] = e->power_values_raw[1] = (int16_t)e->power_raw;
            e->power_positions_ms[0] = 0;
            e->power_positions_ms[1] = (int16_t)e->duration_ms;
        }
        if (e->pitch_count <= 0) {
            e->pitch_count = 2;
            e->pitch_values_millihz = (int32_t *)malloc(2 * sizeof(int32_t));
            e->pitch_positions_ms = (int16_t *)malloc(2 * sizeof(int16_t));
            if (!e->pitch_values_millihz || !e->pitch_positions_ms)
                return 0;
            e->pitch_values_millihz[0] = e->pitch_values_millihz[1] = e->pitch_millihz;
            e->pitch_positions_ms[0] = 0;
            e->pitch_positions_ms[1] = (int16_t)e->duration_ms;
        }
    }
    return 1;
}

typedef struct {
    uint32_t apr_offset;
    uint16_t apr_len;
    uint32_t bpm_start1;
    uint16_t bpm_count;
    uint16_t scale;
    uint16_t flags;
} Dic;

enum RendererType {
    RENDER_DIRECT = 0,
    RENDER_VOICED = 1,
    RENDER_EXTENDED = 3,
    RENDER_CONTROL = 4,
};

typedef struct {
    int phid, section, index, dic_index, duration_tag_ms, renderer_type, mapped;
    Dic d;
} Sel;

// MAP/DICK source selection
static int dic_get(const VrsModel *m, int idx, Dic *d) {
    size_t o = (size_t)idx * 16;
    if (idx < 0 || o + 16 > m->dic_blob.size)
        return 0;
    const uint8_t *p = m->dic_blob.data + o;
    d->apr_offset = be32(p);
    d->apr_len = be16(p + 4);
    d->bpm_start1 = be32(p + 6);
    d->bpm_count = be16(p + 10);
    d->scale = be16(p + 12);
    d->flags = be16(p + 14);
    return 1;
}
static int map_entry(const VrsModel *m, int sec, int idx, uint16_t *dic1, uint8_t *tag) {
    if (sec < 0 || sec > 3 || idx < 0 || (uint32_t)idx >= m->map[sec].count)
        return 0;
    const uint8_t *p = m->map[sec].entries + (size_t)idx * 3;
    *dic1 = be16(p);
    *tag = p[2];
    return *dic1 != 0;
}
static int diph(int l, int r) {
    static const uint8_t p[6][2] = {{1, 2}, {1, 3}, {4, 2}, {4, 3}, {5, 2}, {5, 3}};
    for (int i = 0; i < 6; i++)
        if (p[i][0] == l && p[i][1] == r)
            return i + 1;
    return 0;
}
static int map_flat(int left, int cur, int right, int mode) {
    int lm = left - 1, cm = cur - 1, rm = right - 1, stride = 37;
    if (mode == 1) {
        if (cur < 0 || cur >= 64)
            return -1;
        cm = VRS_Q_CLASS[cur & 63] - 1;
        lm = 0;
        if (right == VRS_Q)
            rm = right - 2;
    } else if (mode == 2) {
        cm = diph(left, cur) - 1;
        lm = 0;
    } else if (mode == 3) {
        stride = 6;
        rm = diph(cur, right) - 1;
        cm = left - 1;
        lm = 0;
    } else if (mode != 0)
        return -1;
    if (cm < 0 || rm < 0)
        return -1;
    return (lm * 36 + cm) * stride + rm;
}
static int raw_to_type(int raw) {
    return raw == 0   ? RENDER_VOICED
           : raw == 1 ? RENDER_DIRECT
           : raw == 2 ? RENDER_EXTENDED
           : raw == 3 ? RENDER_CONTROL
                      : -1;
}

static int is_terminal_phid(int ph) {
    return ph == 0x25 || ph == 0x2f || ph == 0x30 || ph == 0x31 || ph == 0x32;
}

static int terminal_code(int ph) {
    switch (ph) {
    case 0x25:
        return -4;
    case 0x2f:
        return -6;
    case 0x30:
        return -5;
    case 0x31:
        return -8;
    case 0x32:
        return -9;
    default:
        return -999;
    }
}

static int select_line(const VrsModel *m, const VrsLine *l, Sel **out) {
    int N = l->count;
    int explicit_terminal = (N > 0 && is_terminal_phid(l->events[N - 1].phid));
    int n = N + (explicit_terminal ? 0 : 1);
    int loop_count = explicit_terminal ? N - 1 : N;
    int *ph = (int *)malloc((size_t)n * sizeof(int));
    int *marks = (int *)malloc((size_t)(N + 1) * sizeof(int));
    Sel *s = (Sel *)calloc((size_t)N, sizeof(Sel));
    if (!ph || !marks || !s) {
        free(ph);
        free(marks);
        free(s);
        return 0;
    }

    for (int i = 0; i < N; i++)
        ph[i] = l->events[i].phid;
    if (!explicit_terminal)
        ph[N] = VRS_BOUNDARY;
    int mn = 0;
    for (int i = 0; i < N; i++)
        if (l->events[i].phrase_end)
            marks[mn++] = i;
    if (!explicit_terminal)
        marks[mn++] = N;

    int prev_sym = VRS_BOUNDARY, prev_nonstar = VRS_BOUNDARY, mi = 0;
    for (int i = 0; i < loop_count; i++) {
        if (prev_sym != VRS_STAR)
            prev_nonstar = prev_sym;
        int cur = ph[i];
        prev_sym = cur;
        int right = (i == n - 2) ? VRS_BOUNDARY : ph[i + 1];
        if (right == VRS_STAR)
            right = (i + 2 < n) ? ph[i + 2] : VRS_BOUNDARY;
        int sec = 0, idx = -999, leftsel = prev_nonstar, rightsel = right;
        int marker = mi < mn ? marks[mi] : 0x7fff;

        if (cur == VRS_Q) {
            idx = -2;
            if (VRS_Q_CLASS[right & 63] == 0) {
                if (VRS_PRE_Q[prev_nonstar & 63])
                    idx = -10;
            } else
                idx = -7;
        } else if (cur == VRS_STAR) {
            idx = -3;
        } else {
            if (marker == i - 1) {
                mi++;
                int ln = prev_nonstar;
                if (VRS_LOWER_VOWEL[ln & 63])
                    ln -= 5;
                if (diph(ln, cur)) {
                    int rn = right;
                    if (rn == VRS_Q)
                        rn = (i < n - 3) ? ph[i + 2] : VRS_BOUNDARY;
                    idx = map_flat(ln, cur, rn, 2);
                    sec = 2;
                    leftsel = ln;
                    rightsel = rn;
                    goto selected;
                }
            }
            marker = mi < mn ? marks[mi] : 0x7fff;
            if (marker == i) {
                int rn = right;
                if (VRS_LOWER_VOWEL[rn & 63])
                    rn -= 5;
                if (diph(cur, rn)) {
                    idx = map_flat(prev_nonstar, cur, rn, 3);
                    sec = 3;
                    rightsel = rn;
                    goto selected;
                }
            }
            if (prev_nonstar == VRS_Q) {
                if (VRS_Q_CLASS[cur & 63] == 0) {
                    int ln = i < 2 ? VRS_BOUNDARY : ph[i - 2];
                    int rn = right;
                    if (rn == VRS_Q)
                        rn = (i < n - 3) ? ph[i + 2] : VRS_BOUNDARY;
                    idx = map_flat(ln, cur, rn, 0);
                    sec = 0;
                    leftsel = ln;
                    rightsel = rn;
                } else {
                    idx = map_flat(VRS_Q, cur, right, 1);
                    sec = 1;
                    leftsel = VRS_Q;
                }
            } else {
                int rn = right;
                if (rn == VRS_Q)
                    rn = (i < n - 3) ? ph[i + 2] : VRS_BOUNDARY;
                idx = map_flat(prev_nonstar, cur, rn, 0);
                sec = 0;
                rightsel = rn;
            }
        }
    selected:
        (void)leftsel;
        (void)rightsel;
        s[i].phid = cur;
        s[i].section = sec;
        s[i].index = idx;
        s[i].renderer_type = raw_to_type(VRS_SYNTH_CLASS[cur & 63]);
        if (idx >= 0) {
            uint16_t d1;
            uint8_t tag;
            if (!map_entry(m, sec, idx, &d1, &tag) || !dic_get(m, (int)d1 - 1, &s[i].d)) {
                fprintf(stderr, "error: MAP lookup failed at %d (%c), sec=%d idx=%d\n", i,
                        l->events[i].symbol, sec, idx);
                free(ph);
                free(marks);
                free(s);
                return 0;
            }
            s[i].mapped = 1;
            s[i].dic_index = (int)d1 - 1;
            s[i].duration_tag_ms = (int)tag * 5;
            if (s[i].d.flags == 1)
                s[i].renderer_type = RENDER_VOICED;
        }
    }

    if (explicit_terminal) {
        int i = N - 1, cur = ph[i];
        s[i].phid = cur;
        s[i].section = 0;
        s[i].index = terminal_code(cur);
        s[i].renderer_type = raw_to_type(VRS_SYNTH_CLASS[cur & 63]);
    }

    free(ph);
    free(marks);
    *out = s;
    return 1;
}
int vrs_get_map_durations(VrsModel *m, const VrsLine *l, int *out_durations_ms) {
    if (!m || !l || !out_durations_ms || l->count <= 0)
        return 0;

    Sel *selected = NULL;
    if (!select_line(m, l, &selected))
        return 0;

    for (int i = 0; i < l->count; i++) {
        if (selected[i].mapped) {
            out_durations_ms[i] = selected[i].duration_tag_ms;
            continue;
        }

        // FUN_002960E8 uses these durations for selectors which do not have a MAP entry.
        switch (selected[i].index) {
        case -10:
            out_durations_ms[i] = 200;
            break;
        case -9:
            out_durations_ms[i] = 50;
            break;
        case -8:
            out_durations_ms[i] = 700;
            break;
        case -7:
            out_durations_ms[i] = 0;
            break;
        case -6:
            out_durations_ms[i] = 250;
            break;
        case -5:
            out_durations_ms[i] = 500;
            break;
        case -4:
            out_durations_ms[i] = 700;
            break;
        case -3:
            out_durations_ms[i] = 55;
            break;
        case -2:
            out_durations_ms[i] = 150;
            break;
        default:
            out_durations_ms[i] = 0;
            break;
        }
    }

    free(selected);
    return 1;
}

// EE float helpers
static float f32n(double x) {
    float f = (float)x;
    return ee_bits_f32(ee_f32_bits(f));
}
static float addn(float a, float b) {
    return f32n((double)a + (double)b);
}
static float subn(float a, float b) {
    return f32n((double)a - (double)b);
}
static float muln(float a, float b) {
    return f32n((double)a * (double)b);
}
static float divn(float a, float b) {
    return f32n((double)a / (double)b);
}
static float chopmul(float a, float b) {
    return ee_bits_f32(ee_mul_s_chop(ee_f32_bits(a), ee_f32_bits(b)));
}
static float chopadd(float a, float b) {
    return ee_bits_f32(ee_add_s_chop(ee_f32_bits(a), ee_f32_bits(b)));
}
static float divnearest(float a, float b) {
    return ee_bits_f32(ee_div_s_nearest(ee_f32_bits(a), ee_f32_bits(b)));
}

typedef struct {
    uint32_t *v;
    int n, cap;
} U32Vec;

// Target pitch curve to output pulse positions
static int u32push(U32Vec *a, uint32_t x) {
    if (a->n == a->cap) {
        int c = a->cap ? a->cap * 2 : 256;
        uint32_t *p = (uint32_t *)realloc(a->v, (size_t)c * 4);
        if (!p)
            return 0;
        a->v = p;
        a->cap = c;
    }
    a->v[a->n++] = x;
    return 1;
}
static float period_from_mhz(int mhz) {
    float p = f32n(mhz), sr = f32n(VRS_SAMPLE_RATE);
    return p < f32n(10.0) ? divn(sr, f32n(10.0)) : divn(muln(sr, f32n(1000.0)), p);
}
static int trunc_u32_pos(float x) {
    if (x <= 0)
        return 0;
    if (x >= 4294967295.0f)
        return -1;
    return (int)x;
}
static int append_segment(float sp, float ep, int seg, int absend, U32Vec *p, int count) {
    sp = f32n(sp);
    ep = f32n(ep);
    float rounded = f32n((double)trunc_u32_pos(addn(ep, f32n(seg))));
    float ps = addn(sp, ep);
    int cand = ps == 0 ? 0 : (int16_t)ee_cvt_w_s(ee_f32_bits(divn(addn(rounded, rounded), ps)));
    float le = subn(muln(f32n(cand + 1), ps), rounded), re = subn(rounded, muln(f32n(cand), ps));
    if (le < re)
        cand = (int16_t)(cand + 1);
    float step = cand < 2 ? 0.0f : divn(subn(ep, sp), f32n(cand - 1));
    while (p->n <= count)
        if (!u32push(p, 0))
            return -1;
    int cur = count;
    float current = f32n(p->v[cur]), target = f32n(absend);
    int k = 1;
    while (current < target) {
        float per = addn(sp, muln(step, f32n(k)));
        if (step <= 0 && per < ep)
            per = ep;
        current = addn(current, per);
        cur++;
        if (cur >= 0x8001)
            return -1;
        uint32_t sm = (uint32_t)trunc_u32_pos(current);
        if (cur < p->n)
            p->v[cur] = sm;
        else if (!u32push(p, sm))
            return -1;
        k++;
    }
    return cur > 0x7fff ? -1 : cur - 1;
}
static int build_pulses(const VrsLine *l, U32Vec *p, int *total) {
    memset(p, 0, sizeof(*p));
    if (!l->count || l->events[0].pitch_count <= 0)
        return 0;
    if (!u32push(p, 0))
        return 0;

    int count = 0, acc = 0;
    int prevpos = (l->events[0].pitch_positions_ms[0] * VRS_SAMPLE_RATE) / 1000;
    float prev = period_from_mhz(l->events[0].pitch_values_millihz[0]);

    for (int i = 0; i < l->count; i++) {
        const VrsEvent *e = &l->events[i];
        if (e->pitch_count <= 0 || !e->pitch_values_millihz || !e->pitch_positions_ms)
            return 0;
        for (int j = 0; j < e->pitch_count; j++) {
            float cur = period_from_mhz(e->pitch_values_millihz[j]);
            int pos = (e->pitch_positions_ms[j] * VRS_SAMPLE_RATE) / 1000;
            int seg = pos - prevpos;
            int ae = pos + acc;
            if (seg > 0 && ae >= 0) {
                count = append_segment(prev, cur, seg, ae, p, count);
                if (count < 0)
                    return 0;
            }
            prev = cur;
            prevpos = pos;
        }
        acc += (e->duration_ms * VRS_SAMPLE_RATE) / 1000;
    }

    p->n = count;
    *total = acc;
    return 1;
}
static void pulse_range(const VrsLine *l, int idx, const U32Vec *p, int *start, int *end) {
    uint32_t *arr = (uint32_t *)malloc((size_t)(p->n + 1) * 4);
    for (int i = 0; i < p->n; i++)
        arr[i] = p->v[i];
    arr[p->n] = 0xffffffffu;
    int st = 0;
    for (int i = 0; i < idx; i++)
        st += (l->events[i].duration_ms * VRS_SAMPLE_RATE) / 1000;
    int en = st + (l->events[idx].duration_ms * VRS_SAMPLE_RATE) / 1000;
    int q = 0, before = idx == 0 ? arr[q] != 0 : (uint32_t)st < arr[q];
    q++;
    int cursor = 1, si = 0;
    if (!before) {
        uint32_t cur = arr[q];
        si = cursor;
        for (;;) {
            q++;
            if (cur <= (uint32_t)st) {
                si++;
                if (q > p->n)
                    q = p->n;
                cur = arr[q];
                continue;
            }
            break;
        }
        cursor = si + 1;
    }
    if (q > p->n)
        q = p->n;
    if ((uint32_t)en >= arr[q]) {
        for (;;) {
            q++;
            cursor++;
            if (q > p->n)
                q = p->n;
            if (arr[q] > (uint32_t)en)
                break;
        }
    }
    *start = si;
    *end = si != cursor ? cursor - 1 : cursor;
    free(arr);
}

// APR decode and optional source-rate resampling
static int *load_marks(const VrsModel *m, const Dic *d, int *n) {
    if (!d->bpm_count || !d->bpm_start1) {
        *n = 0;
        return NULL;
    }
    uint32_t st = d->bpm_start1 - 1;
    if ((uint64_t)st + d->bpm_count > m->bpm_count)
        return NULL;
    int *a = (int *)malloc((size_t)d->bpm_count * sizeof(int));
    if (!a)
        return NULL;
    for (int i = 0; i < d->bpm_count; i++)
        a[i] = be16(m->bpm_blob.data + 4 + ((size_t)st + i) * 2);
    *n = d->bpm_count;
    return a;
}
static int16_t *decode_apr(const VrsModel *m, int start, int len) {
    if (start < 0 || len < 0 || (uint64_t)start + len > m->apr_blob.size)
        return NULL;
    int16_t *s = (int16_t *)malloc((size_t)len * 2);
    if (!s)
        return NULL;
    for (int i = 0; i < len; i++)
        s[i] = VRS_APR_DECODE[m->apr_blob.data[start + i]];
    return s;
}

static int16_t *sinc_resample(const int16_t *src, int n, int num, int den, int *outn) {
    if (num <= 0 || den <= 0 || num > 4096 || den > 4096 || n <= 14)
        return NULL;
    if (num == den) {
        int16_t *o = (int16_t *)malloc((size_t)n * 2);
        if (o) {
            memcpy(o, src, (size_t)n * 2);
            *outn = n;
        }
        return o;
    }
    int center = num * 15;
    uint32_t *c = (uint32_t *)calloc((size_t)center * 2, 4);
    if (!c)
        return NULL;
    c[center] = ee_f32_bits(1.0f);
    uint32_t nb = ee_cvt_s_w(num), pi = ee_f32_bits(3.1415927f);
    for (int i = 1; i < center; i++) {
        uint32_t x = ee_div_s_nearest(ee_cvt_s_w(i), nb);
        x = ee_mul_s_chop(x, pi);
        uint32_t sx = ee_sin_s_nearest(x);
        uint32_t v = ee_div_s_nearest(sx, x);
        c[center + i] = c[center - i] = v;
    }
    int trimmed = n - 14, count = (trimmed * num + den - 1) / den;
    int16_t *out = (int16_t *)malloc((size_t)count * 2);
    if (!out) {
        free(c);
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        int64_t q = (int64_t)i * den;
        int base = 7 + (int)(q / num), phase = (int)(q % num);
        uint32_t acc = ee_f32_bits(0.0f);
        for (int j = -7; j <= 7; j++) {
            uint32_t sb = ee_cvt_s_w(src[base + j]), cb = c[center + phase - j * num];
            acc = ee_add_s_chop(acc, ee_mul_s_chop(sb, cb));
        }
        out[i] = s16wrap(ee_cvt_w_s(acc));
    }
    free(c);
    *outn = count;
    return out;
}
static void scale_marks(int *m, int n, int num, int den) {
    if (num == den)
        return;
    uint32_t nb = ee_cvt_s_w(num), db = ee_cvt_s_w(den);
    for (int i = 0; i < n; i++) {
        uint32_t x = ee_mul_s_chop(ee_cvt_s_w(m[i]), nb);
        x = ee_div_s_chop(x, db);
        m[i] = s16wrap(ee_cvt_w_s(x));
    }
}
static int local_mark_spacing(const int *m, int n, int idx) {
    int nd = 10000, pd = 10000;
    if (idx < n - 1)
        nd = (m[idx + 1] - m[idx]) & 0xffff;
    if (idx > 0 || (idx == 0 && nd >= 10000)) {
        if (idx > 0)
            pd = (m[idx] - m[idx - 1]) & 0xffff;
        return nd < pd ? nd : pd;
    }
    return nd;
}
static int local_target_spacing(const U32Vec *p, int start, int end, int rel) {
    int idx = start + rel;
    if (start != end) {
        int nd = 10000, pd = 10000;
        if (idx < end)
            nd = (int)p->v[idx + 1] - (int)p->v[idx];
        if (rel > 0 || (rel == 0 && nd >= 10000)) {
            if (idx > 0)
                pd = (int)p->v[idx] - (int)p->v[idx - 1];
            return nd < pd ? nd : pd;
        }
        return nd;
    }
    if (start != 0)
        return (int)p->v[idx] - (int)p->v[idx - 1];
    return (int)p->v[0];
}
// VSP power is linear. 50 gives about -4 dB and 79 is close to unity.
static float power_gain(int raw) {
    return chopmul(f32n(raw), f32n(0.012619999));
}

    //rel_sample is relative to the first pulse assigned to the phoneme. */
static float pulse_power_gain(const VrsEvent *e, int rel_sample) {
    if (!e || e->power_count <= 0 || !e->power_values_raw || !e->power_positions_ms)
        return power_gain(e ? e->power_raw : 0);
    if (e->power_count == 1)
        return power_gain(e->power_values_raw[0]);

    int first = (e->power_positions_ms[0] * VRS_SAMPLE_RATE) / 1000;
    float raw;
    if (e->power_positions_ms[0] != 0 && rel_sample < first) {
        raw = f32n(e->power_values_raw[0]);
    } else {
        int seg = 1;
        while (seg < e->power_count - 1 &&
               (e->power_positions_ms[seg] * VRS_SAMPLE_RATE) / 1000 < rel_sample)
            seg++;
        if (seg >= e->power_count)
            seg = e->power_count - 1;
        int x0 = (e->power_positions_ms[seg - 1] * VRS_SAMPLE_RATE) / 1000;
        int x1 = (e->power_positions_ms[seg] * VRS_SAMPLE_RATE) / 1000;
        int y0 = e->power_values_raw[seg - 1], y1 = e->power_values_raw[seg];
        if (x1 == x0) {
            raw = muln(f32n(y0 + y1), f32n(.5));
        } else {
            float left = chopmul(f32n(y0), f32n(x1 - rel_sample));
            left = divnearest(left, f32n(x1 - x0));
            float right = chopmul(f32n(y1), f32n(rel_sample - x0));
            right = divnearest(right, f32n(x1 - x0));
            raw = chopadd(left, right);
        }
    }
    return chopmul(raw, f32n(0.012619999));
}

/* Direct renderer power interpolation works in 5-ms blocks. pos_num/pos_den
   optionally scales the original SPD model positions. */
static float direct_power_gain_block(const VrsEvent *e, int block_index, int pos_num, int pos_den) {
    if (!e || e->power_count <= 0 || !e->power_values_raw || !e->power_positions_ms)
        return power_gain(e ? e->power_raw : 0);
    if (e->power_count == 1)
        return power_gain(e->power_values_raw[0]);
    if (pos_num <= 0)
        pos_num = 1;
    if (pos_den <= 0)
        pos_den = 1;
    int pos0 = ((e->power_positions_ms[0] * pos_num) / pos_den) / 5;
    float raw;
    if (e->power_positions_ms[0] != 0 && block_index < pos0) {
        raw = f32n(e->power_values_raw[0]);
    } else {
        int seg = 1;
        while (seg < e->power_count - 1) {
            int pb = ((e->power_positions_ms[seg] * pos_num) / pos_den) / 5;
            if (pb >= block_index)
                break;
            seg++;
        }
        if (seg >= e->power_count)
            seg = e->power_count - 1;
        int x0 = ((e->power_positions_ms[seg - 1] * pos_num) / pos_den) / 5;
        int x1 = ((e->power_positions_ms[seg] * pos_num) / pos_den) / 5;
        int y0 = e->power_values_raw[seg - 1], y1 = e->power_values_raw[seg];
        if (x1 == x0)
            raw = muln(f32n(y0 + y1), f32n(.5));
        else {
            float left = chopmul(f32n(y0), f32n(x1 - block_index));
            left = divnearest(left, f32n(x1 - x0));
            float right = chopmul(f32n(y1), f32n(block_index - x0));
            right = divnearest(right, f32n(x1 - x0));
            raw = chopadd(left, right);
        }
    }
    return chopmul(raw, f32n(0.012619999));
}

static void apply_direct_power_model(int16_t *s, int n, const VrsEvent *e, int pos_num,
                                     int pos_den) {
    const int block = (VRS_SAMPLE_RATE / 1000) * 5;
    for (int i = 0; i < n; i++) {
        float g = direct_power_gain_block(e, i / block, pos_num, pos_den);
        s[i] = s16wrap(ee_cvt_w_s(ee_mul_s_chop(ee_f32_bits(g), ee_cvt_s_w(s[i]))));
    }
}
static float source_scale(int markspan, int targetspan) {
    return targetspan == 0 ? f32n(1) : divnearest(f32n(markspan), f32n(targetspan));
}
static int source_mark_index(float scale, int rel) {
    float x = chopmul(scale, f32n(rel));
    x = chopadd(x, f32n(.5));
    return (int16_t)ee_cvt_w_s(ee_f32_bits(x));
}

// Voiced grains
static uint32_t *hann_bits(int n) {
    if (n <= 0)
        return NULL;
    uint32_t *w = (uint32_t *)calloc((size_t)n, 4);
    if (!w)
        return NULL;
    if (n == 1) {
        w[0] = ee_f32_bits(1);
        return w;
    }
    int last = n - 1, half = last / 2;
    for (int i = 0; i < half; i++) {
        float step = divnearest(f32n(6.2831855), f32n(last));
        float angle = chopmul(step, f32n(i));
        uint32_t cb = ee_cos_s_nearest(ee_f32_bits(angle));
        float ch = chopmul(ee_bits_f32(cb), f32n(.5));
        w[i] = ee_f32_bits(chopadd(f32n(.5), -ch));
    }
    int next = half;
    if (half * 2 == last) {
        w[half] = ee_f32_bits(1);
        next = half + 1;
    }
    int src = half - 1;
    for (int i = next; i < last; i++)
        w[i] = w[src--];
    w[last] = (src >= 0 && src < n) ? w[src] : w[0];
    for (int i = 0; i < n / 2; i++)
        w[last - i] = w[i];
    return w;
}
static int16_t *scaled_raw(const int16_t *src, int srcn, int start, int n, float gain, int half) {
    if (start < 0 || start + n > srcn)
        return NULL;
    int16_t *out = (int16_t *)malloc((size_t)n * 2);
    if (!out)
        return NULL;
    float g = gain;
    if (half)
        g = chopmul(g, f32n(.5));
    uint32_t gb = ee_f32_bits(g);
    for (int i = 0; i < n; i++)
        out[i] = s16wrap(ee_cvt_w_s(ee_mul_s_chop(gb, ee_cvt_s_w(src[start + i]))));
    return out;
}
static int16_t *window_raw(const int16_t *raw, int n) {
    uint32_t *w = hann_bits(n);
    if (!w)
        return NULL;
    int16_t *out = (int16_t *)malloc((size_t)n * 2);
    if (!out) {
        free(w);
        return NULL;
    }
    for (int i = 0; i < n; i++)
        out[i] = s16wrap(ee_cvt_w_s(ee_mul_s_chop(w[i], ee_cvt_s_w(raw[i]))));
    free(w);
    return out;
}
static void overlap_add(int16_t *dst, int dn, int start, const int16_t *g, int n) {
    for (int i = 0; i < n; i++) {
        int d = start + i;
        if (d >= 0 && d < dn)
            dst[d] = s16wrap((int)dst[d] + g[i]);
    }
}
static void overwrite(int16_t *dst, int dn, int start, const int16_t *s, int n) {
    int si = 0;
    if (start < 0) {
        si = -start;
        start = 0;
    }
    if (start >= dn || si >= n)
        return;
    int c = n - si;
    if (c > dn - start)
        c = dn - start;
    for (int i = 0; i < c; i++)
        dst[start + i] = s16wrap(s[si + i]);
}
static void tail_limit(int16_t *g, int n, int ref, int remaining, int total) {
    if (remaining >= total || total <= 0 || n <= 0)
        return;
    float target = f32n((double)f32n(ref) * f32n(remaining) / f32n(total));
    int peak = 0;
    for (int i = 0; i < n; i++) {
        int a = g[i] < 0 ? -(int)g[i] : g[i];
        if (a > peak)
            peak = a;
    }
    if (peak <= 0 || target >= f32n(peak))
        return;
    float sc = divn(target, f32n(peak));
    for (int i = 0; i < n; i++)
        g[i] = s16wrap((int)f32n((double)sc * f32n(g[i])));
}
static void bridge(int skipped, const U32Vec *p, int ts, int te, const int16_t *cur, int cn,
                   const int16_t *prev, int pn, int16_t *dst, int dn) {
    if (skipped <= 0 || cn <= 0 || pn <= 0)
        return;
    int ad = ts == 0 ? 1 : ts;
    if (te == 0)
        ad = 0;
    for (int rel = 0; rel < skipped; rel++) {
        int radius = 110, spacing = local_target_spacing(p, ad, te, rel);
        if (spacing < 10000)
            radius = spacing;
        int n = radius * 2 + 1;
        uint32_t *mix = (uint32_t *)malloc((size_t)n * 4);
        if (!mix)
            return;
        for (int i = 0; i < n; i++)
            mix[i] = ee_f32_bits(0);
        const int16_t *srcs[2] = {cur, prev};
        int lens[2] = {cn, pn};
        float weights[2] = {divnearest(f32n(rel + 1), f32n(skipped + 1)),
                            divnearest(f32n(skipped - rel), f32n(skipped + 1))};
        for (int z = 0; z < 2; z++) {
            int off = radius - (lens[z] - 1) / 2, ss, ds;
            if (off < 1) {
                ss = -off;
                ds = 0;
            } else {
                ss = 0;
                ds = off;
            }
            int c = lens[z] - ss;
            if (c > n - ds)
                c = n - ds;
            uint32_t wb = ee_f32_bits(weights[z]);
            for (int k = 0; k < c; k++) {
                uint32_t term = ee_mul_s_chop(wb, ee_cvt_s_w(srcs[z][ss + k]));
                mix[ds + k] = ee_add_s_chop(mix[ds + k], term);
            }
        }
        uint32_t *h = hann_bits(n);
        int16_t *bg = (int16_t *)malloc((size_t)n * 2);
        if (h && bg) {
            for (int k = 0; k < n; k++)
                bg[k] = s16wrap(ee_cvt_w_s(ee_mul_s_chop(h[k], mix[k])));
            overlap_add(dst, dn, (int)p->v[ad + rel] - radius, bg, n);
        }
        free(h);
        free(bg);
        free(mix);
    }
}

/* 0x00298e58..0x00299064 copy geometry: copy 5-ms source
   blocks, repeat the prefix while the target is more than roughly 2x the
   source, then tail-align the second half of the final segment.*/
static void render_type3_extended(int16_t *dst, int dn, int *write_cursor, int *logical_cursor,
                                  const int16_t *source, int sn, int target,
                                  const VrsEvent *event) {
    const int block = VRS_SAMPLE_RATE / 200; /* 5 ms = 110 samples */
    const int write_start = *write_cursor;
    int outpos = write_start;
    int advance = 0;
    int power_index = 0;
    int remain = target;
    uint32_t gain = ee_f32_bits(direct_power_gain_block(event, 0, 1, 1));

    if (sn <= 0 || target <= 0 || block <= 0) {
        int advance = target > 0 ? target : 0;
        *write_cursor += advance;
        *logical_cursor += advance;
        return;
    }

    /* repeat test: floor(sn/2) < floor((remain-sn)/2). */
    while ((sn / 2) < ((remain - sn) / 2)) {
        int left = sn;
        int sp = 0;
        while (block < left) {
            gain = ee_f32_bits(direct_power_gain_block(event, power_index, 1, 1));
            for (int k = 0; k < block; k++) {
                int di = outpos + k;
                if (di >= 0 && di < dn) {
                    uint32_t v = ee_mul_s_chop(gain, ee_cvt_s_w(source[sp + k]));
                    dst[di] = s16wrap(ee_cvt_w_s(v));
                }
            }
            sp += block;
            outpos += block;
            left -= block;
            power_index++;
        }

        advance += sn;
        remain = target - power_index * block;
        if (remain <= 0)
            break;
    }

    if (remain > 0) {
        const int final_total = remain;
        int left = remain;
        int sp = 0;
        while (block < left) {
            gain = ee_f32_bits(direct_power_gain_block(event, power_index, 1, 1));
            for (int k = 0; k < block; k++) {
                int si = sp + k;
                int di = outpos + k;
                if (si >= 0 && si < sn && di >= 0 && di < dn) {
                    uint32_t v = ee_mul_s_chop(gain, ee_cvt_s_w(source[si]));
                    dst[di] = s16wrap(ee_cvt_w_s(v));
                }
            }
            outpos += block;
            left -= block;
            power_index++;

            // Once the remaining target falls into its second half, align the
            // source pointer so the final sample lands on the source tail.
            if (left < final_total / 2)
                sp = sn - left;
            else
                sp += block;
        }

        gain = ee_f32_bits(direct_power_gain_block(event, power_index, 1, 1));
        for (int k = 0; k < left; k++) {
            int si = sp + k;
            int di = outpos + k;
            if (si >= 0 && si < sn && di >= 0 && di < dn) {
                uint32_t v = ee_mul_s_chop(gain, ee_cvt_s_w(source[si]));
                dst[di] = s16wrap(ee_cvt_w_s(v));
            }
        }
        outpos += left;
        advance += remain;
    }
    *write_cursor = write_start + target;
    *logical_cursor += advance;
}

// Main renderer
int vrs_render_line_with_pulses(VrsModel *m, const VrsLine *l, int num, int den,
                                const uint32_t *override_pulses, int override_count,
                                int16_t **out_pcm, int *out_count, int verbose) {
    Sel *sel = 0;
    U32Vec pulses{};
    int total = 0;
    if (!select_line(m, l, &sel))
        return 0;
    if (!build_pulses(l, &pulses, &total)) {
        free(sel);
        return 0;
    }
    if (override_pulses && override_count > 0) {
        if (override_count < 2) {
            fprintf(stderr, "error: pulse override needs at least 2 pulses\n");
            free(sel);
            free(pulses.v);
            return 0;
        }
        for (int i = 1; i < override_count; i++)
            if (override_pulses[i] <= override_pulses[i - 1]) {
                fprintf(stderr, "error: pulse override must be strictly increasing\n");
                free(sel);
                free(pulses.v);
                return 0;
            }
        uint32_t *pv = (uint32_t *)malloc((size_t)override_count * sizeof(uint32_t));
        if (!pv) {
            free(sel);
            free(pulses.v);
            return 0;
        }
        memcpy(pv, override_pulses, (size_t)override_count * sizeof(uint32_t));
        free(pulses.v);
        pulses.v = pv;
        pulses.n = pulses.cap = override_count;
        if (verbose)
            fprintf(stderr, "using explicit target pulse train (%d pulses)\n", override_count);
    }
    int16_t *dst = (int16_t *)calloc((size_t)(total ? total : 1), 2);
    if (!dst) {
        free(sel);
        free(pulses.v);
        return 0;
    }
    int prevtype = RENDER_DIRECT, prevph = VRS_BOUNDARY, prevdur = 0, state = 0;
    int write_cursor = 0, logical_cursor = 0;
    int16_t *saved = NULL;
    int savedn = 0;
    for (int idx = 0; idx < l->count; idx++) {
        Sel *r = &sel[idx];
        const VrsEvent *e = &l->events[idx];
        int type = r->renderer_type;
        int next = (idx + 1 < l->count) ? sel[idx + 1].renderer_type : RENDER_CONTROL;
        int ph = e->phid;
        int dur = (e->duration_ms * VRS_SAMPLE_RATE) / 1000;
        if (state == 2 || state == 3)
            state = (VRS_TRANS_A[ph & 63] || VRS_TRANS_B[ph & 63]) ? 2 : 1;
        if (state != 0 && (ph == 0x21 || ph == 0x0c || prevph == 0x21 || prevph == 0x0c))
            state = 3;
        if (prevtype == RENDER_VOICED && type != RENDER_VOICED && savedn) {
            int advance = 1 + (savedn - 1) / 2;
            write_cursor += advance;
            logical_cursor += advance;
        }
        int acoustic = r->mapped && VRS_EXCLUSION[ph & 63] == 0 && ph != VRS_Q;
        if (verbose)
            fprintf(stderr, "[%d] %c type=%d dur=%dms map=%d:%d dic=%d cursor=%d\n", idx, e->symbol,
                    type, e->duration_ms, r->section, r->index, r->mapped ? r->dic_index + 1 : 0,
                    logical_cursor);
        if (e->duration_ms == 0) {
            if (verbose)
                fprintf(stderr, "    zero-duration phoneme: %c (no audio)\n",
                        e->symbol);
            continue;
        }
        if (!acoustic) {
            write_cursor += dur;
            logical_cursor += dur;
            state = 0;
            free(saved);
            saved = NULL;
            savedn = 0;
            prevtype = type;
            prevph = ph;
            prevdur = dur;
            continue;
        }
        if (type == RENDER_VOICED) {
            int ps, pe;
            pulse_range(l, idx, &pulses, &ps, &pe);
            int realend = pe;
            if (realend >= pulses.n)
                realend = pulses.n - 1;
            if (ps >= pulses.n || realend < ps) {
                prevtype = type;
                prevph = ph;
                prevdur = dur;
                continue;
            }
            int mn = 0, *marks = load_marks(m, &r->d, &mn);
            if (!marks || mn < 1) {
                free(marks);
                goto fail;
            }
            if (mn > 2)
                mn--;
            int gs = (int)r->d.apr_offset - VRS_GUARD_SAMPLES,
                ge = (int)r->d.apr_offset + r->d.apr_len + VRS_GUARD_SAMPLES, sn = ge - gs;
            int16_t *source = decode_apr(m, gs, sn);
            if (!source) {
                free(marks);
                goto fail;
            }
            if (num != den) {
                int rn = 0;
                int16_t *rs = sinc_resample(source, sn, num, den, &rn);
                free(source);
                source = rs;
                sn = rn;
                if (!source) {
                    free(marks);
                    goto fail;
                }
                scale_marks(marks, mn, num, den);
            }
            int tmode = 0;
            if (prevtype == RENDER_DIRECT || prevtype == RENDER_EXTENDED ||
                prevtype == RENDER_CONTROL)
                tmode = 2;
            if (next == RENDER_DIRECT || next == RENDER_EXTENDED || next == RENDER_CONTROL)
                tmode = 1;
            int rawstart = VRS_TRANS_SKIP[state], ad = ps == 0 ? 1 : ps;
            if (realend == 0)
                ad = 0;
            int full = realend - ad + 1;
            if (full <= 0) {
                free(source);
                free(marks);
                prevtype = type;
                prevph = ph;
                prevdur = dur;
                continue;
            }
            int startrel = rawstart < full ? rawstart : full - 1;
            float sc = full == 1 ? f32n(1) : source_scale(mn - 1, full - 1);
            int tail = 3, tailref = 0;
            if (tail > full - startrel)
                tail = full - startrel;
            int16_t *last = NULL;
            int lastn = 0;
            int gain_i = 0;
            for (int rel = startrel; rel < full; rel++) {
                int si = source_mark_index(sc, rel);
                si = clampi(si, 0, mn - 1);
                int ss = local_mark_spacing(marks, mn, si),
                    ts = local_target_spacing(&pulses, ad, realend, rel), ti = ad + rel,
                    tc = (int)pulses.v[ti], rad = 110;
                if (ts < 10000)
                    rad = ts;
                if (ss > 0 && ss < rad)
                    rad = ss;
                if (tc < rad)
                    rad = tc;
                int gn = rad * 2 + 1, start = marks[si] - (rad + 1);
                int gpi = ps + gain_i;
                if (gpi >= pulses.n)
                    gpi = pulses.n - 1;
                int rels =
                    (gpi >= ps && ps < pulses.n) ? ((int)pulses.v[gpi] - (int)pulses.v[ps]) : 0;
                float gain = pulse_power_gain(e, rels);
                gain_i++;
                int16_t *raw =
                    scaled_raw(source, sn, start, gn, gain, rel == startrel && tmode == 2);
                if (!raw)
                    goto voiced_fail;
                if (startrel != 0 && rel == startrel && savedn)
                    bridge(startrel, &pulses, ps, realend, raw, gn, saved, savedn, dst, total);
                int16_t *win = window_raw(raw, gn);
                if (!win) {
                    free(raw);
                    goto voiced_fail;
                }
                if (tmode == 1) {
                    int rem = full - rel;
                    if (rem == tail) {
                        tailref = 0;
                        for (int k = 0; k < gn; k++) {
                            int a = win[k] < 0 ? -(int)win[k] : win[k];
                            if (a > tailref)
                                tailref = a;
                        }
                    } else if (rem < tail)
                        tail_limit(win, gn, tailref, rem, tail);
                }
                overlap_add(dst, total, tc - rad, win, gn);
                free(win);
                free(last);
                last = raw;
                lastn = gn;
            }
            free(saved);
            saved = last;
            savedn = lastn;
            state = 1;
            if (VRS_TRANS_A[ph & 63] || VRS_TRANS_B[ph & 63])
                state = 2;
            if (ph == 0x21 || ph == 0x0c)
                state = 3;
            write_cursor = logical_cursor = (int)pulses.v[realend];
            free(source);
            free(marks);
            goto voiced_ok;
        voiced_fail:
            free(last);
            free(source);
            free(marks);
            goto fail;
        voiced_ok:;
        } else if (type == RENDER_DIRECT) {
            int sn = r->d.apr_len;
            int16_t *source = decode_apr(m, (int)r->d.apr_offset, sn);
            if (!source)
                goto fail;
            int copied = sn, ss = 0, ws = write_cursor;
            if (sn < dur) {
                int advance = dur - sn;
                write_cursor += advance;
                logical_cursor += advance;
                ws = write_cursor;
            } else if (sn > dur) {
                int diff = sn - dur;
                if (idx == 0) {
                    ss = diff;
                    copied = dur;
                } else if (prevdur < diff) {
                    write_cursor -= prevdur;
                    logical_cursor -= prevdur;
                    copied = dur + prevdur;
                    ss = sn - copied;
                    ws = write_cursor;
                } else {
                    write_cursor -= diff;
                    logical_cursor -= diff;
                    ws = write_cursor;
                }
            }
            int16_t *seg = (int16_t *)malloc((size_t)copied * 2);
            if (!seg) {
                free(source);
                goto fail;
            }
            memcpy(seg, source + ss, (size_t)copied * 2);
            apply_direct_power_model(seg, copied, e, copied, dur > 0 ? dur : 1);
            overwrite(dst, total, ws, seg, copied);
            write_cursor = ws + copied;
            logical_cursor += copied;
            free(seg);
            free(source);
            state = 0;
            free(saved);
            saved = NULL;
            savedn = 0;
        } else if (type == RENDER_EXTENDED) {
            int sn = r->d.apr_len;
            int16_t *source = decode_apr(m, (int)r->d.apr_offset, sn);
            if (!source)
                goto fail;
            if (sn < dur)
                render_type3_extended(dst, total, &write_cursor, &logical_cursor, source, sn,
                                      dur, e);
            else {
                int16_t *seg = (int16_t *)malloc((size_t)dur * 2);
                if (!seg) {
                    free(source);
                    goto fail;
                }
                memcpy(seg, source, (size_t)dur * 2);
                apply_direct_power_model(seg, dur, e, 1, 1);
                overwrite(dst, total, write_cursor, seg, dur);
                write_cursor += dur;
                logical_cursor += dur;
                free(seg);
            }
            free(source);
            state = 0;
            free(saved);
            saved = NULL;
            savedn = 0;
        } else {
            write_cursor += dur;
            logical_cursor += dur;
            state = 0;
            free(saved);
            saved = NULL;
            savedn = 0;
        }
        prevtype = type;
        prevph = ph;
        prevdur = dur;
    }
    free(saved);
    free(sel);
    free(pulses.v);
    *out_pcm = dst;
    *out_count = total;
    return 1;
fail:
    free(saved);
    free(sel);
    free(pulses.v);
    free(dst);
    return 0;
}

int vrs_render_line(VrsModel *m, const VrsLine *l, int num, int den, int16_t **out_pcm,
                    int *out_count, int verbose) {
    return vrs_render_line_with_pulses(m, l, num, den, NULL, 0, out_pcm, out_count, verbose);
}

int vrs_upsample2x(const int16_t *src, int n, int16_t **out, int *outn) {
    int16_t *d = (int16_t *)malloc((size_t)n * 4);
    if (!d)
        return 0;
    for (int i = 0; i < n; i++) {
        d[i * 2] = src[i];
        d[i * 2 + 1] = i + 1 < n ? (int16_t)(((int)src[i] + src[i + 1]) / 2) : src[i];
    }
    *out = d;
    *outn = n * 2;
    return 1;
}
int vrs_write_wav(const char *path, const int16_t *pcm, int samples, int sr) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    uint32_t bytes = (uint32_t)samples * 2;
    fwrite("RIFF", 1, 4, f);
    le32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    le32(f, 16);
    le16(f, 1);
    le16(f, 1);
    le32(f, (uint32_t)sr);
    le32(f, (uint32_t)sr * 2);
    le16(f, 2);
    le16(f, 16);
    fwrite("data", 1, 4, f);
    le32(f, bytes);
    fwrite(pcm, 2, (size_t)samples, f);
    fclose(f);
    return 1;
}
