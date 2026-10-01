#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "plugin_api_v1.h"

extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);

enum { INSTANCE_COUNT = 8 };
static plugin_api_v2_t *api;
static const char *module_dir;
static atomic_int start;
static void *instances[INSTANCE_COUNT];

static void *create(void *opaque)
{
    void **instance = opaque;
    while (!atomic_load(&start))
        usleep(100);
    *instance = api->create_instance(module_dir, NULL);
    assert(*instance);
    return NULL;
}

static void wait_ready(void *instance)
{
    char status[16];
    for (int attempt = 0; attempt < 500; ++attempt) {
        assert(api->get_param(instance, "load_status", status, sizeof(status)) >= 0);
        if (!strcmp(status, "1"))
            return;
        assert(strcmp(status, "-1"));
        usleep(20000);
    }
    assert(!"instance failed to finish initialization");
}

int main(int argc, char **argv)
{
    pthread_t threads[INSTANCE_COUNT];
    assert(argc == 2);
    module_dir = argv[1];
    api = move_plugin_init_v2(NULL);
    assert(api);
    for (int i = 0; i < INSTANCE_COUNT; ++i)
        assert(!pthread_create(&threads[i], NULL, create, &instances[i]));
    atomic_store(&start, 1);
    for (int i = 0; i < INSTANCE_COUNT; ++i) {
        assert(!pthread_join(threads[i], NULL));
        wait_ready(instances[i]);
    }
    /* Destroying one decoder must not invalidate the other instances' table.
     * Recreating slots also exercises reuse of the already-built definition. */
    for (int i = 0; i < INSTANCE_COUNT; ++i) {
        api->destroy_instance(instances[i]);
        instances[i] = api->create_instance(module_dir, NULL);
        assert(instances[i]);
        wait_ready(instances[i]);
    }
    for (int i = 0; i < INSTANCE_COUNT; ++i)
        api->destroy_instance(instances[i]);
    puts("multi-instance: concurrent startup and recreation passed");
    return 0;
}
