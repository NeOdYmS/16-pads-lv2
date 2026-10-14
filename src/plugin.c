#include "sampler.h"

#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/log/log.h>
#include <lv2/midi/midi.h>
#include <lv2/patch/patch.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include <math.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLER_URI "https://github.com/NeOdYmS/16-pads-lv2#sampler"
#define PATH_CAPACITY 1024
#define RETIRED_CAPACITY (PAD_COUNT * 2)

typedef struct {
    LV2_URID sequence, object, blank, path, string, urid, midi;
    LV2_URID set, get, property, value, error, log_error;
    LV2_URID sample[PAD_COUNT];
} URIs;

typedef struct {
    int release;
    unsigned pad;
    uint32_t generation;
    Sample* sample;
    char path[PATH_CAPACITY];
} Work;

typedef struct {
    unsigned pad;
    uint32_t generation;
    Sample* sample;
    char path[PATH_CAPACITY];
    char error[160];
} Result;

typedef struct {
    const LV2_Atom_Sequence* events;
    LV2_Atom_Sequence* notify;
    float* left;
    float* right;
    const float* controls[PAD_COUNT][5];
    float* activity[PAD_COUNT];
    double rate;
    LV2_Worker_Schedule* worker;
    LV2_Log_Log* log;
    LV2_Atom_Forge forge;
    URIs uris;
    Pad pads[PAD_COUNT];
    uint16_t notes[PAD_COUNT], ccs[PAD_COUNT];
    int previous_note[PAD_COUNT], previous_cc[PAD_COUNT];
    PadMode previous_mode[PAD_COUNT];
    int previous_gate[PAD_COUNT];
    char paths[PAD_COUNT][PATH_CAPACITY];
    atomic_uchar saved_paths[PAD_COUNT][PATH_CAPACITY];
    atomic_uint path_version[PAD_COUNT];
    atomic_bool worker_failed[PAD_COUNT];
    char wanted[PAD_COUNT][PATH_CAPACITY];
    uint32_t generation[PAD_COUNT];
    int requested[PAD_COUNT], pending[PAD_COUNT], changed[PAD_COUNT];
    Sample* retired[RETIRED_CAPACITY];
    unsigned retired_count;
    char error[192];
} Plugin;

static void publish_path(Plugin* self, unsigned pad)
{
    atomic_fetch_add(&self->path_version[pad], 1);
    for (unsigned i = 0; i < PATH_CAPACITY; ++i) {
        atomic_store(&self->saved_paths[pad][i], (unsigned char)self->paths[pad][i]);
    }
    atomic_fetch_add(&self->path_version[pad], 1);
}

static int snapshot_path(Plugin* self, unsigned pad, char* path)
{
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        const unsigned version = atomic_load(&self->path_version[pad]);
        if (version & 1u) {
            continue;
        }
        for (unsigned i = 0; i < PATH_CAPACITY; ++i) {
            path[i] = (char)atomic_load(&self->saved_paths[pad][i]);
        }
        if (version == atomic_load(&self->path_version[pad])) {
            return 1;
        }
    }
    return 0;
}

static void report(Plugin* self, const char* message)
{
    if (self->log) {
        self->log->printf(self->log->handle, self->uris.log_error,
                          "16 Pads: %s\n", message);
    }
}

static float control(const Plugin* self, unsigned pad, unsigned parameter)
{
    static const float defaults[] = {0, -1, -1, 0, 0};
    return self->controls[pad][parameter] ?
           *self->controls[pad][parameter] : defaults[parameter];
}

static void stop_pad(Plugin* self, unsigned pad)
{
    pad_stop(&self->pads[pad]);
    self->pads[pad].note_held = self->pads[pad].cc_held = 0;
    self->pads[pad].gate_held = 0;
    self->notes[pad] = self->ccs[pad] = 0;
}

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate,
                              const char* bundle, const LV2_Feature* const* features)
{
    (void)descriptor;
    (void)bundle;
    if (!isfinite(rate) || rate <= 0) {
        return NULL;
    }
    Plugin* self = calloc(1, sizeof(*self));
    if (!self) {
        return NULL;
    }
    LV2_URID_Map* map = NULL;
    for (unsigned i = 0; features && features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            map = features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_WORKER__schedule)) {
            self->worker = features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_LOG__log)) {
            self->log = features[i]->data;
        }
    }
    if (!map || !self->worker) {
        free(self);
        return NULL;
    }
