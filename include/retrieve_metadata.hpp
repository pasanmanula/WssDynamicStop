#ifndef STOCK_MARKER_RETRIEVE_METADATA_HPP_
#define STOCK_MARKER_RETRIEVE_METADATA_HPP_

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <nlohmann/json.hpp>

namespace persistent_data {

struct StockData {
  std::string ticker_symbol;
  double avg_filling_price{0.0};
  double shares{0.0};
  double min_profit_target_percentage{0.0};
  double last_dynamic_stop_price{0.0};
  size_t tick_length{1};
  size_t increasing_cumulative_ticks{0};
  double increment_temparature{0.01};
};

/**
 * @brief Reads the Finnhub URI from a configuration file.
 *
 * This function reads the content of a configuration file specified by CONFIG_FILE_PATH.
 * The file is expected to contain the Finnhub URI as plain text.
 *
 * @return A string containing the content of the configuration file, which is the Finnhub URI.
 * @throws std::runtime_error if the file cannot be opened.
 */
std::string get_finnhub_uri()
{
  std::filesystem::path file_path = CONFIG_FILE_PATH;
  std::ifstream file(file_path);

  // Check if the file was successfully opened
  if (!file.is_open()) {
      throw std::runtime_error("Could not open file: " + file_path.string());
  }

  std::ostringstream content;
  content << file.rdbuf();
  file.close();

  return content.str();
}

/**
 * @brief Reads a JSON file containing stock data and returns a map of ticker symbols to StockData.
 *
 * This function reads a JSON file specified by STOCK_LIST_FILE_PATH and parses it to extract stock data.
 * The stock data includes ticker symbol, average filling price, number of shares, and minimum profit target percentage.
 * The function returns an unordered map where the keys are ticker symbols and the values are StockData objects.
 *
 * @return An unordered map with ticker symbols as keys and StockData as values.
 * @throws std::runtime_error if the file cannot be opened.
 */
std::unordered_map<std::string, StockData> get_ticker_list_from_file()
{
  std::filesystem::path file_path = STOCK_LIST_FILE_PATH;
  std::ifstream file(file_path);

  // Check if the file was successfully opened
  if (!file.is_open()) {
      throw std::runtime_error("Could not open file: " + file_path.string());
  }

  // Parse the JSON file
  nlohmann::json json_object;
  file >> json_object;

  std::unordered_map<std::string, StockData> stock_data;
  StockData data;
  for (const auto& item : json_object) {
    data.ticker_symbol = item["ticker_symbol"];
    data.avg_filling_price = item["average_filling_price"];
    data.shares = item["shares"];
    data.min_profit_target_percentage = item["min_profit_target_percentage"];
    data.tick_length = item["dynamic_stop_strategy_tick_length"];
    data.increment_temparature = item["dynamic_stop_strategy_increment_percentage"];
    stock_data.insert(std::pair<std::string, StockData>(item["ticker_symbol"], data));
  }

  return stock_data;
}

}  // namespace persistent_data
#endif  // STOCK_MARKER_RETRIEVE_METADATA_HPP_
