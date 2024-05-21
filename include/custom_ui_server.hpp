#ifndef STOCK_MARKER_CUSTOM_UI_SERVER_HPP_
#define STOCK_MARKER_CUSTOM_UI_SERVER_HPP_

#include <string>
#include <thread>
#include <condition_variable>
#include <set>
#include <nlohmann/json.hpp>
#include <boost/lockfree/queue.hpp>
#include "websocketpp/config/asio_no_tls.hpp"
#include "websocketpp/server.hpp"

class WebSocketServer {
public:
  /**
   * @brief Constructs a WebSocketServer object.
   * 
   * This constructor initializes a WebSocketServer object with the given WebSocket URI.
   * It also starts a separate thread to initialize and start the WebSocket connection.
   * 
   */
  explicit WebSocketServer(
    boost::lockfree::queue<finnhub_json::DataSlice>& data_queue,
    std::queue<std::string>& unsubscribe_list,
    std::mutex& unsubscribe_pipe_mutex,
    std::condition_variable& condition_variable,
    const uint16_t server_port)
      : queue_(data_queue),
        unsubscribe_list_(unsubscribe_list),
        unsubscribe_pipe_mutex_(unsubscribe_pipe_mutex),
        condition_variable_(condition_variable),
        server_thread_{std::thread(&WebSocketServer::init_and_run_wss_server, this, server_port)}
  {

  }

  /**
   * @brief Destroys the WebSocketServer object.
   * 
   * This destructor waits for the thread that was started in the constructor
   * to finish its execution before destroying the WebSocketServer object.
   * It ensures that all resources associated with the WebSocket connection are properly cleaned up.
   */
  ~WebSocketServer()
  {
    if (server_thread_.joinable())
    {
      server_thread_.join();
    }
    if (monitor_thread_.joinable())
    {
      monitor_thread_.join();
    }
  }

private:
  /**
   * @brief Initializes and starts a WebSocket Secure (WSS) connection.
   * 
   * This function initializes the WebSocket client, sets up various handlers,
   * and starts the connection to the specified URI.
   */
  void init_and_run_wss_server(uint16_t port)
  {
    configure_server();
    start_monitor_data_pipe();
    run_server(port);
  }

  /**
  * @brief Initializes the WebSocket client and sets up event handlers.
  * 
  * This function configures the WebSocket client by setting the log level,
  * initializing ASIO, and registering handlers for various events like 
  * connection open, connection fail, incoming message, and connection close.
  */
  void configure_server()
  {
    server_.clear_access_channels(websocketpp::log::alevel::all);
    server_.clear_error_channels(websocketpp::log::elevel::all);
    server_.init_asio();   
    server_.set_open_handler(
        std::bind(&WebSocketServer::on_open, this, std::placeholders::_1));
    server_.set_fail_handler(
        std::bind(&WebSocketServer::on_fail, this, std::placeholders::_1));
    server_.set_message_handler(
        std::bind(
            &WebSocketServer::on_message, this,
            std::placeholders::_1, std::placeholders::_2));
    server_.set_close_handler(
        std::bind(&WebSocketServer::on_close, this, std::placeholders::_1));
  }

  /**
   * @brief Starts a separate thread to monitor the data queue.
   *
   * This function creates a new thread that runs the `check_data_queue` method.
   * The thread will continuously monitor the data queue for new messages.
   */
  void start_monitor_data_pipe()
  {
    monitor_thread_ = std::thread(&WebSocketServer::check_data_queue, this);
  }

  /**
   * @brief Checks the data queue for new messages and broadcasts them.
   *
   * This function continuously checks the data queue for new messages. When a
   * message is found, it is removed from the queue and broadcasted to all
   * connected clients. If the queue is empty, the function will exit.
   */
  void check_data_queue()
  {
    while (true)
    {
      while (!queue_.empty())
      {
          finnhub_json::DataSlice message;
          while (!queue_.pop(message))
          {
            // Wait if the queue is empty
            if (queue_.empty())
            {
              return;
            }
          }
          broadcast_message(message);
      }
    }
  }

  /**
  * @brief Starts the WebSocket connection.
  * 
  * This function creates a connection to the specified URI and starts the 
  * ASIO io_service run loop to handle network events.
  */
  void run_server(uint16_t port)
  {
    try
    {
      server_.set_reuse_addr(true);
      server_.listen(port);
      server_.start_accept();
      std::cout << "Custom UI data server has been started on port : " << port << std::endl;
      server_.run();
    }
    catch(const websocketpp::exception& e)
    {
      std::cerr << e.what() << '\n';
      server_.stop_listening();

      // Close all connections gracefully
      server_.get_io_service().stop();
    } 
  }

