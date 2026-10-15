#include "../src/plugin.c"

#include <assert.h>

#ifdef TEST_LILV_STATE
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <lilv/lilv.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef union {
    uint64_t align;
    uint8_t bytes[8192];
} Buffer;

typedef struct {
    char names[128][160];
    unsigned count;
    Plugin* plugin;
    Work jobs[128];
    unsigned job_count;
    Result results[128];
    unsigned result_count;
    int reject_schedule;
    int reject_response;
    int synchronous;
    char state[PAD_COUNT][PATH_CAPACITY];
    uint32_t state_type[PAD_COUNT];
    unsigned stored_count;
} Host;

static LV2_URID map_uri(LV2_URID_Map_Handle handle, const char* uri)
{
    Host* host = handle;
    for (unsigned i = 0; i < host->count; ++i) {
        if (!strcmp(host->names[i], uri)) {
            return i + 1;
        }
    }
    assert(host->count < 128 && strlen(uri) < sizeof(host->names[0]));
    strcpy(host->names[host->count++], uri);
    return host->count;
}

static LV2_Worker_Status respond(LV2_Worker_Respond_Handle handle,
                                 uint32_t size, const void* data)
{
    Host* host = handle;
    assert(size == sizeof(Result));
    if (host->reject_response) {
        return LV2_WORKER_ERR_NO_SPACE;
    }
    if (host->synchronous) {
        return work_response(host->plugin, size, data);
    }
    assert(host->result_count < 128);
    memcpy(&host->results[host->result_count++], data, size);
    return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status enqueue(LV2_Worker_Schedule_Handle handle,
                                 uint32_t size, const void* data)
{
    Host* host = handle;
    assert(size == sizeof(Work));
    if (host->reject_schedule) {
        return LV2_WORKER_ERR_NO_SPACE;
    }
    if (host->synchronous) {
        return work(host->plugin, respond, host, size, data);
    }
    assert(host->job_count < 128);
    memcpy(&host->jobs[host->job_count++], data, size);
    return LV2_WORKER_SUCCESS;
}

static void pump(Host* host)
{
    for (unsigned i = 0; i < host->job_count; ++i) {
        assert(work(host->plugin, respond, host, sizeof(Work), &host->jobs[i])
               == LV2_WORKER_SUCCESS);
    }
    host->job_count = 0;
    for (unsigned i = 0; i < host->result_count; ++i) {
        assert(work_response(host->plugin, sizeof(Result), &host->results[i])
               == LV2_WORKER_SUCCESS);
    }
    host->result_count = 0;
}

static void empty(Buffer* input, LV2_URID_Map* map)
{
    LV2_Atom_Forge forge;
    LV2_Atom_Forge_Frame sequence;
    lv2_atom_forge_init(&forge, map);
    lv2_atom_forge_set_buffer(&forge, input->bytes, sizeof(input->bytes));
    lv2_atom_forge_sequence_head(&forge, &sequence, 0);
    lv2_atom_forge_pop(&forge, &sequence);
}

static void block(Host* host, Buffer* output, uint32_t frames)
{
    ((LV2_Atom_Sequence*)output->bytes)->atom.size = sizeof(output->bytes);
    run(host->plugin, frames);
}

static void patch(Buffer* input, LV2_URID_Map* map, Plugin* plugin,
                  unsigned pad, const char* path)
{
    LV2_Atom_Forge forge;
    LV2_Atom_Forge_Frame sequence, object;
    lv2_atom_forge_init(&forge, map);
    lv2_atom_forge_set_buffer(&forge, input->bytes, sizeof(input->bytes));
    lv2_atom_forge_sequence_head(&forge, &sequence, 0);
    lv2_atom_forge_frame_time(&forge, 0);
    lv2_atom_forge_object(&forge, &object, 0, plugin->uris.set);
    lv2_atom_forge_key(&forge, plugin->uris.property);
    lv2_atom_forge_urid(&forge, plugin->uris.sample[pad]);
    lv2_atom_forge_key(&forge, plugin->uris.value);
    lv2_atom_forge_path(&forge, path, (uint32_t)strlen(path));
    lv2_atom_forge_pop(&forge, &object);
    lv2_atom_forge_pop(&forge, &sequence);
}

static void midi_block(Buffer* input, LV2_URID_Map* map, Plugin* plugin,
                       uint32_t on_time, const uint8_t on[3],
                       uint32_t off_time, const uint8_t off[3])
{
    LV2_Atom_Forge forge;
    LV2_Atom_Forge_Frame sequence;
    lv2_atom_forge_init(&forge, map);
    lv2_atom_forge_set_buffer(&forge, input->bytes, sizeof(input->bytes));
    lv2_atom_forge_sequence_head(&forge, &sequence, 0);
    lv2_atom_forge_frame_time(&forge, on_time);
    lv2_atom_forge_atom(&forge, 3, plugin->uris.midi);
    lv2_atom_forge_write(&forge, on, 3);
    if (off) {
        lv2_atom_forge_frame_time(&forge, off_time);
        lv2_atom_forge_atom(&forge, 3, plugin->uris.midi);
        lv2_atom_forge_write(&forge, off, 3);
    }
    lv2_atom_forge_pop(&forge, &sequence);
}

static LV2_State_Status store(LV2_State_Handle handle, uint32_t key,
                              const void* value, size_t size, uint32_t type,
                              uint32_t flags)
{
    Host* host = handle;
    (void)flags;
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        if (key == host->plugin->uris.sample[pad]) {
            assert(size <= PATH_CAPACITY);
            memcpy(host->state[pad], value, size);
            host->state_type[pad] = type;
            ++host->stored_count;
            return LV2_STATE_SUCCESS;
        }
    }
    return LV2_STATE_ERR_UNKNOWN;
}