#define MAP(field, uri) self->uris.field = map->map(map->handle, uri)
    MAP(sequence, LV2_ATOM__Sequence);
    MAP(object, LV2_ATOM__Object);
    MAP(blank, LV2_ATOM__Blank);
    MAP(path, LV2_ATOM__Path);
    MAP(string, LV2_ATOM__String);
    MAP(urid, LV2_ATOM__URID);
    MAP(midi, LV2_MIDI__MidiEvent);
    MAP(set, LV2_PATCH__Set);
    MAP(get, LV2_PATCH__Get);
    MAP(property, LV2_PATCH__property);
    MAP(value, LV2_PATCH__value);
    MAP(error, SAMPLER_URI ":error");
    MAP(log_error, LV2_LOG__Error);
#undef MAP
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        char uri[160];
        snprintf(uri, sizeof(uri), SAMPLER_URI ":sample%02u", pad + 1);
        self->uris.sample[pad] = map->map(map->handle, uri);
        self->previous_note[pad] = self->previous_cc[pad] = -1;
        atomic_init(&self->path_version[pad], 0);
        atomic_init(&self->worker_failed[pad], 0);
        for (unsigned i = 0; i < PATH_CAPACITY; ++i) {
            atomic_init(&self->saved_paths[pad][i], 0);
        }
    }
    if (!atomic_is_lock_free(&self->saved_paths[0][0]) ||
        !atomic_is_lock_free(&self->path_version[0]) ||
        !atomic_is_lock_free(&self->worker_failed[0])) {
        free(self);
        return NULL;
    }
    self->rate = rate;
    lv2_atom_forge_init(&self->forge, map);
    return self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    Plugin* self = instance;
    switch (port) {
    case 0: self->events = data; break;
    case 1: self->notify = data; break;
    case 2: self->left = data; break;
    case 3: self->right = data; break;
    default:
        if (port < 4 + PAD_COUNT * 6) {
            const unsigned pad = (port - 4) / 6;
            const unsigned parameter = (port - 4) % 6;
            if (parameter == 5) {
                self->activity[pad] = data;
            } else {
                self->controls[pad][parameter] = data;
            }
        }
    }
}

static void activate(LV2_Handle instance)
{
    Plugin* self = instance;
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        stop_pad(self, pad);
        self->previous_gate[pad] = 0;
    }
}

static void render(Plugin* self, uint32_t start, uint32_t end)
{
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        pad_render(&self->pads[pad], pad_mode(control(self, pad, 0)),
                   pad_gain(control(self, pad, 3)), self->rate,
                   self->left, self->right, start, end);
    }
}

static void midi(Plugin* self, const uint8_t* bytes, uint32_t size)
{
    if (size != 3 || bytes[1] > 127 || bytes[2] > 127) {
        return;
    }
    const unsigned type = bytes[0] & 0xf0;
    const uint16_t channel = (uint16_t)(1u << (bytes[0] & 15));
    if (type == 0xb0 && (bytes[1] == 120 || bytes[1] == 123)) {
        for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
            stop_pad(self, pad);
        }
        return;
    }
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        Pad* voice = &self->pads[pad];
        const PadMode mode = pad_mode(control(self, pad, 0));
        if ((type == 0x80 || type == 0x90) &&
            midi_assignment(control(self, pad, 1)) == bytes[1]) {
            if (type == 0x90 && bytes[2]) {
                self->notes[pad] |= channel;
                voice->note_held = 1;
                pad_trigger(voice, bytes[2] / 127.0f);
            } else {
                self->notes[pad] &= (uint16_t)~channel;
                voice->note_held = self->notes[pad] != 0;
                pad_release(voice, mode);
            }
        } else if (type == 0xb0 &&
                   midi_assignment(control(self, pad, 2)) == bytes[1]) {
            if (bytes[2] >= 64) {
                if (!(self->ccs[pad] & channel)) {
                    pad_trigger(voice, 1);
                }
                self->ccs[pad] |= channel;
            } else {
                self->ccs[pad] &= (uint16_t)~channel;
            }
            voice->cc_held = self->ccs[pad] != 0;
            pad_release(voice, mode);
        }
    }
}

