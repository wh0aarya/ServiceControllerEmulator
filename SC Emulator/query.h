/**
 * query.h
 *
 * Handles the 'query' command.
 *
 * This command retrieves and displays the current status
 * of a Windows service (running, stopped, etc.).
 */

#pragma once

#include <vector>
#include <string>

 /**
  * Processes the 'query' command.
  *
  * Expected usage:
  *   query <service_name>
  *
  * @param args Full command-line arguments
  * @return true if successful, false otherwise
  */
bool HandleQueryCommand(const std::vector<std::string>& args);