/**
 * config.h
 *
 * Handles the 'config' command.
 *
 * This command modifies an existing Windows service configuration,
 * such as startup type, binary path, account, etc.
 */

#pragma once

#include <vector>
#include <string>

 /**
  * Processes the 'config' command.
  *
  * Expected usage:
  *   config <service_name> [options]
  *
  * Example:
  *   config MyService start= auto
  *
  * @param args Full command-line arguments
  * @return true if successful, false otherwise
  */
bool HandleConfigCommand(const std::vector<std::string>& args);