static void command(Plugin* self, const LV2_Atom_Object* object)
{
    const uint8_t* cursor = (const uint8_t*)&object->body +
                            sizeof(LV2_Atom_Object_Body);
    const uint8_t* end = (const uint8_t*)&object->body + object->atom.size;
    while (cursor < end) {
        if ((size_t)(end - cursor) < sizeof(LV2_Atom_Property_Body)) {
            snprintf(self->error, sizeof(self->error), "Malformed patch object");
            return;
        }
        const LV2_Atom_Property_Body* prop = (const LV2_Atom_Property_Body*)cursor;
        const uint64_t size = sizeof(LV2_Atom_Property_Body) +
                              (((uint64_t)prop->value.size + 7) & ~UINT64_C(7));
        if (size > (uint64_t)(end - cursor)) {
            snprintf(self->error, sizeof(self->error), "Malformed patch property");
            return;
        }
        cursor += size;
    }
    const LV2_Atom* property = NULL;
    const LV2_Atom* value = NULL;
    lv2_atom_object_get(object, self->uris.property, &property,
                        self->uris.value, &value, 0);
    LV2_URID key = 0;
    if (property && property->type == self->uris.urid &&
        property->size == sizeof(LV2_URID)) {
        memcpy(&key, LV2_ATOM_BODY_CONST(property), sizeof(key));
    }
    if (object->body.otype == self->uris.get) {
        for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
            if (!property || key == self->uris.sample[pad]) {
                self->changed[pad] = 1;
            }
        }
        return;
    }
    if (object->body.otype != self->uris.set) {
        return;
    }
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        if (key != self->uris.sample[pad]) {
            continue;
        }
        if (!value || value->type != self->uris.path || !value->size ||
            value->size > PATH_CAPACITY ||
            ((const char*)LV2_ATOM_BODY_CONST(value))[value->size - 1] != '\0' ||
            memchr(LV2_ATOM_BODY_CONST(value), '\0', value->size - 1)) {
            snprintf(self->error, sizeof(self->error),
                     "Pad %02u: sample must be a NUL-terminated atom:Path (max 1023 bytes)",
                     pad + 1);
            return;
        }
        memcpy(self->wanted[pad], LV2_ATOM_BODY_CONST(value), value->size);
        ++self->generation[pad];
        self->requested[pad] = 1;
        return;
    }
}

static void schedule(Plugin* self)
{
    while (self->retired_count) {
        Work work = {0};
        work.release = 1;
        work.sample = self->retired[self->retired_count - 1];
        if (self->worker->schedule_work(self->worker->handle, sizeof(work), &work)
            != LV2_WORKER_SUCCESS) {
            break;
        }
        --self->retired_count;
    }
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        /* Reserve one retirement slot for every possible in-flight response. */
        if (self->retired_count >= PAD_COUNT) {
            break;
        }
        if (!self->requested[pad] || self->pending[pad]) {
            continue;
        }
        Work work = {0};
        work.pad = pad;
        work.generation = self->generation[pad];
        memcpy(work.path, self->wanted[pad], PATH_CAPACITY);
        self->pending[pad] = 1;
        self->requested[pad] = 0;
        if (self->worker->schedule_work(self->worker->handle, sizeof(work), &work)
            != LV2_WORKER_SUCCESS) {
            self->pending[pad] = 0;
            self->requested[pad] = 1;
            snprintf(self->error, sizeof(self->error),
                     "Sample worker queue full; load deferred");
        }
    }
}

