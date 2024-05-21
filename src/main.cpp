#include <boost/lockfree/queue.hpp>
#include "constants.hpp"
#include "retrieve_metadata.hpp"
#include "custom_json_parser.hpp"
#include "finnhub_conn_client.hpp"
#include "custom_ui_server.hpp"

int main()
{
  // Only shared resource between websocket client and websocker server to ui.
  boost::lockfree::queue<finnhub_json::DataSlice> queue(kDataPipeMaxSize);
  std::queue<std::string> unsubscribe_list;

  //  ThreadConditions thread_sync;
  std::mutex unsubscribe_pipe_mutex;
  std::condition_variable condition_variable;

  FinnhubWssConnection finnhub_conn_client(
    queue,
    unsubscribe_list,
    unsubscribe_pipe_mutex,
    condition_variable,
    persistent_data::get_finnhub_uri(),
    persistent_data::get_ticker_list_from_file());

  WebSocketServer websocket_server(
    queue,
    unsubscribe_list,
    unsubscribe_pipe_mutex,
    condition_variable,
    kWebSocketServerPort);

  return 0;
}