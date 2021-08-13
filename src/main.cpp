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

int main(int argc, char *argv[])
{
    char devname[32] = "/dev/ttyUSB0";
    
    if (argc > 2)
    {
        dbprintlf(FATAL "Invalid number of command-line arguments given.");
        return -1;
    }
    else if (argc == 2)
    {
        strcpy(devname, argv[1]);
    }

    // Open a connection to the dish controller.
    int connection = open_connection(devname);
    if (connection < 3)
    {
        dbprintlf(FATAL "Device not found.");
        return -1;
    }

    SGP4 *target = new SGP4(Tle(TLE[0], TLE[1]));
    Observer *dish = new Observer(GS_LAT, GS_LON, ELEV);
    CoordTopocentric ideal;

    double current_azimuth = 0;
    double current_elevation = 0;

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
        if (ideal.azimuth DEG - current_azimuth < -1 || ideal.azimuth DEG - current_azimuth > 1)
        {
            aim_azimuth(connection, ideal.azimuth DEG);
            current_azimuth = ideal.azimuth DEG;
        }

        if (ideal.elevation DEG - current_elevation < -1 || ideal.elevation DEG - current_elevation > 1)
        {
            aim_elevation(connection, ideal.elevation DEG);
            current_elevation = ideal.elevation DEG;
        }

        usleep(1 SEC);
    }

    delete dish;
    delete target;
    close(connection);
    return 1;
}