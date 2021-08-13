/**
 * @file main.cpp
 * @author Mit Bailey (mitbailey99@gmail.com)
 * @brief Designed to aim the SPACE-HAUC Ground Station communications dish at the satellite as it passes overhead.
 * @version See Git tags for version information.
 * @date 2021.08.12
 * 
 * Uses Daniel Warner's sgp4 repository (https://github.com/dnwrnr/sgp4) as a driver.
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include <unistd.h>
#include <string.h>
#include "CoordGeodetic.h"
#include "CoordTopocentric.h"
#include "Observer.h"
#include "SGP4.h"
#include "meb_debug.h"
#include "track.hpp"
#include "network.hpp"

int main(int argc, char *argv[])
{
    global_data_t global_data[1] = {0};
    network_data_init(global_data->network_data, SERVER_PORT);
    global_data->network_data->rx_active = true;
    global_data->network_data->thread_status = 1;

    strcpy(global_data->devname, "/dev/ttyUSB0");

    if (argc > 2)
    {
        dbprintlf(FATAL "Invalid number of command-line arguments given.");
        return -1;
    }
    else if (argc == 2)
    {
        strcpy(global_data->devname, argv[1]);
    }

    pthread_t net_polling_tid, net_rx_tid, tracking_tid;

    while (global_data->network_data->thread_status > -1)
    {
        pthread_create(&net_polling_tid, NULL, gs_polling_thread, &global_data->network_data);
        pthread_create(&net_rx_tid, NULL, gs_network_rx_thread, global_data);
        pthread_create(&tracking_tid, NULL, tracking_thread, global_data);

        void *thread_return;
        pthread_join(net_polling_tid, &thread_return);
        pthread_join(net_rx_tid, &thread_return);
        pthread_join(tracking_tid, &thread_return);
    }

    // Finished.
    void *thread_return;
    pthread_cancel(net_polling_tid);
    pthread_cancel(net_rx_tid);
    pthread_cancel(tracking_tid);
    thread_return == PTHREAD_CANCELED ? printf("Good net_polling_tid join.\n") : printf("Bad net_polling_tid join.\n");
    pthread_join(net_rx_tid, &thread_return);
    thread_return == PTHREAD_CANCELED ? printf("Good net_rx_tid join.\n") : printf("Bad net_rx_tid join.\n");
    pthread_join(tracking_tid, &thread_return);
    thread_return == PTHREAD_CANCELED ? printf("Good tracking_tid join.\n") : printf("Bad tracking_tid join.\n");

    close(global_data->network_data->socket);

    return global_data->network_data->thread_status;

    
    return 1;
}