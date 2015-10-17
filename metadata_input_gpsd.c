#include <stdio.h>
#include <stdint.h>
#include <gps.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>

#include "metadata_exporter.h"
#include "metadata_input_gpsd.h"
#include "backend_event_loop.h"

static void md_input_gpsd_handle_event(void *ptr, int32_t fd, uint32_t events)
{
    struct md_input_gpsd *mig = ptr;
    struct md_gps_event gps_event;
    struct timeval tv;

    if (gps_read(&(mig->gps_data)) <= 0) {
        fprintf(stderr, "Failed to read data from GPS\n");
        return;
    }

    if (!(mig->gps_data.set & MODE_SET) ||
        mig->gps_data.fix.mode < 2)
        return;

    memset(&gps_event, 0, sizeof(struct md_gps_event));
    gettimeofday(&tv, NULL);
    gps_event.md_type = META_TYPE_POS;
    gps_event.minmea_id = MINMEA_UNKNOWN;
    gps_event.sequence = mde_inc_seq(mig->parent);
    gps_event.tstamp = tv.tv_sec;
    gps_event.latitude = mig->gps_data.fix.latitude;
    gps_event.longitude = mig->gps_data.fix.longitude;
    gps_event.satellites_tracked = mig->gps_data.satellites_visible;

    if (mig->gps_data.set & ALTITUDE_SET)
        gps_event.altitude = mig->gps_data.fix.altitude;

    if (mig->gps_data.set & SPEED_SET)
        gps_event.speed = mig->gps_data.fix.speed;

    mde_publish_event_obj(mig->parent, (struct md_event *) &gps_event);
}

static uint8_t md_gpsd_config(struct md_input_gpsd *mig,
                              const char *address,
                              const char *port)
{
    memset(&(mig->gps_data), 0, sizeof(struct gps_data_t));

    if (gps_open(address, port, &(mig->gps_data))) {
        fprintf(stderr, "GPS error: %s\n", gps_errstr(errno));
        return RETVAL_FAILURE;
    }

    if (gps_stream(&(mig->gps_data), WATCH_ENABLE | WATCH_JSON, NULL)) {
        fprintf(stderr, "GPS error: %s\n", gps_errstr(errno));
        return RETVAL_FAILURE;
    }

    if(!(mig->event_handle = backend_create_epoll_handle(mig,
                    mig->gps_data.gps_fd, md_input_gpsd_handle_event)))
        return RETVAL_FAILURE;
    
    backend_event_loop_update(mig->parent->event_loop, EPOLLIN, EPOLL_CTL_ADD,
        mig->gps_data.gps_fd, mig->event_handle);

    return RETVAL_SUCCESS;
}

static uint8_t md_input_gpsd_init(void *ptr, int argc, char *argv[])
{
    struct md_input_gpsd *mig = ptr;
    const char *address = NULL, *port = NULL;
    int c, option_index = 0;

    static struct option gpsd_options[] = {
        {"gpsd_address",         required_argument,  0,  0},
        {"gpsd_port",            required_argument,  0,  0},
        {0,                                      0,  0,  0}};

    while (1) {
        //No permuting of array here as well
        c = getopt_long_only(argc, argv, "--", gpsd_options, &option_index);

        if (c == -1)
            break;
        else if (c)
            continue;

        if (!strcmp(gpsd_options[option_index].name, "gpsd_address"))
            address = optarg;
        else if (!strcmp(gpsd_options[option_index].name, "gpsd_port"))
            port = optarg;
    }

    if (address == NULL || port == NULL) {
        fprintf(stderr, "Missing required GPSD argument\n");
        return RETVAL_FAILURE;
    }

    return md_gpsd_config(mig, address, port);
}

static void md_gpsd_usage()
{
    fprintf(stderr, "GPSD input:\n");
    fprintf(stderr, "--gpsd_address: gpsd address (r)\n");
    fprintf(stderr, "--gpsd_port: gpsd port (r)\n");
}

void md_gpsd_setup(struct md_exporter *mde, struct md_input_gpsd *mig)
{
    mig->parent = mde;
    mig->init = md_input_gpsd_init;
    mig->usage = md_gpsd_usage;
}