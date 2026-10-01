#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "plugin_api_v1.h"

extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);

int main(int argc, char **argv)
{
    plugin_api_v2_t *api;
    void *instance;
    char status[16] = {0};
    char value[4096] = {0};
    int attempts;

    assert(argc == 2);
    api = move_plugin_init_v2(NULL);
    assert(api != NULL);
    instance = api->create_instance(argv[1], NULL);
    assert(instance != NULL);
    assert(api->get_param(instance, "control_mode", value,
                          (int)sizeof(value)) >= 0);
    assert(strcmp(value, "2") == 0);
    assert(api->get_param(instance, "low_cut", value,
                          (int)sizeof(value)) >= 0);
    assert(strcmp(value, "180.0") == 0);
    assert(api->get_param(instance, "fader", value,
                          (int)sizeof(value)) >= 0);
    assert(strcmp(value, "1.000") == 0);
    api->set_param(instance, "fader", "0.25");
    assert(api->get_param(instance, "fader", value,
                          (int)sizeof(value)) >= 0);
    assert(strcmp(value, "0.250") == 0);
    api->set_param(instance, "fader", "2");
    assert(api->get_param(instance, "fader", value,
                          (int)sizeof(value)) >= 0);
    assert(strcmp(value, "1.000") == 0);
    assert(api->get_param(instance, "scratch_view_status", value,
                          (int)sizeof(value)) >= 0);
    assert(strstr(value, ",1.0000,") != NULL);

    for (attempts = 0; attempts < 250; ++attempts) {
        assert(api->get_param(instance, "load_status", status,
                              (int)sizeof(status)) >= 0);
        if (strcmp(status, "1") == 0)
            break;
        assert(strcmp(status, "-1") != 0);
        usleep(20000);
    }

    assert(strcmp(status, "1") == 0);
    api->destroy_instance(instance);
    puts("default sample: loaded bundled WAV");
    return 0;
}
