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

// PB = Azimuth Command
// PA = Elevation Command

// Azimuth in DEGREES
int aim_azimuth(int connection, double azimuth)
{
    azimuth += AZIM_ADJ;
    azimuth = azimuth < 0 ? 360.0 + azimuth : azimuth;
    
    // Command the dish manuever azimuth.
    const int command_size = 0x10;
    char command[command_size];
    snprintf(command, command_size, "PB %d\r", (int)(azimuth));

    dbprintlf(GREEN_FG "COMMANDING AZ (%.2f): %s", azimuth, command);

    ssize_t az_bytes = 0;
    az_bytes = write(connection, command, command_size);
    if (az_bytes != command_size)
    {
        dbprintlf(RED_FG "Error sending azimuth adjustment command to positioner (%d).", az_bytes);
        return -1;
    }

    return 1;
}

// Elevation in DEGREES
int aim_elevation(int connection, double elevation)
{
    // Command the dish manuever elevation.
    const int command_size = 0x10;
    char command[command_size];
    snprintf(command, command_size, "PA %d\r", (int)(elevation));

    dbprintlf(GREEN_FG "COMMANDING EL (%.2f): %s", elevation, command);

    ssize_t el_bytes = 0;
    el_bytes = write(connection, command, command_size);
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
    while (dish->GetLookAngle(target->FindPosition(DateTime(time))).elevation < MIN_ELEV)
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
    CoordTopocentric obscured_azel; // only used for debug printing of 

    // double current_azimuth = 0;
    // double current_elevation = 0;

    bool sat_viewable_last_pass = false;
    bool sat_viewable = false;
    double pass_start_Az = 0.0;
    double pass_start_El = 0.0;

    for (;;)
    {
        // Establish if the target is visible.
        if (dish->GetLookAngle(target->FindPosition(DateTime::Now(true))).elevation DEG > MIN_ELEV)
        { // The target is visible.
            sat_viewable = true;
            // Find the angle to the target.
            ideal = dish->GetLookAngle(target->FindPosition(DateTime::Now(true)));
            dbprintlf(GREEN_FG "IDEAL AT   AZ:EL %.2f:%.2f", ideal.azimuth DEG, ideal.elevation DEG);
        }
        else
        { // The target is not visible.
            sat_viewable = false;
            // Find the angle to the next targetrise.
            dbprintlf(BLUE_FG "TARGET NOT VISIBLE");
            // ideal = dish->GetLookAngle(target->FindPosition(DateTime::Now(true)));
            // ideal = find_next_targetrise(target, dish);
            // dbprintlf(YELLOW_FG "WAITING AT AZ:EL %.2f:%.2f", ideal.azimuth DEG, ideal.elevation DEG);
            obscured_azel = dish->GetLookAngle(target->FindPosition(DateTime::Now(true)));
            // ideal.azimuth = 1.5708; // 90deg, this is in radians
            // Change elevation to 90' when parked.
            ideal.elevation = M_PI / 2; // 90deg, this is in radians.
            /// D O   N O T   C H A N G E   A Z   W H E N   P A R K E D
            dbprintlf(YELLOW_FG "PARKING AT AZ:EL %.2f:%.2f (%.2f:%.2f)", ideal.azimuth DEG, ideal.elevation DEG, obscured_azel.azimuth DEG, obscured_azel.elevation DEG);
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

        if (sat_viewable && !sat_viewable_last_pass)
        {
            dbprintlf(BLUE_FG "LOGGING STARTING AZEL OF THIS PASS");
            sat_viewable_last_pass = true;
            pass_start_Az = global->AzEl[0];
            pass_start_El = global->AzEl[1];
        }
        else if (!sat_viewable && sat_viewable_last_pass)
        {
            dbprintlf(BLUE_FG "RETRACING TO STARTING AZEL OF THIS PASS");
            sat_viewable_last_pass = false;
            aim_azimuth(global->connection, pass_start_Az);
            aim_elevation(global->connection, pass_start_El);
        }

        dbprintlf(BLUE_FG "CURRENT AZEL: %f:%f", global->AzEl[0], global->AzEl[1]);

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

        while (read_size >= 0 && network_data->recv_active && network_data->thread_status > 0)
        {
            dbprintlf(BLUE_FG "Waiting to receive...");

            NetFrame *netframe = new NetFrame();
            read_size = netframe->recvFrame(network_data);

            dbprintlf("Read %d bytes.", read_size);

            if (read_size >= 0)
            {
                dbprintlf("Received the following NetFrame:");
                netframe->print();
                netframe->printNetstat();

                // Extract the payload into a buffer.
                int payload_size = netframe->getPayloadSize();
                unsigned char *payload = (unsigned char *)malloc(payload_size);
                if (payload == nullptr)
                {
                    dbprintlf(FATAL "Memory for payload failed to allocate, packet lost.");
                    continue;
                }

                if (netframe->retrievePayload(payload, payload_size) < 0)
                {
                    dbprintlf(RED_FG "Error retrieving data.");
                    if (payload != nullptr)
                    {
                        free(payload);
                        payload = nullptr;
                    }
                    continue;
                }

                switch (netframe->getType())
                {
                case NetType::TRACKING_COMMAND:
                {
                    dbprintlf(BLUE_FG "Received a tracking command.");

                    double AzEl[2] = {0};
                    netframe->retrievePayload((unsigned char *)AzEl, sizeof(AzEl));

                    // dbprintlf(BLUE_FG "Setting AzEl to: %d, %d", AzEl[0], AzEl[1]);

                    // aim_azimuth(global->connection, AzEl[0]);
                    // global->AzEl[0] = AzEl[0];
                    // aim_azimuth(global->connection, AzEl[1]);
                    // global->AzEl[1] = AzEl[1];

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
                if (payload != nullptr)
                {
                    free(payload);
                    payload = nullptr;
                }
            }
            else
            {
                break;
            }

            delete netframe;
        }
        if (read_size == -404)
        {

        }
        else if (errno == EAGAIN)
        {

        }
        erprintlf(errno);
    }

    network_data->recv_active = false;

    dbprintlf(FATAL "DANGER! NETWORK RECEIVE THREAD IS RETURNING!");
    if (global->network_data->thread_status > 0)
    {
        global->network_data->thread_status = 0;
    }
    return nullptr;
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