static const void* retrieve(LV2_State_Handle handle, uint32_t key, size_t* size,
                            uint32_t* type, uint32_t* flags)
{
    Host* host = handle;
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        if (key == host->plugin->uris.sample[pad]) {
            if (!host->state_type[pad]) {
                return NULL;
            }
            *size = strlen(host->state[pad]) + 1;
            *type = host->state_type[pad];
            *flags = LV2_STATE_IS_POD;
            return host->state[pad];
        }
    }
    return NULL;
}

static void near(float value, float expected)
{
    assert(fabsf(value - expected) < 0.0001f);
}

static void fixture(void)
{
    static const uint8_t wav[] = {
        'R','I','F','F', 52,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
        0x80,0xbb,0,0, 0,0x77,1,0, 2,0,16,0,
        'd','a','t','a', 16,0,0,0,
        0,0x40, 0,0x40, 0,0x40, 0,0x40,
        0,0x40, 0,0x40, 0,0x40, 0,0x40
    };
    FILE* file = fopen("build/test-plugin.wav", "wb");
    assert(file);
    assert(fwrite(wav, 1, sizeof(wav), file) == sizeof(wav));
    assert(!fclose(file));
}

#ifdef TEST_LILV_STATE
static const char* unmap_uri(LV2_URID_Unmap_Handle handle, LV2_URID id)
{
    const Host* host = handle;
    assert(id && id <= host->count);
    return host->names[id - 1];
}

