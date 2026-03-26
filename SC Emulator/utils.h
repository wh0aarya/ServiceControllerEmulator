/**
 * utils.h
 *
 * Shared utility functions for:
 * - Argument parsing
 * - String manipulation
 * - Validation
 * - Debug printing
 */

#pragma once

#include <string>
#include <vector>

 /**
  * Represents a parsed option of the form:
  * key= value
  */
struct ParsedOption
{
    std::string key;
    std::string value;
};

// String helpers
std::string ToLower(std::string s);
bool EqualsIgnoreCase(const std::string& a, const std::string& b);

// Error / UI helpers
void PrintDefaultError();
void PrintUsage();

// Command helpers
bool IsKnownCommand(const std::string& cmd);
bool IsOptionToken(const std::string& token);

// Parsing logic
bool ParseOptions(
    const std::vector<std::string>& rawArgs,
    size_t startIndex,
    std::vector<ParsedOption>& parsedOptions);

// Option helpers
bool HasOption(const std::vector<ParsedOption>& options, const std::string& key);
std::string GetOptionValue(const std::vector<ParsedOption>& options, const std::string& key);

// Validation
bool ValidateCommandArguments(
    const std::string& command,
    const std::string& serviceName,
    const std::vector<ParsedOption>& options);

// Debug helper
void PrintParsedArguments(
    const std::string& command,
    const std::string& serviceName,
    const std::vector<ParsedOption>& options);