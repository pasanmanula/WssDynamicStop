# Finnhub WebSocket Server and Client

# Application Threading Model

## Overview

This simple application will have five threads running concurrently, ensuring efficient handling of WebSocket communication, management of the unsubscribe list, and monitoring of the data queue.

## Dependencies

OpenSSL & Boost

## API Dependencies

You need to create an account to get your own API key from finnhub.io and paste it inside config/finnhub_api_key.txt
(link:- https://finnhub.io/register )

Modify your own stock monitor list. A sample json for a stock is provided. Please check config/stock_list.json file.

## Threads

### Main Thread

- **Main Thread**: Executes the `main` function.

### FinnhubWssConnection Threads

- **WebSocket Connection Thread**: Executes the `init_and_start_wss_connection` method.
- **Feedback Monitor Thread**: Executes the `monitor_unsubscribe_list` method.

### WebSocketServer Threads

- **Server Thread**: Executes the `init_and_run_wss_server` method.
- **Monitor Data Pipe Thread**: Executes the `check_data_queue` method.

## Dashboard

Use dashboard.html file to monitor the stocks realtime. Please note that this dashboard.html file lacks of styling and feel free to modify with new frameworks as you wish!

## Summary

This threading model allows this application to handle WebSocket communication with Finnhub, manage the unsubscribe list, and monitor the data queue concurrently.

Please note that this is still working in progress work (As of now, still need to "fully" implement "Close Position" feature). I am going to update this as time permitted.
