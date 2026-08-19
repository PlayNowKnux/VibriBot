#ifndef MOJIB_VRS_H
#define MOJIB_VRS_H
#include <stddef.h>
#include <stdint.h>

#define VRS_SAMPLE_RATE 22050
#define VRS_GUARD_SAMPLES 440
#define VRS_BOUNDARY 37
#define VRS_Q 38
#define VRS_STAR 39

extern const char VRS_PHONEMES[];
extern const char VRS_CLASS1[];
extern const char VRS_CLASS2[];

typedef struct {
    uint8_t *data;
    size_t size;
} VrsBlob;
typedef struct {
    uint32_t count;
    const uint8_t *entries;
} VrsMapSection;
typedef struct {
    VrsBlob map_blob, dic_blob, bpm_blob, apr_blob;
    VrsMapSection map[4];
    uint32_t bpm_count;
} VrsModel;

typedef struct {
    int phid;
    char symbol;
    int duration_ms;
    int pitch_millihz;
    int power_raw;
    int duration_was_explicit;
    int pitch_was_explicit;
    int power_was_explicit;
    int phrase_end;

    int power_count;
    int16_t *power_values_raw;
    int16_t *power_positions_ms;
    int pitch_count;
    int32_t *pitch_values_millihz;
    int16_t *pitch_positions_ms;
} VrsEvent;

typedef struct {
    VrsEvent *events;
    int count;
} VrsLine;

int vrs_model_load(VrsModel *m, const char *dir);
int vrs_model_load_files(VrsModel *m, const char *map_path, const char *dic_path,
                         const char *bpm_path, const char *apr_path);
int vrs_model_load_memory(VrsModel *m, const uint8_t *map_data, size_t map_size,
                          const uint8_t *dic_data, size_t dic_size, const uint8_t *bpm_data,
                          size_t bpm_size, const uint8_t *apr_data, size_t apr_size);
void vrs_model_free(VrsModel *m);

int vrs_parse_line(const char *text, VrsLine *line, int default_pitch_millihz,
                   int default_power_raw);
int vrs_parse_spd_file(const char *path, VrsLine **out_lines, int *out_line_count);
void vrs_line_free(VrsLine *line);
void vrs_lines_free(VrsLine *lines, int count);
int vrs_prepare_models(VrsLine *line);
int vrs_get_map_durations(VrsModel *m, const VrsLine *line, int *out_durations_ms);

int vrs_render_line(VrsModel *m, const VrsLine *line, int source_rate_num, int source_rate_den,
                    int16_t **out_pcm, int *out_count, int verbose);
int vrs_render_line_with_pulses(VrsModel *m, const VrsLine *line, int source_rate_num,
                                int source_rate_den, const uint32_t *pulses, int pulse_count,
                                int16_t **out_pcm, int *out_count, int verbose);

int vrs_write_wav(const char *path, const int16_t *pcm, int samples, int sample_rate);
int vrs_upsample2x(const int16_t *src, int n, int16_t **out, int *out_n);

#endif