  /**
  * @brief Serializes a DataSlice object into a JSON string.
  *
  * This function takes a DataSlice object and converts it into a JSON string
  * representation. The JSON object includes the following fields:
  * - "ticker_symbol": The symbol of the ticker.
  * - "last_price": The last price of the ticker.
  * - "avg_filling_price": The average filling price of the ticker.
  * - "shares": The total shares of the ticker.
  * - "dynamic_stop_price": The last dynamic stop price of the ticker.
  * - "gain_loss": The gain or loss associated with the ticker.
  *
  * @param data_slice The DataSlice object to serialize.
  * @return A JSON string representing the serialized DataSlice object.
  */
  std::string serialize_data_slice(const finnhub_json::DataSlice& data_slice)
  {
    nlohmann::json ui_json_data;
    ui_json_data["ticker_symbol"] = std::string(data_slice.ticker_symbol);
    ui_json_data["last_price"] = data_slice.last_price;
    ui_json_data["avg_filling_price"] = data_slice.avg_filling_price;
    ui_json_data["shares"] = data_slice.shares;
    ui_json_data["dynamic_stop_price"] = data_slice.last_dynamic_stop_price;
    ui_json_data["gain_loss"] = data_slice.gain_loss;
    return ui_json_data.dump();
  }

  std::string retrieve_close_position(const std::string& json_string)
  {
    nlohmann::json ui_json_data = nlohmann::json::parse(json_string, nullptr, false);
    if (ui_json_data["action"] == "close_position") {
        return ui_json_data["ticker_symbol"];
    } else {
        return "";
    }
  }

 /**
  * @brief Broadcasts a message to all connected clients.
  *
  * This function takes a DataSlice message and sends it to all connected
  * clients. The message is serialized into a JSON string before being sent.
  * If the broadcast fails for a client, an error message is printed to the
  * console.
  *
  * @param msg The DataSlice message to broadcast.
  */
  void broadcast_message(const finnhub_json::DataSlice& msg)
  {
    std::lock_guard<std::mutex> lock(client_conn_mutex_);
    for (auto hdl : client_connections_) {
      try
      {
        server_.send(hdl, std::move(serialize_data_slice(msg)), websocketpp::frame::opcode::text);
      } catch (const websocketpp::lib::error_code& e) {
        std::cout << "Broadcast failed to a client because: " << e.message() << std::endl;
      }
    }
  }

  /**
   * @brief Handler for the WebSocket connection open event.
   * 
   * @param hdl The handle to the WebSocket connection.
   */
  void on_open(std::weak_ptr<void> hdl)
  {
    std::lock_guard<std::mutex> lock(client_conn_mutex_);
    client_connections_.insert(hdl);
    std::cout << "A new client has been connected" << std::endl;
  }

  /**
  * @brief Handler for the WebSocket connection fail event.
  * 
  * @param weak_hdl The weak handle to the WebSocket connection.
  */
  void on_fail(std::weak_ptr<void> weak_hdl)
  {
    std::cout << "A client connection has been failed!" << std::endl;
  }

  /**
   * @brief Handler for incoming WebSocket messages.
   * 
   * @param weak_hdl The weak handle to the WebSocket connection.
   * @param msg The WebSocket message received.
   */
  void on_message(
    std::weak_ptr<void> weak_hdl,
    websocketpp::config::asio_tls_client::message_type::ptr msg)
  {
      std::string return_close_position = retrieve_close_position(msg->get_payload());
      if (!return_close_position.empty()) {
        std::lock_guard<std::mutex> lock(unsubscribe_pipe_mutex_);
        unsubscribe_list_.push(std::move(return_close_position));
        condition_variable_.notify_one();
      }
  }

  /**
   * @brief Handler for the WebSocket connection close event.
   * 
   * @param weak_hdl The weak handle to the WebSocket connection.
   */
  void on_close(std::weak_ptr<void> weak_hdl)
  {
    std::lock_guard<std::mutex> lock(client_conn_mutex_);
    client_connections_.erase(weak_hdl);
    std::cout << "A client connection has been closed!" << std::endl;
  }

  // Member variables
  std::vector<persistent_data::StockData> persistent_list_;
  boost::lockfree::queue<finnhub_json::DataSlice>& queue_;
  std::queue<std::string>& unsubscribe_list_;
  std::mutex& unsubscribe_pipe_mutex_;
  std::condition_variable& condition_variable_;
  std::thread server_thread_, monitor_thread_;
  websocketpp::server<websocketpp::config::asio> server_;
  std::set<
    websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> client_connections_;
  std::mutex client_conn_mutex_;
};

#endif  // STOCK_MARKER_CUSTOM_UI_SERVER_HPP_
