#pragma once


#include <boost/asio/ssl.hpp>
#include <string>

namespace ssl = boost::asio::ssl; // from <boost/asio/ssl.hpp>

namespace detail {

template<class = void>
void
load_certificates_and_keys(ssl::context& ctx, boost::system::error_code& ec)
{
  /* Daemon certificate is here */
  std::string const daemon_cert =
    /* BEGIN DAEMON CERT */
    "FILL"
    "HERE"
    "CERTIFICATE"
    /* END DAEMON CERT */
    ;


  /* Our own certificate is here */
  std::string const own_cert =
    /* BEGIN BROWSER CERT */
    "FILL"
    "HERE"
    "CERTIFICATE"
    /* END BROWSER CERT */
    ;

  /* Our private key */
  std::string const own_key =
    /* BEGIN BROWSER KEY */
    "FILL"
    "HERE"
    "CERTIFICATE"
    /* END BROWSER KEY */
    ;

  ctx.add_certificate_authority(boost::asio::buffer(daemon_cert.data(), daemon_cert.size()), ec);
  if(ec)
    return;

  ctx.use_certificate(boost::asio::buffer(own_cert.data(), own_cert.size()), boost::asio::ssl::context::file_format::pem, ec);
  if(ec)
    return;

  ctx.use_rsa_private_key(boost::asio::buffer(own_key.data(), own_key.size()), boost::asio::ssl::context::file_format::pem, ec);
  if(ec)
    return;
}

} // end namespace detail


inline
void
load_certificates_and_keys(ssl::context& ctx, boost::system::error_code& ec)
{
  detail::load_certificates_and_keys(ctx, ec);
}

inline
void
load_certificates_and_keys(ssl::context& ctx)
{
  boost::system::error_code ec;
  detail::load_certificates_and_keys(ctx, ec);
  if(ec)
    throw boost::system::system_error{ec};
}
