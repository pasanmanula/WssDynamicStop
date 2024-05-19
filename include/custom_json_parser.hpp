#ifndef STOCK_MARKER_CUSTOM_JSON_PARSER_HPP_
#define STOCK_MARKER_CUSTOM_JSON_PARSER_HPP_

#include <iostream>
#include <cstring>
#include <string>
#include <nlohmann/json.hpp>
#include <boost/lockfree/queue.hpp>
#include "constants.hpp"

namespace finnhub_json {

struct DataSlice {
  DataSlice()
  {
    std::memset(ticker_symbol, 0, kMaxSymbolLength);
  }
  char ticker_symbol[kMaxSymbolLength];
  double last_price{0.0};
  double avg_filling_price{0.0};
  double shares{0.0};
  double dynamic_stop_price{0.0};
  double gain_loss{0.0};
};

/**
 * @brief Processes ticker data and updates the data queue and persistent data.
 *
 * This function processes ticker data from a JSON object, updates the data queue with new data slices,
 * and modifies the persistent data map with the latest values. It calculates gain/loss and dynamic stop price.
 *
 * @param ticker_data The JSON object containing ticker data.
 * @param data_queue A reference to the lock-free queue where processed data slices will be stored.
 * @param persist_data A reference to the unordered map containing persistent stock data.
 * @return true if the ticker data was successfully processed; false otherwise.
 */
bool process_ticker_data(
  const nlohmann::json& ticker_data,
  boost::lockfree::queue<DataSlice>& data_queue,
  std::unordered_map<std::string, persistent_data::StockData>& persist_data)
{
  try
  {
    DataSlice ui_data;
    for(const auto& data : ticker_data["data"])
    {
      const std::string ticker_symbol = data["s"];
      auto persistent_itr = persist_data.find(ticker_symbol);
      if(persistent_itr != persist_data.end())
      { 
        std::strncpy(ui_data.ticker_symbol, ticker_symbol.c_str(), kMaxSymbolLength - 1);
        ui_data.ticker_symbol[kMaxSymbolLength - 1] = '\0'; // Ensure null-termination
        ui_data.last_price = data["p"];
        ui_data.avg_filling_price = persistent_itr->second.avg_filling_price;
        ui_data.shares = persistent_itr->second.shares;
        // Initial dynamic is default to min profit target
        if (persistent_itr->second.last_dynamic_stop_price == 0.0) {
          ui_data.dynamic_stop_price =
            ui_data.avg_filling_price * (1.0 + persistent_itr->second.min_profit_target_percentage);
        } else {
          // Subsequent dynamic stop
          // TODO - should based on unrealized gain
          // ui_data.dynamic_stop_price = new_dynamic_stop; // implement your own new_dynamic_stop
        }
        persistent_itr->second.last_dynamic_stop_price = ui_data.dynamic_stop_price;
        ui_data.gain_loss =
          std::round(
            (ui_data.last_price - ui_data.avg_filling_price) * persistent_itr->second.shares * 100.0) / 100.0;
      }

      if (!data_queue.push(ui_data))
      {
        std::cerr << "Queue is full, dropping data.\n";
      }
    }
    return true;
  }
  catch(const std::exception& e)
  {
    std::cerr << e.what() << '\n';
    return false;
  }
}

/**
 * @brief Validates and processes a JSON message from the WebSocket server.
 *
 * This function parses a JSON message received from the WebSocket server and validates it.
 * If the message is valid and of type "trade", it processes the ticker data.
 *
 * @param wss_message The JSON message received from the WebSocket server.
 * @param data_queue A reference to the lock-free queue where processed data slices will be stored.
 * @param persist_data A reference to the unordered map containing persistent stock data.
 * @return true if the message was successfully processed; false otherwise.
 */
bool validate_json(
  const std::string& wss_message,
  boost::lockfree::queue<DataSlice>& data_queue,
  std::unordered_map<std::string, persistent_data::StockData>& persist_data)
{
  nlohmann::json ticker_data = nlohmann::json::parse(wss_message, nullptr, false);
  if (ticker_data.is_discarded())
  {
    std::cerr << "Invalid json message received. Ignoring\n";
    return false;
  }
  if (ticker_data["type"] == "trade")
  {
    return process_ticker_data(ticker_data, data_queue, persist_data);
  }
  return false;
}

}  // namespace finnhub_json

#endif  // STOCK_MARKER_CUSTOM_JSON_PARSER_HPP_
