/**
 * qfailure.h
 *
 * Handles the 'qfailure' command.
 *
 * This command retrieves the configured failure actions
 * for a Windows service (e.g., restart, reboot, run program).
 */

#pragma once

#include <vector>
#include <string>

 /**
  * Processes the 'qfailure' command.
  *
  * Expected usage:
  *   qfailure <service_name>
  *
  * @param args Full command-line arguments
  * @return true if successful, false otherwise
  */
bool HandleQFailureCommand(const std::vector<std::string>& args);