static void disk_state_roundtrip(void)
{
    Host host = {0};
    LV2_URID_Map map = {&host, map_uri};
    LV2_URID_Unmap unmap = {&host, unmap_uri};
    LV2_Worker_Schedule worker = {&host, enqueue};
    const LV2_Feature map_feature = {LV2_URID__map, &map};
    const LV2_Feature worker_feature = {LV2_WORKER__schedule, &worker};
    const LV2_Feature* features[] = {&map_feature, &worker_feature, NULL};
    char cwd[PATH_CAPACITY], bundle[PATH_CAPACITY * 2];
    char sample[PATH_CAPACITY * 2], directory[PATH_CAPACITY * 2];
    assert(getcwd(cwd, sizeof(cwd)));
    snprintf(bundle, sizeof(bundle), "%s/build/state-test/16-pads.lv2/", cwd);
    snprintf(sample, sizeof(sample), "%s/build/test-plugin.wav", cwd);
    assert(strlen(sample) < PATH_CAPACITY);
    snprintf(directory, sizeof(directory), "%s/build/state-test/session-%ld",
             cwd, (long)getpid());
    assert(!mkdir(directory, 0700));
    LilvWorld* world = lilv_world_new();
    assert(world);
    LilvNode* bundle_uri = lilv_new_file_uri(world, NULL, bundle);
    lilv_world_load_bundle(world, bundle_uri);
    LilvNode* uri = lilv_new_uri(world, SAMPLER_URI);
    const LilvPlugin* plugin = lilv_plugins_get_by_uri(
        lilv_world_get_all_plugins(world), uri);
    assert(plugin);
    Buffer input = {0}, output = {0};
    float left[16], right[16], triggers[PAD_COUNT] = {0};
    host.synchronous = 1;

    /* Sparse and empty states must survive disk serialization, a fresh
       instance, and restoration over an existing assignment. */
    for (unsigned snapshot = 0; snapshot < 3; ++snapshot) {
        char state_dir[PATH_CAPACITY * 3], state_file[PATH_CAPACITY * 3 + 32];
        snprintf(state_dir, sizeof(state_dir), "%s/snapshot-%u", directory, snapshot);
        assert(!mkdir(state_dir, 0700));
        snprintf(state_file, sizeof(state_file), "%s/effect.ttl", state_dir);
        LilvInstance* instance = lilv_plugin_instantiate(plugin, 48000, features);
        assert(instance);
        host.plugin = lilv_instance_get_handle(instance);
        host.plugin->events = (const LV2_Atom_Sequence*)input.bytes;
        host.plugin->notify = (LV2_Atom_Sequence*)output.bytes;
        host.plugin->left = left;
        host.plugin->right = right;
        for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
            const int assigned = snapshot == 0 ? pad == 0 || pad == 15 :
                                 snapshot == 1 && pad == 7;
            if (assigned) {
                patch(&input, &map, host.plugin, pad, sample);
                block(&host, &output, 16);
                assert(host.plugin->pads[pad].sample);
            }
        }
        LilvState* state = lilv_state_new_from_instance(
            plugin, instance, &map, NULL, state_dir, state_dir, state_dir,
            NULL, NULL, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, features);
        assert(state);
        assert(!lilv_state_save(world, &map, &unmap, state, NULL,
                               state_dir, "effect.ttl"));
        lilv_state_free(state);
        lilv_instance_free(instance);

        state = lilv_state_new_from_file(world, &map, NULL, state_file);
        assert(state);
        instance = lilv_plugin_instantiate(plugin, 48000, features);
        assert(instance);
        host.plugin = lilv_instance_get_handle(instance);
        host.plugin->events = (const LV2_Atom_Sequence*)input.bytes;
        host.plugin->notify = (LV2_Atom_Sequence*)output.bytes;
        host.plugin->left = left;
        host.plugin->right = right;
        /* A missing property must clear a previously assigned pad too. */
        patch(&input, &map, host.plugin, 3, sample);
        block(&host, &output, 16);
        assert(host.plugin->pads[3].sample);
        lilv_state_restore(state, instance, NULL, NULL,
                           LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, features);
        empty(&input, &map);
        for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
            const int assigned = snapshot == 0 ? pad == 0 || pad == 15 :
                                 snapshot == 1 && pad == 7;
            assert(!!host.plugin->pads[pad].sample == assigned);
            assert(!!host.plugin->paths[pad][0] == assigned);
            host.plugin->controls[pad][CONTROL_TRIGGER] = &triggers[pad];
            triggers[pad] = 1;
            block(&host, &output, 16);
            near(left[0], assigned ? 0.5f : 0);
            triggers[pad] = 0;
            block(&host, &output, 16);
        }
        lilv_instance_free(instance);
        assert(!lilv_state_delete(world, state));
        lilv_state_free(state);
    }
    assert(!rmdir(directory));
    lilv_node_free(uri);
    lilv_node_free(bundle_uri);
    lilv_world_free(world);
    puts("Lilv disk state/re-instantiation tests passed");
}
#endif

