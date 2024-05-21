#ifndef STOCK_MARKER_FINNHUB_CONNECTION_CLIENT_HPP_
#define STOCK_MARKER_FINNHUB_CONNECTION_CLIENT_HPP_

#include <string>
#include <thread>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <boost/lockfree/queue.hpp>
#include "websocketpp/config/asio_client.hpp"
#include "websocketpp/client.hpp"

class FinnhubWssConnection {
public:
  /**
   * @brief Constructs a FinnhubWssConnection object with the specified WebSocket URI.
   * 
   * This constructor initializes a FinnhubWssConnection object with the given WebSocket URI.
   * It also starts a separate thread to initialize and start the WebSocket connection.
   * 
   * @param finnhub_wss_uri The WebSocket URI for the Finnhub WebSocket connection.
   */
  explicit FinnhubWssConnection(
    boost::lockfree::queue<finnhub_json::DataSlice>& queue,
    std::queue<std::string>& unsubscribe_list,
    std::mutex& unsubscribe_pipe_mutex,
    std::condition_variable& condition_variable,
    const std::string& finnhub_wss_uri,
    const std::unordered_map<std::string, persistent_data::StockData>& ticker_list)
      : queue_(queue),
        unsubscribe_list_(unsubscribe_list),
        unsubscribe_pipe_mutex_(unsubscribe_pipe_mutex),
        condition_variable_(condition_variable),
        wss_uri_{std::move(finnhub_wss_uri)},
        persistent_data_(std::move(ticker_list)),
        thread_{std::thread(&FinnhubWssConnection::init_and_start_wss_connection, this)}
  {

  }

  /**
   * @brief Destroys the FinnhubWssConnection object.
   * 
   * This destructor waits for the thread that was started in the constructor
   * to finish its execution before destroying the FinnhubWssConnection object.
   * It ensures that all resources associated with the WebSocket connection are properly cleaned up.
   */
  ~FinnhubWssConnection()
  {
    if (thread_.joinable())
    {
      thread_.join();
    }
  }

private:
  /**
   * @brief Initializes and starts a WebSocket Secure (WSS) connection.
   * 
   * This function initializes the WebSocket client, sets up various handlers,
   * and starts the connection to the specified URI.
   */
  void init_and_start_wss_connection()
  {
    initialize_client();
    start_connection();
  }

  void monitor_unsubscribe_list(std::weak_ptr<void> hdl)
  {
    while (true) {
      std::unique_lock<std::mutex> lock(unsubscribe_pipe_mutex_);

      // Wait until there is data in the queue
      condition_variable_.wait(lock, [this]{ return !unsubscribe_list_.empty(); });
  
      // Process the data
      while (!unsubscribe_list_.empty()) {
        std::string unsubscribe_ticker_symbol = unsubscribe_list_.front();
        unsubscribe_list_.pop();
        const std::string subscription =
          R"({"type":"unsubscribe","symbol":")" + unsubscribe_ticker_symbol + R"("})";

        // Lock the weak_ptr to obtain a shared_ptr
        if (auto shared_hdl = hdl.lock()) {
          client_.send(shared_hdl, subscription, websocketpp::frame::opcode::text);
          std::lock_guard<std::mutex> persistent_lock(persistent_data_mutex_);
          if (persistent_data_.find(unsubscribe_ticker_symbol) != persistent_data_.end()) {
            persistent_data_.erase(unsubscribe_ticker_symbol);
            std::cout << "Unsubscribe to: " << unsubscribe_ticker_symbol << std::endl;
          }
        } else {
          std::cout << "Connection handle is no longer valid." << std::endl;
        }
        
      }
    }
  }

  /**
  * @brief Initializes the WebSocket client and sets up event handlers.
  * 
  * This function configures the WebSocket client by setting the log level,
  * initializing ASIO, and registering handlers for various events like 
  * connection open, connection fail, incoming message, and connection close.
  */
  void initialize_client()
  {
    client_.clear_access_channels(websocketpp::log::alevel::all);  
    client_.init_asio();   
    client_.set_open_handler(
        std::bind(&FinnhubWssConnection::on_open, this, std::placeholders::_1));
    client_.set_fail_handler(
        std::bind(&FinnhubWssConnection::on_fail, this, std::placeholders::_1));
    client_.set_message_handler(
        std::bind(
            &FinnhubWssConnection::on_message, this,
            std::placeholders::_1, std::placeholders::_2));
    client_.set_close_handler(
        std::bind(&FinnhubWssConnection::on_close, this, std::placeholders::_1));  
    client_.set_tls_init_handler(
        std::bind(&FinnhubWssConnection::on_tls_init, this, std::placeholders::_1));
  }

