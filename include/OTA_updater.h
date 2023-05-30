/**
 * @file       OTA_updater.h
 * @author     Fa-b
 * @license    This project is released under the MIT License (MIT)
 * @date       Sep 2021
 * @brief      Checks for updates on a dedicated OTA Server (Port 8070 atm), compares versions and downloads
 */

#include <Arduino.h>

#ifndef OTA_updater_h
#define OTA_updater_h

int versionCompare(String, String);
void checkForUpdates(void);

#endif /* OTA_updater_h */