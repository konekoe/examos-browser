#include "ConnHandler.hpp"
#include "Certificates.hpp"

ConnHandler::ConnHandler() :
  m_connected(false)
{
  // For server-side connections, we must verify the server we are communicating with
  //m_sslctx.set_verify_mode(ssl::verify_peer);

  // Set the trusted certificates (only allow our own certs)
  // https://stackoverflow.com/questions/7324425/
  //m_sslctx.load_verify_file("cert.pem");
  //m_sslctx.use_certificate_file("client-cert.pem", boost::asio::ssl::context::file_format::pem);
  //m_sslctx.use_private_key_file("client-key.pem", boost::asio::ssl::context::file_format::pem);
}

ConnHandler::~ConnHandler()
{

}

void ConnHandler::Init()
{

}

std::thread ConnHandler::Run()
{
  // create thread and let it run forever
  // where do we do thread join?

  // we can take the thread and store it somewhere else, for example in the ExamTool class,
  // and join it from there. Therefore we could also monitor if the thread terminates unexpectedly.
  return std::thread(&ConnHandler::MainLoop, this);
}


void ConnHandler::OnWrite(boost::system::error_code ec, std::size_t bytes_transferred)
{
  // Clear the buffer :-)
  m_outBuffer.consume(m_outBuffer.size());

  boost::ignore_unused(bytes_transferred);
  if(ec)
  {
    std::cerr << "OnWrite: " << ec.message() << "\n";
    return;
  }

  mMsgOutQueMutex.lock();
  mMsgOutQue.pop();

  if(!mMsgOutQue.empty())
  {
    mMsgOutQueMutex.unlock();
    if(m_ws && m_ws->is_open())
    {
      size_t n = boost::asio::buffer_copy(m_outBuffer.prepare(mMsgOutQue.front().message.size()), boost::asio::buffer(mMsgOutQue.front().message));
      m_outBuffer.commit(n);

      m_ws->async_write(m_outBuffer.data(), boost::beast::bind_front_handler(&ConnHandler::OnWrite, this));
    }
  }
  mMsgOutQueMutex.unlock();
}


void ConnHandler::SendData(const std::string& data)
{
  // Send the message
  message_t newmsg{data};
  mMsgOutQueMutex.lock();
  mMsgOutQue.push(std::move(newmsg));

  if(mMsgOutQue.size() > 1 && m_ws && m_ws->is_open())
  {
    mMsgOutQueMutex.unlock();
    return;
  }
  mMsgOutQueMutex.unlock();
  if(m_ws && m_ws->is_open())
  {
    size_t n = boost::asio::buffer_copy(m_outBuffer.prepare(mMsgOutQue.front().message.size()), boost::asio::buffer(mMsgOutQue.front().message));
    m_outBuffer.commit(n);
    m_ws->async_write(m_outBuffer.data(), boost::beast::bind_front_handler(&ConnHandler::OnWrite, this));
  }
}


void ConnHandler::CloseWebSock()
{
  m_running = false;
  if(m_ws && m_ws->is_open())
    try
    {
      m_ws->close(websocket::close_code::normal);
    }
    catch(std::exception const& ex)
    {
      std::cerr << "CloseWebSock: " << ex.what() << std::endl;
    }
}


void ConnHandler::OnRead(boost::system::error_code ec, std::size_t bytes_transferred)
{
  boost::ignore_unused(bytes_transferred);
  if(ec)
  {
    std::cerr << "OnRead: " << ec.message() << "\n";
    return;
  }

  std::string message = boost::beast::buffers_to_string(m_inBuffer.data());
  message_t newmsg{message};

  mMessageQueueMutex.lock();
  mMessageQueue.push(std::move(newmsg));
  mMessageQueueMutex.unlock();

  // Clear the buffer :-)
  m_inBuffer.consume(m_inBuffer.size());

  m_ws->async_read(m_inBuffer, boost::beast::bind_front_handler(&ConnHandler::OnRead, this));  
}


void ConnHandler::MainLoop()
{
  m_running = true;
  while(true)
  {
    if(!m_running)
    {
      std::cerr << "terminating ..." << std::endl;
      return;
    }
    if(!m_ws || !m_ws->is_open())
    {
      Connect();

      // if still not connected, give the CPU some rest
      if(!m_connected)
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));

      continue;
    }

    mMsgOutQueMutex.lock();
    if(!mMsgOutQue.empty())
    {
      if(m_ws && m_ws->is_open())
      {
        size_t n = boost::asio::buffer_copy(m_outBuffer.prepare(mMsgOutQue.front().message.size()), boost::asio::buffer(mMsgOutQue.front().message));
        m_outBuffer.commit(n);

        m_ws->async_write(m_outBuffer.data(), boost::beast::bind_front_handler(&ConnHandler::OnWrite, this));
      }
    }
    mMsgOutQueMutex.unlock();

    m_ws->async_read(m_inBuffer, boost::beast::bind_front_handler(&ConnHandler::OnRead, this));

    m_ioctx.run();

    m_ws.reset();
    
    if(!m_running)
    {
      std::cerr << "terminating ..." << std::endl;
      return;
    }
  }
}


void ConnHandler::Connect()
{
  try
  {
    /* Set up the SSL */
    ssl::context sslctx{ssl::context::sslv23_client};
    load_certificates_and_keys(sslctx);
    sslctx.set_verify_mode(ssl::verify_peer);

    m_ws = std::make_shared<websocket::stream<boost::beast::ssl_stream<tcp::socket>>>(boost::asio::make_strand(m_ioctx), sslctx);

    auto const host = "0.0.0.0";
    tcp::resolver resolver{m_ioctx};
    auto const results = resolver.resolve(host, "5888");

    // connect TCP
    boost::asio::connect(m_ws->next_layer().next_layer(), results.begin(), results.end());

    // perform SSL handshake
    m_ws->next_layer().handshake(ssl::stream_base::client);

    // set appropriate timeout
    m_ws->set_option(websocket::stream_base::timeout::suggested(boost::beast::role_type::client));

    // perform the WebSocket handshake
    m_ws->handshake(host, "/");

    m_connected = true;
  }
  catch(std::exception const& ex)
  {
    std::cerr << "ERROR: " << ex.what() << std::endl;
    m_connected = false;
  }
}