int main(void)
{
    Host host = {0};
    LV2_URID_Map mapper = {&host, map_uri};
    LV2_Worker_Schedule worker = {&host, enqueue};
    const LV2_Feature map_feature = {LV2_URID__map, &mapper};
    const LV2_Feature worker_feature = {LV2_WORKER__schedule, &worker};
    const LV2_Feature* features[] = {&map_feature, &worker_feature, NULL};
    assert(lv2_descriptor(0) && !lv2_descriptor(1));
    assert(!instantiate(&descriptor, 48000, "", NULL));
    host.plugin = instantiate(&descriptor, 48000, "", features);
    assert(host.plugin);
    Plugin* plugin = host.plugin;
    Buffer input = {0}, output = {0};
    float left[32], right[32], controls[PAD_COUNT][CONTROL_COUNT] = {{0}};
    float activity[PAD_COUNT] = {0};
    float meters[2] = {0};
    connect_port(plugin, 0, input.bytes);
    connect_port(plugin, 1, output.bytes);
    connect_port(plugin, 2, left);
    connect_port(plugin, 3, right);
    assert(METER_PORT == 68);
    connect_port(plugin, 68, &meters[0]);
    connect_port(plugin, 69, &meters[1]);
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        for (unsigned parameter = 0; parameter < CONTROL_COUNT; ++parameter) {
            connect_port(plugin, 4 + pad * 4 + parameter, &controls[pad][parameter]);
        }
        connect_port(plugin, 4 + pad * 4 + 3, &activity[pad]);
    }
    activate(plugin);
    fixture();
    patch(&input, &mapper, plugin, 0, "build/test-plugin.wav");
    block(&host, &output, 16);
    assert(plugin->pending[0] && !plugin->pads[0].sample);
    empty(&input, &mapper);
    pump(&host);
    assert(plugin->pads[0].sample && !plugin->pending[0]);
    /* Raw MIDI notes/CCs no longer trigger pads; MODEP maps trigger ports. */
    const uint8_t on[] = {0x90, 60, 127}, off[] = {0x80, 60, 0};
    const uint8_t cc_on[] = {0xb0, 21, 127};
    midi_block(&input, &mapper, plugin, 2, on, 5, off);
    block(&host, &output, 16);
    near(left[2], 0);
    midi_block(&input, &mapper, plugin, 2, cc_on, 0, NULL);
    block(&host, &output, 16);
    near(left[2], 0);
    near(activity[0], 0);
    empty(&input, &mapper);

    controls[0][CONTROL_MODE] = PAD_ONE_SHOT;
    controls[0][CONTROL_TRIGGER] = 1;
    block(&host, &output, 4);
    near(left[0], 0.5f);
    controls[0][CONTROL_TRIGGER] = 0;
    block(&host, &output, 16);
    near(left[3], 0.5f); /* One-shot ignores release. */
    near(left[4], 0);

    controls[0][CONTROL_MODE] = PAD_TRIG;
    controls[0][CONTROL_TRIGGER] = 1;
    block(&host, &output, 4);
    near(left[0], 0.5f);
    near(activity[0], 1);
    controls[0][CONTROL_TRIGGER] = 0;
    block(&host, &output, 4);
    near(left[0], 0);
    near(activity[0], 0);
    controls[0][CONTROL_TRIGGER] = 1;
    block(&host, &output, 16);
    near(left[15], 0);
    near(activity[0], 1); /* Trig blinks until release, even after sample end. */
    controls[0][CONTROL_TRIGGER] = 0;
    block(&host, &output, 4);
    near(activity[0], 0);

    controls[0][CONTROL_MODE] = PAD_LOOP;
    controls[0][CONTROL_TRIGGER] = 1;
    block(&host, &output, 16);
    near(left[15], 0.5f);
    near(activity[0], 1);
    controls[0][CONTROL_TRIGGER] = 0;
    block(&host, &output, 16);
    near(left[15], 0.5f);
    near(activity[0], 1);
    controls[0][CONTROL_TRIGGER] = 1;
    block(&host, &output, 16);
    near(left[0], 0); /* Retrigger stops without restarting. */
    near(activity[0], 0);
    controls[0][CONTROL_TRIGGER] = 0;
    block(&host, &output, 4);

    /* CC 123 remains a sample-accurate global panic. */
    controls[0][CONTROL_TRIGGER] = 1;
    block(&host, &output, 16);
    controls[0][CONTROL_TRIGGER] = 0;
    const uint8_t panic[] = {0xb0, 123, 0};
    midi_block(&input, &mapper, plugin, 3, panic, 0, NULL);
    block(&host, &output, 16);
    near(left[2], 0.5f);
    near(left[3], 0);
    near(activity[0], 0);
    empty(&input, &mapper);
    activate(plugin);

    /* All sixteen independent voices can run at once. */
    for (unsigned pad = 1; pad < PAD_COUNT; ++pad) {
        patch(&input, &mapper, plugin, pad, "build/test-plugin.wav");
        block(&host, &output, 16);
        pump(&host);
    }
    empty(&input, &mapper);
    controls[0][CONTROL_MODE] = PAD_ONE_SHOT;
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        controls[pad][CONTROL_TRIGGER] = 1;
    }
    block(&host, &output, 16);
    near(left[0], 8);
    near(right[0], 8);
    near(meters[0], 20 * log10f(8));
    near(meters[1], meters[0]);
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        controls[pad][CONTROL_TRIGGER] = 0;
    }
    block(&host, &output, 16);

    Sample* previous = plugin->pads[0].sample;
    patch(&input, &mapper, plugin, 0, "build/no-such-file.wav");
    block(&host, &output, 16);
    pump(&host);
    assert(plugin->pads[0].sample == previous);
    assert(strstr(plugin->error, "Cannot open"));
    assert(save(plugin, store, &host, 0, features) == LV2_STATE_SUCCESS);
    assert(host.stored_count == 16);
    assert(!strcmp(host.state[0], "build/test-plugin.wav"));
    assert(restore(plugin, retrieve, &host, 0, features) == LV2_STATE_SUCCESS);
    strcpy(host.state[15], "build/no-such-file.wav");
    previous = plugin->pads[0].sample;
    assert(restore(plugin, retrieve, &host, 0, features) != LV2_STATE_SUCCESS);
    assert(plugin->pads[0].sample == previous);

    /* Superseded loads must never replace a newer selection. */
    patch(&input, &mapper, plugin, 0, "build/test-plugin.wav");
    block(&host, &output, 16);
    patch(&input, &mapper, plugin, 0, "");
    block(&host, &output, 16);
    pump(&host);
    assert(plugin->pads[0].sample == previous);
    empty(&input, &mapper);
    block(&host, &output, 16);
    pump(&host);
    assert(!plugin->pads[0].sample);

    host.reject_schedule = 1;
    patch(&input, &mapper, plugin, 0, "build/test-plugin.wav");
    block(&host, &output, 16);
    assert(plugin->requested[0] && !plugin->pending[0]);
    host.reject_schedule = 0;
    host.synchronous = 1;
    empty(&input, &mapper);
    block(&host, &output, 16);
    assert(plugin->pads[0].sample && !plugin->pending[0]);
    host.synchronous = 0;
    host.reject_response = 1;
    patch(&input, &mapper, plugin, 0, "build/test-plugin.wav");
    block(&host, &output, 16);
    assert(host.job_count == 1);
    assert(work(plugin, respond, &host, sizeof(Work), &host.jobs[0])
           == LV2_WORKER_ERR_NO_SPACE);
    host.job_count = 0;
    host.reject_response = 0;
    empty(&input, &mapper);
    block(&host, &output, 16);
    assert(!plugin->pending[0]);

    /* Malformed atoms and a small notification buffer must not overflow. */
    patch(&input, &mapper, plugin, 0, "build/test-plugin.wav");
    LV2_Atom_Event* malformed = (LV2_Atom_Event*)(input.bytes + sizeof(LV2_Atom_Sequence));
    malformed->body.size = UINT32_MAX;
    block(&host, &output, 16);
    empty(&input, &mapper);
    memset(output.bytes, 0xa5, sizeof(output.bytes));
    ((LV2_Atom_Sequence*)output.bytes)->atom.size = 32;
    run(plugin, 16);
    for (unsigned i = 32; i < sizeof(output.bytes); ++i) {
        assert(output.bytes[i] == 0xa5);
    }
    for (unsigned pad = 0; pad < PAD_COUNT; ++pad) {
        assert(!plugin->pending[pad]);
    }
    block(&host, &output, 16);
    pump(&host);
    activate(plugin);
    empty(&input, &mapper);
    block(&host, &output, 16);
    near(meters[0], -60);
    near(meters[1], -60);
    float stereo_audio[] = {0.5f, 0.25f, 0.5f, 0.25f};
    Sample stereo = {stereo_audio, 2, 48000, 2};
    Sample* retained = plugin->pads[0].sample;
    plugin->pads[0].sample = &stereo;
    pad_trigger(&plugin->pads[0], 1);
    block(&host, &output, 16);
    near(meters[0], 20 * log10f(0.5f));
    near(meters[1], 20 * log10f(0.25f));
    const float previous_level = meters[0];
    block(&host, &output, 16);
    assert(meters[0] < previous_level && meters[0] > -60);
    plugin->pads[0].sample = retained;
    cleanup(plugin);
#ifdef TEST_LILV_STATE
    disk_state_roundtrip();
#endif
    assert(!remove("build/test-plugin.wav"));
    puts("LV2 host harness tests passed");
    return 0;
}
