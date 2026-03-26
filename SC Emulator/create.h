/**
 * create.h
 *
 * Handles the 'create' command.
 *
 * This command creates a new Windows service using the
 * Service Control Manager (SCM).
 */

#pragma once

#include <vector>
#include <string>

 /**
  * Processes the 'create' command.
  *
  * Expected usage:
  *   create <service_name> binPath= <path> [options]
  *
  * Example:
  *   create MyService binPath= C:\myservice.exe start= auto
  *
  * @param args Full command-line arguments
  * @return true if creation succeeded, false otherwise
  */
bool HandleCreateCommand(const std::vector<std::string>& args);