static int notification(Plugin* self, LV2_URID property, const char* text,
                        int is_path)
{
    LV2_Atom_Forge* forge = &self->forge;
    /* Preflight the entire message: never leave a truncated atom in the output. */
    if ((uint64_t)forge->offset + strlen(text) + 128 > forge->size) {
        return 0;
    }
    LV2_Atom_Forge_Frame object;
    lv2_atom_forge_frame_time(forge, 0);
    lv2_atom_forge_object(forge, &object, 0, self->uris.set);
    lv2_atom_forge_key(forge, self->uris.property);
    lv2_atom_forge_urid(forge, property);
    lv2_atom_forge_key(forge, self->uris.value);
    if (is_path) {
        lv2_atom_forge_path(forge, text, (uint32_t)strlen(text));
    } else {
        lv2_atom_forge_string(forge, text, (uint32_t)strlen(text));
    }
    lv2_atom_forge_pop(forge, &object);
    return 1;
}

static void run(LV2_Handle instance, uint32_t frames)
{
    Plugin* self = instance;
    if (!self->left || !self->right) {
        return;
    }
    memset(self->left, 0, (size_t)frames * sizeof(float));
    memset(self->right, 0, (size_t)frames * sizeof(float));
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        if (atomic_exchange(&self->worker_failed[pad], 0)) {
            self->pending[pad] = 0;
            snprintf(self->error, sizeof(self->error),
                     "Pad %02u: host rejected worker response; retry sample selection",
                     pad + 1);
        }
        const int note = midi_assignment(control(self, pad, 1));
        const int cc = midi_assignment(control(self, pad, 2));
        const PadMode mode = pad_mode(control(self, pad, 0));
        const float gate_value = control(self, pad, 4);
        const int gate = isfinite(gate_value) && gate_value >= 0.5f;
        const int entering_trig = mode == PAD_TRIG &&
                                  self->previous_mode[pad] != PAD_TRIG;
        if (entering_trig) {
            stop_pad(self, pad);
        }
        if (note != self->previous_note[pad]) {
            self->notes[pad] = 0;
            self->pads[pad].note_held = 0;
        }
        if (cc != self->previous_cc[pad]) {
            self->ccs[pad] = 0;
            self->pads[pad].cc_held = 0;
        }
        self->pads[pad].gate_held = gate;
        if (gate && (!self->previous_gate[pad] || entering_trig)) {
            pad_trigger(&self->pads[pad], 1);
        }
        pad_release(&self->pads[pad], mode);
        self->previous_gate[pad] = gate;
        self->previous_note[pad] = note;
        self->previous_cc[pad] = cc;
        self->previous_mode[pad] = mode;
    }
    uint32_t cursor = 0;
    if (self->events && self->events->atom.type == self->uris.sequence &&
        self->events->atom.size >= sizeof(LV2_Atom_Sequence_Body)) {
        const uint8_t* event_ptr = (const uint8_t*)self->events +
                                  sizeof(LV2_Atom_Sequence);
        const uint8_t* end = (const uint8_t*)self->events +
                            sizeof(LV2_Atom) + self->events->atom.size;
        while ((size_t)(end - event_ptr) >= sizeof(LV2_Atom_Event)) {
            const LV2_Atom_Event* event = (const LV2_Atom_Event*)event_ptr;
            const uint64_t event_size = sizeof(LV2_Atom_Event) +
                                       (((uint64_t)event->body.size + 7) & ~UINT64_C(7));
            if (event_size > (uint64_t)(end - event_ptr)) {
                snprintf(self->error, sizeof(self->error), "Malformed input event");
                break;
            }
            if (event->time.frames >= (int64_t)cursor &&
                event->time.frames < (int64_t)frames) {
                const uint32_t time = (uint32_t)event->time.frames;
                render(self, cursor, time);
                cursor = time;
                if (event->body.type == self->uris.midi) {
                    midi(self, (const uint8_t*)(event + 1), event->body.size);
                } else if ((event->body.type == self->uris.object ||
                            event->body.type == self->uris.blank) &&
                           event->body.size >= sizeof(LV2_Atom_Object_Body)) {
                    command(self, (const LV2_Atom_Object*)&event->body);
                }
            }
            event_ptr += event_size;
        }
    }
    render(self, cursor, frames);
    schedule(self);
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        if (self->activity[pad]) {
            const Pad* voice = &self->pads[pad];
            const int held = voice->note_held || voice->cc_held || voice->gate_held;
            *self->activity[pad] = pad_mode(control(self, pad, 0)) == PAD_TRIG ?
                                   (float)held : (float)voice->playing;
        }
    }
    if (self->notify) {
        const uint32_t capacity = self->notify->atom.size;
        if (capacity >= sizeof(LV2_Atom_Sequence)) {
            LV2_Atom_Forge_Frame sequence;
            lv2_atom_forge_set_buffer(&self->forge, (uint8_t*)self->notify, capacity);
            lv2_atom_forge_sequence_head(&self->forge, &sequence, 0);
            for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
                if (self->changed[pad] &&
                    notification(self, self->uris.sample[pad], self->paths[pad], 1)) {
                    self->changed[pad] = 0;
                }
            }
            if (self->error[0] &&
                notification(self, self->uris.error, self->error, 0)) {
                self->error[0] = '\0';
            }
            lv2_atom_forge_pop(&self->forge, &sequence);
        } else {
            self->notify->atom.type = self->uris.sequence;
            self->notify->atom.size = 0;
        }
    }
}

