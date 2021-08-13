/**
 * @file track.hpp
 * @author Mit Bailey (mitbailey99@gmail.com)
 * @brief 
 * @version See Git tags for version information.
 * @date 2021.08.12
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#ifndef TRACK_HPP
#define TRACK_HPP

#include "CoordTopocentric.h"

#define SEC *1000000 // nanoseconds to seconds
#define DEG *57.29578 // radians to degrees
#define GS_LAT 42.655583
#define GS_LON -71.325433
#define ELEV 0.061 // Lowell ASL + Olney Height; Kilometers for some reason.

/**
 * @brief Two line element of the ISS.
 * 
 * TLE Updated: 	8/13/2021 3:28:02 AM UTC
 * TLE Epoch: 	8/13/2021 4:25:00 AM UTC
 * https://live.ariss.org/iss.txt
 *
 */
const char TLE[2][70] = {"1 25544U 98067A   21225.01742615  .00000967  00000-0  25772-4 0  9997",
                         "2 25544  51.6430  61.6856 0001213 305.7663 186.0142 15.48894495297401"};

/**
 * @brief Opens a serial connection.
 * 
 * @return int Connection value.
 */
int open_connection();

/**
 * @brief Commands the dish aim at this angle.
 * 
 * @param angle 
 * @return int 
 */
int aim_at(CoordTopocentric angle);

/**
 * @brief Finds the topocentric coordinates of the next targetrise.
 * 
 * @param model 
 * @param dish 
 * @return CoordTopocentric 
 */
CoordTopocentric find_next_targetrise(SGP4 *model, Observer *dish);

#endif // TRACK_HPP