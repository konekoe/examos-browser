#include <gtkmm/application.h>
#include <boost/asio.hpp>
#include <arpa/inet.h>
#include <cstring>
#include <vector>
#include <istream>
#include "WebBrowser.hpp"

int main(int argc, char *argv[])
{

  /* Create the GTK application */
  Glib::RefPtr<Gtk::Application> browserApp = Gtk::Application::create();

  /* Check that the process name is right */
  //if(std::strncmp(argv[0], "/usr/bin/examtool-browser", std::strlen("/usr/bin/examtool-browser")))
  //{
  //  return -1;
  //}

  std::string urlStr;

  /* Start browser with custom URL */
  if(argc == 2)
  {
    urlStr = argv[1];
    /* Create final URL with sanity checks */
    if(urlStr.compare(0, std::strlen("http"), "http"))
    {
      std::stringstream ss;
      ss << "http://";
      urlStr = ss.str() + urlStr;
    }
    WebBrowser browser(urlStr);
    browserApp->run(browser);
  }
  /* If no URL is given, we need to retrieve the URL from the daemon */
  else
  {
    WebBrowser browser("UNDEFINED");
    browserApp->run(browser);
  }
  return 0;
}