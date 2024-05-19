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

  FinnhubWssConnection finnhub_conn_client(
    persistent_data::get_finnhub_uri(), queue, persistent_data::get_ticker_list_from_file());

  WebSocketServer websocket_server(kWebSocketServerPort, queue);

  return 0;
}