#pragma once

#include <string>
#include <vector>

/**
 * Prints usage/help information for the `delete` command.
 *
 * This is displayed when the user passes `/ ?` or provides invalid arguments.
 */
void PrintDeleteUsage();

/**
 * Entry point for handling the `delete` command.
 *
 * This version accepts arguments as wide strings (std::wstring),
 * which matches how arguments are passed from `main` on Windows.
 *
 * Internally, the implementation converts arguments to narrow strings
 * and forwards them to the core logic.
 *
 * @param args Vector of command-line arguments (excluding the command name).
 * @return 0 on success, non-zero on failure (SC-style behavior).
 */
int HandleDeleteCommand(const std::vector<std::wstring>& args);