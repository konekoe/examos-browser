#pragma once

#include <boost/regex.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <gtkmm.h>
#include <gtkmm/window.h>
#include <gtkmm/button.h>
#include <gtkmm/box.h>
#include <gtkmm/textview.h>
#include <gtkmm/label.h>
#include <gtkmm/image.h>
#include <gtkmm/invisible.h>
#include <gtkmm/settings.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/messagedialog.h>
#include <gdkmm/rgba.h>
#include <gdk/gdk.h>
#include <webkit2/webkit2.h>
#include <functional>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iterator>
#include <algorithm>
#include <boost/asio.hpp>
#include <future>
#include <chrono>
#include <thread>

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#include "ConnHandler.hpp"

#include "json.hpp"
using json = nlohmann::json;

class WebBrowser;

typedef void (WebBrowser::*ParserFunction)(json& j);
typedef std::map<std::string, ParserFunction> parser_list_t;

class WebBrowser : public Gtk::Window
{
public:
  WebBrowser(const std::string url = "UNDEFINED");
  virtual ~WebBrowser();

  void HandleMessage(const std::string& jstring);

  void ClearBaseName();

  void AddName(std::string name);

  void Login(std::string URI);

  void SendURLLogData(const std::string& URL);

  void CheckStudentNo(GInputStream* stream);

  void GoBack();
  void GoForward();
  void ReloadPage();
  void ZoomIn();
  void ZoomOut();

private:
  void HandleMessages();
  bool MatchWildCards(const std::string& text, std::string wildcardPattern);

  /* containers */
  Gtk::VBox mMainContainer;
  Gtk::HBox mMenuContainer;
  Gtk::HBox mBrowserContainer;

  /* Buttons */
  Gtk::Button mBackButton;
  Gtk::Button mForwardButton;
  Gtk::Button mReloadButton;
  Gtk::Button mZoomInButton;
  Gtk::Button mZoomOutButton;

  /* Web stuffs */
  WebKitWebView* mWebView;
  WebKitWebContext* mWebContext;

  /* Web restriction */
  std::string mBaseName;
  std::vector<std::string> mNameList;

  /* Connection handler to daemon */
  ConnHandler mConnHandler;
  std::thread mConnHandlerThread;

  /* Polling of events */
  bool TimeoutPoll(int& number);
  sigc::connection mPollConn;

  /* Message Parsing */
  void ParseExamURI(json& j);
  parser_list_t mMessageParsers;
};