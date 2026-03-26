/**
 * qdescription.h
 *
 * Handles the 'qdescription' command.
 *
 * This retrieves and displays the description of a
 * Windows service.
 */

#pragma once

#include <vector>
#include <string>

 /**
  * Processes the 'qdescription' command.
  *
  * Expected usage:
  *   qdescription <service_name>
  *
  * @param args Full command-line arguments
  * @return true if successful, false otherwise
  */
bool HandleQDescriptionCommand(const std::vector<std::string>& args);