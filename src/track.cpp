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
#include "DateTime.h"
#include "Observer.h"
#include "SGP4.h"
#include "meb_debug.h"
#include "track.hpp"

int open_connection(char *devname)
{
    if (devname == nullptr)
    {
        dbprintlf(RED_FG "Device name null!");
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

    char command[7];
    snprintf(command, sizeof(command), "PB %d\r", (int)(azimuth DEG));

    ssize_t az_bytes = write(connection, command, sizeof(command));
    if (az_bytes != sizeof(command))
    {
        dbprintlf(RED_FG "Error sending azimuth adjustment command to positioner (%d).", az_bytes);
        return -1;
    }

    return 1;
}

int aim_elevation(int connection, double elevation)
{
    // Command the dish manuever elevation.

    char command[7];
    snprintf(command, sizeof(command), "PB %d\r", (int)(elevation DEG));

    ssize_t el_bytes = write(connection, command, sizeof(command));
    if (el_bytes != sizeof(command))
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