static LV2_Worker_Status work(LV2_Handle instance,
                              LV2_Worker_Respond_Function respond,
                              LV2_Worker_Respond_Handle handle,
                              uint32_t size, const void* data)
{
    Plugin* self = instance;
    if (size != sizeof(Work)) {
        return LV2_WORKER_ERR_UNKNOWN;
    }
    const Work* request = data;
    if (request->release) {
        sample_free(request->sample);
        return LV2_WORKER_SUCCESS;
    }
    Result result = {0};
    result.pad = request->pad;
    result.generation = request->generation;
    memcpy(result.path, request->path, PATH_CAPACITY);
    if (request->path[0]) {
        result.sample = sample_load(request->path, result.error, sizeof(result.error));
        if (!result.sample) {
            report(self, result.error);
        }
    }
    const LV2_Worker_Status status = respond(handle, sizeof(result), &result);
    if (status != LV2_WORKER_SUCCESS) {
        sample_free(result.sample);
        atomic_store(&self->worker_failed[result.pad], 1);
        report(self, "Host rejected sample worker response");
    }
    return status;
}

static LV2_Worker_Status work_response(LV2_Handle instance, uint32_t size,
                                       const void* data)
{
    Plugin* self = instance;
    if (size != sizeof(Result)) {
        return LV2_WORKER_ERR_UNKNOWN;
    }
    const Result* result = data;
    if (result->pad >= PAD_COUNT) {
        return LV2_WORKER_ERR_UNKNOWN;
    }
    const unsigned pad = result->pad;
    self->pending[pad] = 0;
    if (result->generation != self->generation[pad]) {
        if (result->sample) {
            self->retired[self->retired_count++] = result->sample;
        }
        return LV2_WORKER_SUCCESS;
    }
    if (result->error[0]) {
        snprintf(self->error, sizeof(self->error), "Pad %02u: %s", pad + 1, result->error);
        self->changed[pad] = 1;
        return LV2_WORKER_SUCCESS;
    }
    if (self->pads[pad].sample) {
        self->retired[self->retired_count++] = self->pads[pad].sample;
    }
    self->pads[pad].sample = result->sample;
    stop_pad(self, pad);
    memcpy(self->paths[pad], result->path, PATH_CAPACITY);
    publish_path(self, pad);
    self->changed[pad] = 1;
    return LV2_WORKER_SUCCESS;
}

static const void* feature(const LV2_Feature* const* features, const char* uri)
{
    for (unsigned i = 0; features && features[i]; ++i) {
        if (!strcmp(features[i]->URI, uri)) {
            return features[i]->data;
        }
    }
    return NULL;
}

static void free_path(const LV2_Feature* const* features, char* path)
{
    const LV2_State_Free_Path* allocator = feature(features, LV2_STATE__freePath);
    if (allocator) {
        allocator->free_path(allocator->handle, path);
    } else {
        free(path);
    }
}

