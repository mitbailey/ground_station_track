/**
 * @file track.cpp
 * @author Mit Bailey (mitbailey99@gmail.com)
 * @brief 
 * @version See Git tags for version information.
 * @date 2021.08.12
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "DateTime.h"
#include "Observer.h"
#include "SGP4.h"
#include "meb_debug.h"
#include "track.hpp"
#include "network.hpp"

int open_connection(char *devname)
{
    if (devname == NULL)
    {
        dbprintlf(RED_FG "Device name null.");
        return -1;
    }

    int connection = open(devname, O_RDWR | O_NOCTTY | O_NDELAY);
    if (connection < 3)
    {
        dbprintlf(RED_FG "Unable to open serial connection.");
        return -1;
    }

    struct termios options[1];
    tcgetattr(connection, options);
    options->c_cflag = B2400 | CS8 | CLOCAL | CREAD;
    options->c_iflag = IGNPAR;
    options->c_oflag = 0;
    options->c_lflag = 0;
    tcflush(connection, TCIFLUSH);
    tcsetattr(connection, TCSANOW, options);

    return connection;
}

int aim_azimuth(int connection, double azimuth)
{
    // Command the dish manuever azimuth.
    const int command_size = 0x10;
    char command[command_size];
    snprintf(command, command_size, "PB %d\r", (int)(azimuth DEG));

    dbprintlf(BLUE_FG "COMMANDING AZ: %s", command);

    ssize_t az_bytes = 0;
    // ssize_t az_bytes = write(connection, command, command_size);
    if (az_bytes != command_size)
    {
        dbprintlf(RED_FG "Error sending azimuth adjustment command to positioner (%d).", az_bytes);
        return -1;
    }

    return 1;
}

int aim_elevation(int connection, double elevation)
{
    // Command the dish manuever elevation.
    const int command_size = 0x10;
    char command[command_size];
    snprintf(command, command_size, "PB %d\r", (int)(elevation DEG));

    dbprintlf(BLUE_FG "COMMANDING EL: %s", command);

    ssize_t el_bytes = 0;
    // ssize_t el_bytes = write(connection, command, command_size);
    if (el_bytes != command_size)
    {
        dbprintlf(RED_FG "Error sending elevation adjustment command to positioner (%d).", el_bytes);
        return -1;
    }

    return 1;
}

CoordTopocentric find_next_targetrise(SGP4 *target, Observer *dish)
{
    DateTime time(DateTime::Now(true));
    while (dish->GetLookAngle(target->FindPosition(DateTime(time))).elevation < 0.f)
    {
        time = time + TimeSpan(0, 1, 0);
    }

    return dish->GetLookAngle(target->FindPosition(DateTime(time)));
}

void *tracking_thread(void *args)
{
    dbprintlf(GREEN_FG "TRACKING THREAD STARTING");

    global_data_t *global = (global_data_t *)args;

    while (global->connection < 3)
    {
        // Open a connection to the dish controller.
        global->connection = open_connection(global->devname);

        if (global->connection < 3)
        {
            dbprintlf(RED_FG "Device not found.");
            usleep(5 SEC);
        }
    }

    SGP4 *target = new SGP4(Tle(TLE[0], TLE[1]));
    Observer *dish = new Observer(GS_LAT, GS_LON, ELEV);
    CoordTopocentric ideal;

    // double current_azimuth = 0;
    // double current_elevation = 0;

    for (;;)
    {
        // Establish if the target is visible.
        if (dish->GetLookAngle(target->FindPosition(DateTime::Now(true))).elevation > 0.f)
        { // The target is visible.
            // Find the angle to the target.
            ideal = dish->GetLookAngle(target->FindPosition(DateTime::Now(true)));
        }
        else
        { // The target is not visible.
            // Find the angle to the next targetrise.
            dbprintlf(BLUE_FG "TARGET NOT VISIBLE");
            ideal = dish->GetLookAngle(target->FindPosition(DateTime::Now(true)));
            // ideal = find_next_targetrise(target, dish);
        }

        // NOTE: Assume the current azimuth and elevation is whatever we last told it to be at.

        // Find the difference between ideal and actual angles. If we are off from ideal by >1 degree, aim at the ideal.
        if (ideal.azimuth DEG - global->AzEl[0] < -1 || ideal.azimuth DEG - global->AzEl[0] > 1)
        {
            aim_azimuth(global->connection, ideal.azimuth DEG);
            global->AzEl[0] = ideal.azimuth DEG;

            // Send our updated coordinates.
            NetFrame *network_frame = new NetFrame((unsigned char *)global->AzEl, sizeof(global->AzEl), NetType::TRACKING_DATA, NetVertex::CLIENT);
            network_frame->sendFrame(global->network_data);
            delete network_frame;
        }

        if (ideal.elevation DEG - global->AzEl[1] < -1 || ideal.elevation DEG - global->AzEl[1] > 1)
        {
            aim_elevation(global->connection, ideal.elevation DEG);
            global->AzEl[1] = ideal.elevation DEG;

            // Send our updated coordinates.
            NetFrame *network_frame = new NetFrame((unsigned char *)global->AzEl, sizeof(global->AzEl), NetType::TRACKING_DATA, NetVertex::CLIENT);
            network_frame->sendFrame(global->network_data);
            delete network_frame;
        }

        dbprintlf(GREEN_FG "CURRENT AZEL: %d:%d", global->AzEl[0], global->AzEl[1]);

        usleep(1 SEC);
    }

    delete dish;
    delete target;
    close(global->connection);

    dbprintlf(RED_BG "TRACKING THREAD EXITING");
    if (global->network_data->thread_status > 0)
    {
        global->network_data->thread_status = 0;
    }
    return NULL;
}

void *gs_network_rx_thread(void *args)
{
    dbprintlf(GREEN_FG "GS NETWORK RX THREAD STARTING");

    global_data_t *global = (global_data_t *)args;
    NetDataClient *network_data = global->network_data;

    // Similar, if not identical, to the network functionality in ground_station.
    // Roof UHF is a network client to the GS Server, and so should be very similar in socketry to ground_station.

    while (network_data->recv_active && global->network_data->thread_status > 0)
    {
        if (!network_data->connection_ready)
        {
            usleep(5 SEC);
            continue;
        }

        int read_size = 0;

        while (read_size >= 0 && network_data->recv_active)
        {
            char buffer[sizeof(NetFrame) * 2];
            memset(buffer, 0x0, sizeof(buffer));

            dbprintlf(BLUE_BG "Waiting to receive...");
            read_size = recv(network_data->socket, buffer, sizeof(buffer), 0);
            dbprintlf("Read %d bytes.", read_size);

            if (read_size > 0)
            {
                dbprintf("RECEIVED (hex): ");
                for (int i = 0; i < read_size; i++)
                {
                    printf("%02x", buffer[i]);
                }
                printf("(END)\n");

                // Parse the data by mapping it to a NetworkFrame.
                NetFrame *network_frame = (NetFrame *)buffer;

                // Check if we've received data in the form of a NetworkFrame.
                if (network_frame->validate() < 0)
                {
                    dbprintlf("Integrity check failed (%d).", network_frame->validate());
                    continue;
                }
                dbprintlf("Integrity check successful.");

                global->netstat = network_frame->getNetstat();

                // For now, just print the Netstat.
                uint8_t netstat = network_frame->getNetstat();
                dbprintlf(BLUE_FG "NETWORK STATUS (%d)", netstat);
                dbprintf("GUI Client ----- ");
                ((netstat & 0x80) == 0x80) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Roof UHF ------- ");
                ((netstat & 0x40) == 0x40) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Roof X-Band ---- ");
                ((netstat & 0x20) == 0x20) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Haystack ------- ");
                ((netstat & 0x10) == 0x10) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Track ---------- ");
                ((netstat & 0x8) == 0x8) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");

                // Extract the payload into a buffer.
                int payload_size = network_frame->getPayloadSize();
                unsigned char *payload = (unsigned char *)malloc(payload_size);
                if (payload == NULL)
                {
                    dbprintlf(FATAL "Memory for payload failed to allocate, packet lost.");
                    continue;
                }

                if (network_frame->retrievePayload(payload, payload_size) < 0)
                {
                    dbprintlf(RED_FG "Error retrieving data.");
                    if (payload != NULL)
                    {
                        free(payload);
                        payload = NULL;
                    }
                    continue;
                }

                switch (network_frame->getType())
                {
                case NetType::TRACKING_COMMAND:
                {
                    dbprintlf(BLUE_FG "Received a tracking command.");

                    double AzEl[2] = {0};
                    network_frame->retrievePayload((unsigned char *)AzEl, sizeof(AzEl));

                    dbprintlf(BLUE_FG "Setting AzEl to: %d, %d", AzEl[0], AzEl[1]);

                    aim_azimuth(global->connection, AzEl[0]);
                    global->AzEl[0] = AzEl[0];
                    aim_azimuth(global->connection, AzEl[1]);
                    global->AzEl[1] = AzEl[1];

                    // Send our updated coordinates.
                    NetFrame *network_frame = new NetFrame((unsigned char *)global->AzEl, sizeof(global->AzEl), NetType::TRACKING_DATA, NetVertex::CLIENT);
                    network_frame->sendFrame(global->network_data);
                    delete network_frame;

                    break;
                }
                case NetType::ACK:
                {
                    break;
                }
                case NetType::NACK:
                {
                    break;
                }
                default:
                {
                    break;
                }
                }
                if (payload != NULL)
                {
                    free(payload);
                    payload = NULL;
                }
            }
            else
            {
                break;
            }
        }
        if (read_size == 0)
        {
            dbprintlf(RED_BG "Connection forcibly closed by the server.");
            strcpy(network_data->disconnect_reason, "SERVER-FORCED");
            network_data->connection_ready = false;
            continue;
        }
        else if (errno == EAGAIN)
        {
            dbprintlf(YELLOW_BG "Active connection timed-out (%d).", read_size);
            strcpy(network_data->disconnect_reason, "TIMED-OUT");
            network_data->connection_ready = false;
            continue;
        }
        erprintlf(errno);
    }

    network_data->recv_active = false;

    dbprintlf(RED_BG "GS NETWORK RECEIVE THREAD EXITING");
    if (global->network_data->thread_status > 0)
    {
        global->network_data->thread_status = 0;
    }
    return NULL;
}

void *track_status_thread(void *args)
{
    dbprintlf(GREEN_FG "TRACK STATUS THREAD STARTING");

    global_data_t *global = (global_data_t *)args;
    NetDataClient *network_data = global->network_data;

    while (network_data->thread_status > 0)
    {
        // Send our coordinates.
        NetFrame *network_frame = new NetFrame((unsigned char *)global->AzEl, sizeof(global->AzEl), NetType::TRACKING_DATA, NetVertex::CLIENT);
        network_frame->sendFrame(global->network_data);
        delete network_frame;

        usleep(10 SEC);
    }

    dbprintlf(RED_BG "TRACK STATUS THREAD EXITING");
    if (global->network_data->thread_status > 0)
    {
        global->network_data->thread_status = 0;
    }
}