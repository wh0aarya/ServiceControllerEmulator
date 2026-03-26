/**
 * failure.h
 *
 * Handles the 'failure' command.
 *
 * This command sets failure recovery actions for a service,
 * such as restarting the service or running a program when it fails.
 */

#pragma once

#include <vector>
#include <string>

 /**
  * Processes the 'failure' command.
  *
  * Expected usage:
  *   failure <service_name> [options]
  *
  * Example:
  *   failure MyService reset= 60 actions= restart/5000
  *
  * @param args Full command-line arguments
  * @return true if successful, false otherwise
  */
bool HandleFailureCommand(const std::vector<std::string>& args);