static LV2_State_Status save(LV2_Handle instance, LV2_State_Store_Function store,
                             LV2_State_Handle handle, uint32_t flags,
                             const LV2_Feature* const* features)
{
    (void)flags;
    Plugin* self = instance;
    const LV2_State_Map_Path* mapper = feature(features, LV2_STATE__mapPath);
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        char snapshot[PATH_CAPACITY];
        if (!snapshot_path(self, pad, snapshot)) {
            report(self, "Sample path changed during state save; retry save");
            return LV2_STATE_ERR_UNKNOWN;
        }
        char* mapped = snapshot[0] && mapper ?
                       mapper->abstract_path(mapper->handle, snapshot) : NULL;
        if (snapshot[0] && mapper && !mapped) {
            report(self, "Host could not map sample path for state save");
            return LV2_STATE_ERR_UNKNOWN;
        }
        const char* path = mapped ? mapped : snapshot;
        const LV2_State_Status status = store(
            handle, self->uris.sample[pad], path, strlen(path) + 1,
            self->uris.path, LV2_STATE_IS_POD |
            ((mapped || !path[0]) ? LV2_STATE_IS_PORTABLE : 0));
        if (mapped) {
            free_path(features, mapped);
        }
        if (status != LV2_STATE_SUCCESS) {
            return status;
        }
    }
    return LV2_STATE_SUCCESS;
}

static LV2_State_Status restore(LV2_Handle instance,
                                LV2_State_Retrieve_Function retrieve,
                                LV2_State_Handle handle, uint32_t flags,
                                const LV2_Feature* const* features)
{
    (void)flags;
    Plugin* self = instance;
    const LV2_State_Map_Path* mapper = feature(features, LV2_STATE__mapPath);
    Sample* samples[PAD_COUNT] = {0};
    char paths[PAD_COUNT][PATH_CAPACITY] = {{0}};
    LV2_State_Status status = LV2_STATE_SUCCESS;
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        size_t size = 0;
        uint32_t type = 0, value_flags = 0;
        const char* stored = retrieve(handle, self->uris.sample[pad],
                                      &size, &type, &value_flags);
        if (!stored) {
            continue;
        }
        if (type != self->uris.path || !size || stored[size - 1] ||
            memchr(stored, '\0', size - 1)) {
            status = LV2_STATE_ERR_BAD_TYPE;
            break;
        }
        char* mapped = stored[0] && mapper ?
                       mapper->absolute_path(mapper->handle, stored) : NULL;
        const char* path = mapped ? mapped : stored;
        if ((stored[0] && mapper && !mapped) || strlen(path) >= PATH_CAPACITY) {
            if (mapped) {
                free_path(features, mapped);
            }
            status = LV2_STATE_ERR_UNKNOWN;
            break;
        }
        memcpy(paths[pad], path, strlen(path) + 1);
        if (path[0]) {
            char error[160];
            samples[pad] = sample_load(path, error, sizeof(error));
            if (!samples[pad]) {
                report(self, error);
                status = LV2_STATE_ERR_UNKNOWN;
            }
        }
        if (mapped) {
            free_path(features, mapped);
        }
        if (status != LV2_STATE_SUCCESS) {
            break;
        }
    }
    if (status == LV2_STATE_SUCCESS) {
        for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
            sample_free(self->pads[pad].sample);
            self->pads[pad].sample = samples[pad];
            samples[pad] = NULL;
            stop_pad(self, pad);
            memcpy(self->paths[pad], paths[pad], PATH_CAPACITY);
            publish_path(self, pad);
            ++self->generation[pad];
            self->requested[pad] = 0;
            self->changed[pad] = 1;
        }
    } else {
        report(self, "Sample state restore failed; previous samples retained");
    }
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        sample_free(samples[pad]);
    }
    return status;
}

static void cleanup(LV2_Handle instance)
{
    Plugin* self = instance;
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        sample_free(self->pads[pad].sample);
    }
    for (unsigned i = 0; i < self->retired_count; ++i) {
        sample_free(self->retired[i]);
    }
    free(self);
}

static const void* extension_data(const char* uri)
{
    static const LV2_Worker_Interface worker = {work, work_response, NULL};
    static const LV2_State_Interface state = {save, restore};
    if (!strcmp(uri, LV2_WORKER__interface)) {
        return &worker;
    }
    if (!strcmp(uri, LV2_STATE__interface)) {
        return &state;
    }
    return NULL;
}

static const LV2_Descriptor descriptor = {
    SAMPLER_URI, instantiate, connect_port, activate, run, activate, cleanup, extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
