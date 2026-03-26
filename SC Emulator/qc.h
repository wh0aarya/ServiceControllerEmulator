/**
 * qc.h
 *
 * Handles the 'qc' (query configuration) command.
 *
 * This retrieves and displays the configuration details
 * of a specified Windows service.
 */

#pragma once

#include <vector>
#include <string>

 /**
  * Processes the 'qc' command.
  *
  * Expected usage:
  *   qc <service_name>
  *
  * @param args Full command-line arguments
  * @return true if successful, false otherwise
  */
bool HandleQcCommand(const std::vector<std::string>& args);