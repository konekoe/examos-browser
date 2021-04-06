#pragma once

#include <queue>
#include <string>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <mutex>


using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>
namespace ssl = boost::asio::ssl;               // from <boost/asio/ssl.hpp>
namespace websocket = boost::beast::websocket;  // from <boost/beast/websocket.hpp>


struct message_t {
  std::string message;
};



class ConnHandler
{
public:
  ConnHandler();
  ~ConnHandler();
  void Init();
  std::thread Run();
  void SendData(const std::string&);
  bool IsConnected();
  void CloseWebSock();
  std::mutex& GetMsgMutex() { return mMessageQueueMutex; };
  std::queue<message_t>& GetMsgQue() { return mMessageQueue; };

private:
  std::mutex mMessageQueueMutex;
  std::mutex mMsgOutQueMutex;
  std::queue<message_t> mMessageQueue;
  std::queue<message_t> mMsgOutQue;
  void MainLoop();
  void Connect();
  void OnWrite(boost::system::error_code ec, std::size_t bytes_transferred);
  void OnRead(boost::system::error_code ec, std::size_t bytes_transferred);

  boost::asio::io_context m_ioctx;
  std::shared_ptr<websocket::stream<boost::beast::ssl_stream<tcp::socket>>> m_ws;
  boost::beast::multi_buffer m_inBuffer;
  boost::beast::multi_buffer m_outBuffer;

  bool m_connected;

  bool m_running;
};
