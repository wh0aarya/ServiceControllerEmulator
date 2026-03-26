/**
 * stop.h
 *
 * Handles stopping a Windows service.
 */

#pragma once

#include <string>
#include <vector>

 /**
  * Stops a given service.
  *
  * @param args Expected: [service_name]
  * @return exit code
  */
int HandleStopCommand(const std::vector<std::wstring>& args);

/**
 * Prints usage for stop command.
 */
void PrintStopUsage();