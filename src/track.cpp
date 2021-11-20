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
#include "gpiodev/gpiodev.h"

int open_connection(char *devname)
{
#if defined(DISABLE_DEVICE)
    int connection = 3;
    dbprintlf(FATAL "Serial device not in use, simulation only");
#else
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
#endif
    return connection;
}

// PB = Azimuth Command
// PA = Elevation Command

// Azimuth in DEGREES, takes 160 ms
int aim_azimuth(int connection, double azimuth)
{
#if defined(DISABLE_DEVICE)
    dbprintlf(FATAL "Serial device not in use, simulation only");
#else
    azimuth += AZIM_ADJ;
    azimuth = azimuth < 0 ? 360.0 + azimuth : azimuth;

    // Command the dish manuever azimuth.
    const int command_size = 0x10;
    char command[command_size];
    snprintf(command, command_size, "PB %03d\r\n", (int)(azimuth));

    dbprintlf(GREEN_FG "COMMANDING AZ (%.2f): %s", azimuth, command);

    for (int i = 0; i < 8; i++)
    {
        if (write(connection, command + i, 1) != 1)
        {
            dbprintlf(FATAL "Writing byte %d/8 of AZ command, error");
            return -1;
        }
        usleep(20000);
    }
#endif
    return 1;
}

// Elevation in DEGREES, takes 160 ms
int aim_elevation(int connection, double elevation)
{
#if defined(DISABLE_DEVICE)
    dbprintlf(FATAL "Serial device not in use, simulation only");
#else
    // Command the dish manuever elevation.
    const int command_size = 0x10;
    char command[command_size];
    snprintf(command, command_size, "PA %03d\r\n", (int)(elevation));

    dbprintlf(GREEN_FG "COMMANDING EL (%.2f): %s", elevation, command);

    for (int i = 0; i < 8; i++)
    {
        if (write(connection, command + i, 1) != 1)
        {
            dbprintlf(FATAL "Writing byte %d/8 of AZ command, error");
            return -1;
        }
        usleep(20000);
    }
#endif
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

    sleep(2);
    if (global->resetAtInit)
    {
        aim_azimuth(global->connection, -AZIM_ADJ);
        usleep(20000);
        aim_elevation(global->connection, 90);
        for (int i = 60; i > 0; i--)
        {
            dbprintlf(RED_FG "Init: Sleep remaining %d seconds", i);
            sleep(1);
        }
    }

    char biasctrl_status = 0;
    if ((biasctrl_status = system("biasctrl -r") >> 8) < 0)
    {
        dbprintlf(FATAL "Could not reset bias controller, exiting, code %d", biasctrl_status);
        exit(0);
    }
    if ((biasctrl_status = system("biasctrl -s -3.0") >> 8) < 0)
    {
        dbprintlf(FATAL "Could not reset bias voltage, exiting, code %d", biasctrl_status);
        exit(0);
    }

    SGP4 *target = new SGP4(Tle(TLE[0], TLE[1]));
    Observer *dish = new Observer(GS_LAT, GS_LON, ELEV);

    bool pending_az = false;
    bool pending_el = false;
    bool pending_any = false;

    bool sat_viewable = false;

    double cmd_az = 0;
    double cmd_el = M_PI_2;

    int sleep_timer = 0;
    int sleep_timer_max = 0;

    for (;;)
    {
        // Step 1: Execute command
        pending_any = pending_az | pending_el;

        if (pending_az)
            aim_azimuth(global->connection, cmd_az); // 160 ms
        else
            usleep(160000);

        pending_az = false;
        if (pending_el)
            aim_elevation(global->connection, cmd_el); // 160 ms
        else
            usleep(160000);

        pending_el = false;
        usleep(100000);
        if (pending_any) // any change, send over network
        {
            global->AzEl[0] = cmd_az;
            global->AzEl[1] = cmd_el;
            NetFrame *network_frame = new NetFrame((unsigned char *)global->AzEl, sizeof(global->AzEl), NetType::TRACKING_DATA, NetVertex::CLIENT);
            network_frame->sendFrame(global->network_data);
            delete network_frame;
        }
        // Now we have 680000 us left
        usleep(580000);
        // Determine position of satellite NOW
        DateTime tnow = DateTime::Now(true);
        Eci pos_now = target->FindPosition(tnow);
        CoordTopocentric current_pos = dish->GetLookAngle(pos_now);
        CoordGeodetic current_lla = pos_now.ToGeodetic();
        dbprintlf(BLUE_BG "Current Position: %.2f AZ, %.2f EL | %.2f LA, %.2f LN", current_pos.azimuth DEG, current_pos.elevation DEG, current_lla.latitude DEG, current_lla.longitude DEG);
        if (sleep_timer)
        {
            if (sleep_timer > sleep_timer_max) // update max
                sleep_timer_max = sleep_timer;
            if ((sleep_timer_max - sleep_timer) < 20) // for 20 seconds command parking
            {
                pending_az = true;
                pending_el = true;
            }
            sleep_timer--;
            dbprintlf(BLUE_FG "Will be sleeping for %d more seconds...", sleep_timer);
            continue;
        }
        else
        {
            sleep_timer_max = 0;
        }
        // Step 2: Are we in a pass?
        if (current_pos.elevation DEG > MIN_ELEV)
        {
            if (!sat_viewable) // satellite just became visible
            {
                gpioSetMode(15, GPIO_OUT);
                gpioWrite(15, GPIO_LOW);
            }
            sat_viewable = true;
            if (fabs(cmd_az DEG - current_pos.azimuth DEG) > 1) // azimuth has changed
            {
                cmd_az = current_pos.azimuth DEG;
                pending_az = true;
            }
            if (fabs(cmd_el DEG - current_pos.elevation DEG) > 1)
            {
                cmd_el = current_pos.elevation DEG;
                pending_el = true;
            }
            continue;
        }
        // Step 3: Were we in a pass?
        if (sat_viewable) // we are here, but sat_viewable is on. Meaning we just got out of a pass
        {
            sat_viewable = false;
            cmd_az = -AZIM_ADJ;
            cmd_el = 90;
            pending_az = true;
            pending_el = true;
            sleep_timer = 120; // 120 seconds
            gpioSetMode(15, GPIO_IN); // set packet output to high Z
            gpioSetMode(18, GPIO_IN); // set PA VDD to high Z
        }
        sat_viewable = false;
        // Step 4: Projection
        DateTime tnext = tnow;
#define LOOKAHEAD_MIN 2
#define LOOKAHEAD_MAX 4
        tnext = tnext.AddMinutes(LOOKAHEAD_MAX); // 4 minutes lookahead
        for (int i = 0; i < (LOOKAHEAD_MAX - LOOKAHEAD_MIN) * 60; i++)
        {
            Eci eci_ahd = target->FindPosition(tnext);
            CoordTopocentric pos_ahd = dish->GetLookAngle(eci_ahd);
            if (i == 0)
                dbprintlf(GREEN_BG "Lookahead %d: %.2f AZ %.2f EL", i, pos_ahd.azimuth DEG, pos_ahd.elevation DEG);
            int ahd_el = pos_ahd.elevation DEG;
            if (ahd_el < (int)MIN_ELEV) // still not in view 4 minutes ahead, don't care
            {
                break;
            }
            if (ahd_el > (int)MIN_ELEV) // already up, find where it is at proper elevation
            {
                tnext = tnext.AddSeconds(-1);
            }
            else // right point
            {
                cmd_az = pos_ahd.azimuth DEG;
                cmd_el = pos_ahd.elevation DEG;
                pending_az = true;
                pending_el = true;
                sleep_timer = LOOKAHEAD_MAX * 60 - i; // lookahead left
                gpioSetMode(18, GPIO_OUT);            // set PA VDD EN to output
                gpioWrite(18, GPIO_HIGH);             // enable PA VDD
                break;                                // break inner for loop
            }
        }
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
