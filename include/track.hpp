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
#include "network.hpp"

#define SERVER_PORT 52040

#define SEC *1000000 // nanoseconds to seconds
#define DEG *(180/3.1415926) // radians to degrees
#define GS_LAT 42.655583
#define GS_LON -71.325433
#define ELEV 0.061 // Lowell ASL + Olney Height; Kilometers for some reason.
#define MIN_ELEV 10.0 // degrees
#define ELEV_ADJ 0 // degrees adjustment +-
#define AZIM_ADJ -34 // degrees adjustment +-

typedef struct
{
    // uhf_modem_t modem; // Just an int.
    int uhf_initd;
    NetDataClient *network_data;
    uint8_t netstat;
    char devname[32];
    double AzEl[2]; // Azimuth, Elevation
    int connection;
} global_data_t;

/**
 * @brief Two line element of the ISS.
 * 
 * TLE Updated: 	8/13/2021 3:28:02 AM UTC
 * TLE Epoch: 	8/13/2021 4:25:00 AM UTC
 * https://live.ariss.org/iss.txt
 *
 */
// const char TLE[2][70] = {"1 25544U 98067A   21225.01742615  .00000967  00000-0  25772-4 0  9997",
//                          "2 25544  51.6430  61.6856 0001213 305.7663 186.0142 15.48894495297401"};

// Pseudo-SPACEHAUC TLE
// const char TLE[2][70] = {"1 92993U 92993A   21285.08657525  .00003625  00000-0  73984-4 0  9997",
//                          "2 92993  51.6423 124.6990 0004178  81.3930  10.3821 15.48978029306712"};

// Psuedo-SPACEHAUC TLE 2
const char TLE[2][70] = {"1 92994U 92994A   21285.08657525  .00003625  00000-0  73984-4 0  9997",
                         "2 92994  51.6423 124.6990 0004178  80.1930  10.3821 15.48978029306712"};

// 1 92994U 92994A   21285.08657525  .00003625  00000-0  73984-4 0  9997
// 2 92994  51.6423 124.6990 0004178  80.1930  10.3821 15.48978029306712

// 1 92993U 92993A   21285.08657525  .00003625  00000-0  73984-4 0  9997
// 2 92993  51.6423 124.6990 0004178  81.3930  10.3821 15.48978029306712

/**
 * @brief Opens a serial connection.
 * 
 * @param devname Name of the device.
 * @return int File descriptor on success, negative on failure.
 */
int open_connection(char *devname);

/**
 * @brief 
 * 
 * @param connection 
 * @param azimuth 
 * @return int 
 */
int aim_azimuth(int connection, double azimuth);

/**
 * @brief 
 * 
 * @param connection 
 * @param elevation 
 * @return int 
 */
int aim_elevation(int connection, double elevation);

/**
 * @brief Finds the topocentric coordinates of the next targetrise.
 * 
 * @param model 
 * @param dish 
 * @return CoordTopocentric 
 */
CoordTopocentric find_next_targetrise(SGP4 *model, Observer *dish);

/**
 * @brief 
 * 
 * @param args 
 * @return void* 
 */
void *tracking_thread(void *args);

/**
 * @brief 
 * 
 * @param args 
 * @return void* 
 */
void *gs_network_rx_thread(void *args);

/**
 * @brief Periodically sends X-Band status updates.
 * 
 */
void *track_status_thread(void *args);

#endif // TRACK_HPP