  /**
  * @brief Starts the WebSocket connection.
  * 
  * This function creates a connection to the specified URI and starts the 
  * ASIO io_service run loop to handle network events.
  */
  void start_connection()
  {
    // Create a conn to the given URI and queue it for connection once the event loop starts
    connection_ptr_ = client_.get_connection(wss_uri_, error_code_);
    if (error_code_)
    {
      std::cerr << "Could not create connection: " << error_code_.message() << std::endl;
      return;
    }
    client_.connect(connection_ptr_);
    connection_hdl_ = connection_ptr_->get_handle();
    // Start the ASIO io_service run loop
    client_.run(); 
  }

  /**
   * @brief Handler for the WebSocket connection open event.
   * 
   * @param hdl The handle to the WebSocket connection.
   */
  void on_open(websocketpp::connection_hdl hdl)
  {
    std::lock_guard<std::mutex> persistent_lock(persistent_data_mutex_);

    for(const auto& [ticker_symbol, other] : persistent_data_)
    {
      const std::string subscription = R"({"type":"subscribe","symbol":")" + ticker_symbol + R"("})";
      client_.send(hdl, subscription, websocketpp::frame::opcode::text);
      client_.get_alog().write(
        websocketpp::log::alevel::app, "Subscription requested for -> " + ticker_symbol);
      std::cout << "Subscription requested for ticker : " << ticker_symbol << std::endl;
    }

    std::cout << "All subscriptions have been requested!\n";

    hdl_ = hdl;
    if (!initialized_) {
      feedback_monitor_thread_ = std::thread(&FinnhubWssConnection::monitor_unsubscribe_list, this, std::weak_ptr<void> (hdl_));
      initialized_ = false;
    }
    
  }

  /**
  * @brief Handler for the WebSocket connection fail event.
  * 
  * @param weak_hdl The weak handle to the WebSocket connection.
  */
  void on_fail(std::weak_ptr<void> weak_hdl)
  {
    client_.get_alog().write(websocketpp::log::alevel::app, "Connection Failed. Trying to reconnect.");
    start_connection();
  }

  /**
   * @brief Handler for incoming WebSocket messages.
   * 
   * @param weak_hdl The weak handle to the WebSocket connection.
   * @param msg The WebSocket message received.
   */
  void on_message(std::weak_ptr<void> weak_hdl,
    websocketpp::config::asio_tls_client::message_type::ptr msg)
  {
    std::lock_guard<std::mutex> persistent_lock(persistent_data_mutex_);
    if(!finnhub_json::validate_json(msg->get_payload(), queue_, persistent_data_))
    {
      // std::cerr << "Data received, but unable to push to the queue" << std::endl;
    }
  }

  /**
   * @brief Handler for the WebSocket connection close event.
   * 
   * @param weak_hdl The weak handle to the WebSocket connection.
   */
  void on_close(std::weak_ptr<void> weak_hdl)
  {
    client_.close(weak_hdl, websocketpp::close::status::normal, "");
    client_.get_alog().write(websocketpp::log::alevel::app, "Connection Closed");
  }

  /**
   * @brief Handler for initializing the TLS context for secure connections.
   * 
   * This function is responsible for setting up the TLS context for secure 
   * WebSocket connections.
   * 
   * @param weak_hdl The weak handle to the WebSocket connection.
   * @return A shared pointer to the initialized TLS context.
   */
  websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>
    on_tls_init(std::weak_ptr<void> weak_hdl)
  {
      context_ptr_ = websocketpp::lib::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12);
      try {
        context_ptr_->set_options(asio::ssl::context::default_workarounds |
            asio::ssl::context::no_sslv2 |
            asio::ssl::context::no_sslv3 |
            asio::ssl::context::single_dh_use);
        context_ptr_->set_verify_mode(asio::ssl::verify_none);
      } catch (std::exception& e) {
        std::cout << e.what() << std::endl;
      }
      return context_ptr_;
  }

  // Member variables
  boost::lockfree::queue<finnhub_json::DataSlice>& queue_;
  std::queue<std::string>& unsubscribe_list_;
  std::mutex& unsubscribe_pipe_mutex_;
  std::condition_variable& condition_variable_;
  std::string wss_uri_;
  std::unordered_map<std::string, persistent_data::StockData> persistent_data_;
  std::thread thread_, feedback_monitor_thread_;
  websocketpp::client<websocketpp::config::asio_tls_client> client_;
  websocketpp::client<websocketpp::config::asio_tls_client>::connection_ptr connection_ptr_;
  websocketpp::lib::error_code error_code_;
  websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> context_ptr_;
  websocketpp::connection_hdl connection_hdl_;
  websocketpp::connection_hdl hdl_;
  std::mutex persistent_data_mutex_;
  bool initialized_;
};

#endif  // STOCK_MARKER_FINNHUB_CONNECTION_CLIENT_HPP_