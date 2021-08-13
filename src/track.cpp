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

    ssize_t az_bytes = write(connection, command, command_size);
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

    ssize_t el_bytes = write(connection, command, command_size);
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
    global_data_t *global_data = (global_data_t *) args;

    // Open a connection to the dish controller.
    int connection = open_connection(global_data->devname);
    if (connection < 3)
    {
        dbprintlf(FATAL "Device not found.");
        global_data->network_data->thread_status = 0;
        return NULL;
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
            ideal = find_next_targetrise(target, dish);
        }

        // NOTE: Assume the current azimuth and elevation is whatever we last told it to be at.

        // Find the difference between ideal and actual angles. If we are off from ideal by >1 degree, aim at the ideal.
        if (ideal.azimuth DEG - global_data->AzEl[0] < -1 || ideal.azimuth DEG - global_data->AzEl[0] > 1)
        {
            aim_azimuth(connection, ideal.azimuth DEG);
            global_data->AzEl[0] = ideal.azimuth DEG;

            // Send our updated coordinates.
            gs_network_transmit(global_data->network_data, CS_TYPE_TRACKING_DATA, CS_ENDPOINT_CLIENT, global_data->AzEl, sizeof(global_data->AzEl));
        }

        if (ideal.elevation DEG - global_data->AzEl[1] < -1 || ideal.elevation DEG - global_data->AzEl[1] > 1)
        {
            aim_elevation(connection, ideal.elevation DEG);
            global_data->AzEl[1] = ideal.elevation DEG;

            // Send our updated coordinates.
            gs_network_transmit(global_data->network_data, CS_TYPE_TRACKING_DATA, CS_ENDPOINT_CLIENT, global_data->AzEl, sizeof(global_data->AzEl));
        }

        usleep(1 SEC);
    }

    delete dish;
    delete target;
    close(connection);

    if (global_data->network_data->thread_status > 0)
    {
        global_data->network_data->thread_status = 0;
    }
    return NULL;
}

void *gs_network_rx_thread(void *args)
{
    global_data_t *global_data = (global_data_t *)args;
    network_data_t *network_data = global_data->network_data;

    // Similar, if not identical, to the network functionality in ground_station.
    // Roof UHF is a network client to the GS Server, and so should be very similar in socketry to ground_station.

    while (network_data->rx_active && global_data->network_data->thread_status > 0)
    {
        if (!network_data->connection_ready)
        {
            usleep(5 SEC);
            continue;
        }

        int read_size = 0;

        while (read_size >= 0 && network_data->rx_active)
        {
            char buffer[sizeof(NetworkFrame) * 2];
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
                NetworkFrame *network_frame = (NetworkFrame *)buffer;

                // Check if we've received data in the form of a NetworkFrame.
                if (network_frame->checkIntegrity() < 0)
                {
                    dbprintlf("Integrity check failed (%d).", network_frame->checkIntegrity());
                    continue;
                }
                dbprintlf("Integrity check successful.");

                global_data->netstat = network_frame->getNetstat();

                // For now, just print the Netstat.
                uint8_t netstat = network_frame->getNetstat();
                dbprintlf(BLUE_FG "NETWORK STATUS");
                dbprintf("GUI Client ----- ");
                ((netstat & 0x80) == 0x80) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Roof UHF ------- ");
                ((netstat & 0x40) == 0x40) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Roof X-Band ---- ");
                ((netstat & 0x20) == 0x20) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");
                dbprintf("Haystack ------- ");
                ((netstat & 0x10) == 0x10) ? printf(GREEN_FG "ONLINE" RESET_ALL "\n") : printf(RED_FG "OFFLINE" RESET_ALL "\n");

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

                NETWORK_FRAME_TYPE type = network_frame->getType();
                switch (type)
                {
                case CS_TYPE_TRACKING_CONFIG:
                {
                    dbprintlf(BLUE_FG "Received a tracking configuration.");

                    // TODO: Apply some configuration.

                    break;
                }
                case CS_TYPE_TRACKING_DATA:
                {
                    // If we receive this, then we are being polled.
                    // Typically this is sent automatically.
                    dbprintlf(BLUE_FG "Received a tracking data request.");

                    gs_network_transmit(network_data, CS_TYPE_TRACKING_DATA, CS_ENDPOINT_CLIENT, global_data->AzEl, sizeof(global_data->AzEl));
                    
                    break;
                }
                case CS_TYPE_ACK:
                {
                    dbprintlf(BLUE_FG "Received an ACK frame!");
                    break;
                }
                case CS_TYPE_NACK:
                {
                    dbprintlf(BLUE_FG "Received a NACK frame!");
                    break;
                }
                case CS_TYPE_CONFIG_UHF:
                {
                    dbprintlf(BLUE_FG "Received an UHF CONFIG frame!");
                    break;
                }
                case CS_TYPE_DATA:
                {
                    dbprintlf(BLUE_FG "Received a DATA frame!");
                    break;
                }
                case CS_TYPE_POLL_XBAND_CONFIG:
                case CS_TYPE_XBAND_COMMAND:
                case CS_TYPE_CONFIG_XBAND:
                case CS_TYPE_NULL:
                case CS_TYPE_ERROR:
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
            strcpy(network_data->discon_reason, "SERVER-FORCED");
            network_data->connection_ready = false;
            continue;
        }
        else if (errno == EAGAIN)
        {
            dbprintlf(YELLOW_BG "Active connection timed-out (%d).", read_size);
            strcpy(network_data->discon_reason, "TIMED-OUT");
            network_data->connection_ready = false;
            continue;
        }
        erprintlf(errno);
    }

    network_data->rx_active = false;
    dbprintlf(FATAL "DANGER! NETWORK RECEIVE THREAD IS RETURNING!");

    if (global_data->network_data->thread_status > 0)
    {
        global_data->network_data->thread_status = 0;
    }
    return NULL;
}