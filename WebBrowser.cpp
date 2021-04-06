#include "WebBrowser.hpp"

#include "json.hpp"
using json = nlohmann::json;


static void GeneralJSCallback(GObject* object, GAsyncResult* result, void* user_data)
{
  return;
}


static void WebLoadFail(WebKitWebView* webView, WebKitLoadEvent loadEvent, void* classPtr)
{
  // just debugging
  std::cerr << "LOAD FAILED" << std::endl;
}

static void WebDataLoadCallback(GObject* source_object, GAsyncResult* result, void* classPtr)
{
  WebKitWebView* webView = WEBKIT_WEB_VIEW(source_object);
  GError* error = nullptr;
  GInputStream* inStream = webkit_web_view_save_finish(webView, result, &error);
  if(inStream)
  {
    ((WebBrowser*)classPtr)->CheckStudentNo(inStream);
  }
  else
  {
    std::cerr << "error saving: " << error->message << std::endl;
  }
}

static void WebLoadStateChange(WebKitWebView* webView, WebKitLoadEvent loadEvent, void* classPtr)
{
  switch(loadEvent)
  {
    case WEBKIT_LOAD_STARTED:
    {
      ((WebBrowser*)classPtr)->ClearBaseName();
      break;
    }
    case WEBKIT_LOAD_FINISHED:
    {
      std::string load_uri = webkit_web_view_get_uri(webView);

      /* 
       * Send analytic info to server
       */
      WebBrowser* browser = static_cast<WebBrowser*>(classPtr);
      browser->SendURLLogData(load_uri);

      /* 
       * Check if we have successfully logged in
       * This is currently checked here, as the redirection
       * check is not working for some reason.
       */
      size_t startToken = load_uri.find("examToken=");
      if(startToken != std::string::npos)
      {
        browser->Login(load_uri);
      }

      // hacky hack to prevent drags :-)
      webkit_web_view_run_javascript(webView, "document.addEventListener(\"dragstart\", function(evt) { evt.preventDefault(); });", nullptr, GeneralJSCallback, nullptr);
      ((WebBrowser*)classPtr)->ClearBaseName();
      webkit_web_view_save(webView, WEBKIT_SAVE_MODE_MHTML, nullptr, WebDataLoadCallback, classPtr);
      break;
    }
    case WEBKIT_LOAD_REDIRECTED:
    default:
    {
      break;
    }
  }
}


static void loadFailedWithTLSerrors(WebKitWebView *webView, const char *failingURI, GTlsCertificate *certificate, GTlsCertificateFlags errors, void* classPtr)
{
  SoupURI *uri = soup_uri_new(failingURI);
  webkit_web_context_allow_tls_certificate_for_host(webkit_web_view_get_context(webView), certificate, uri->host);
  soup_uri_free(uri);
  webkit_web_view_load_uri(webView, failingURI);
}

static GtkWidget* createWindow(WebKitWebView *web_view, WebKitNavigationAction *navigation_action, void* classPtr)
{
  WebKitURIRequest* URIreq = webkit_navigation_action_get_request(navigation_action);
  const gchar* reqStr = webkit_uri_request_get_uri(URIreq);

  int pid = fork();
  if(pid < 0)
  {
    return nullptr;
  }
  else if(pid == 0)
  {
    char *const child_args[]={ const_cast<char *>("examtool-browser"), const_cast<char *>(reqStr), NULL };
    execv("/usr/bin/examtool-browser", child_args);
    exit(0);
  }

  return nullptr;
}


/* See https://webkitgtk.org/reference/webkit2gtk/unstable/WebKitContextMenuItem.html#WebKitContextMenuAction */
static bool editContextMenu(WebKitWebView* webView, WebKitContextMenu* contextMenu, GdkEvent* event, 
  WebKitHitTestResult* hit_test_result, gpointer user_data)
{
  std::vector<WebKitContextMenuItem *> toRemove;
  int numItems = webkit_context_menu_get_n_items(contextMenu);
  for(int i = 0; i < numItems; i++)
  {
    WebKitContextMenuItem* curr = webkit_context_menu_get_item_at_position(contextMenu, i);
    WebKitContextMenuAction cAction = webkit_context_menu_item_get_stock_action(curr);
    if(!(cAction == WEBKIT_CONTEXT_MENU_ACTION_NO_ACTION ||
       cAction == WEBKIT_CONTEXT_MENU_ACTION_OPEN_LINK ||
       cAction == WEBKIT_CONTEXT_MENU_ACTION_COPY_IMAGE_TO_CLIPBOARD ||
       cAction == WEBKIT_CONTEXT_MENU_ACTION_GO_BACK || 
       cAction == WEBKIT_CONTEXT_MENU_ACTION_GO_FORWARD || 
       cAction == WEBKIT_CONTEXT_MENU_ACTION_STOP || 
       cAction == WEBKIT_CONTEXT_MENU_ACTION_RELOAD || 
       cAction == WEBKIT_CONTEXT_MENU_ACTION_COPY || 
       cAction == WEBKIT_CONTEXT_MENU_ACTION_CUT || 
       cAction == WEBKIT_CONTEXT_MENU_ACTION_PASTE || 
       cAction == WEBKIT_CONTEXT_MENU_ACTION_DELETE || 
       cAction == WEBKIT_CONTEXT_MENU_ACTION_SELECT_ALL ||
       cAction == WEBKIT_CONTEXT_MENU_ACTION_OPEN_LINK_IN_NEW_WINDOW ))
    {
      toRemove.push_back(curr);
    }
  }

  for(auto it = toRemove.begin(); it != toRemove.end(); it++)
  {
    webkit_context_menu_remove(contextMenu, *it);
  }

  return false;
}

void resourceLoadStartedDebug(WebKitWebView* webView, WebKitWebResource* resource, WebKitURIRequest* request, void* classPtr)
{
  std::string ReqURI = webkit_uri_request_get_uri(request);

  /* Find the webserver name */
  size_t startRawName = ReqURI.find("://");
  if(startRawName == std::string::npos)
  {
    return;
  }
  std::string startRawNameStr = ReqURI.substr(startRawName + 3);

  size_t endRawName = startRawNameStr.find("/");
  if(endRawName == std::string::npos)
  {
    return;
  }
  std::string rawName(startRawNameStr, 0, endRawName);

  ((WebBrowser*)classPtr)->AddName(rawName);
  return;
}


static bool decidePolicy(WebKitWebView* webView, WebKitPolicyDecision* decision, WebKitPolicyDecisionType decision_type, void* classPtr)
{
  switch(decision_type)
  {
    case WEBKIT_POLICY_DECISION_TYPE_RESPONSE:
    {
      WebKitResponsePolicyDecision* policydec = WEBKIT_RESPONSE_POLICY_DECISION(decision);
      if(!webkit_response_policy_decision_is_mime_type_supported(policydec))
      {
        webkit_policy_decision_download(decision);
      }
      else
      {
        webkit_policy_decision_use(decision);
      }
      break;
    }
    default:
    {
      return false;
    }
  }
  return true;
}


static bool downloadFailedFlag = false;
static bool downloadCanceledByUser = false;

static bool downloadDecideDestination(WebKitDownload* download, char* suggested_filename, void* classPtr)
{
  WebBrowser* browser = (WebBrowser*)classPtr;

  Gtk::FileChooserDialog file_dialog(*(Gtk::Window*)browser, "Download file", Gtk::FILE_CHOOSER_ACTION_SAVE, Gtk::DIALOG_DESTROY_WITH_PARENT);
  file_dialog.set_current_name(suggested_filename);
  file_dialog.set_current_folder("/home/student");
  file_dialog.set_do_overwrite_confirmation(true);
  file_dialog.add_button("Save", Gtk::RESPONSE_OK);
  file_dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);


  int result = file_dialog.run();

  if(result == Gtk::RESPONSE_OK)
  {
    downloadCanceledByUser = false;
    webkit_download_set_destination(download, file_dialog.get_uri().c_str());
  }
  else
  {
    downloadCanceledByUser = true;
    webkit_download_cancel(download);
  }
  
  return false;
}


static void downloadFailed(WebKitDownload* download, GError* error, void* classPtr)
{
  if(downloadCanceledByUser)
  {
    return;
  }
  downloadFailedFlag = true;
  WebBrowser* browser = (WebBrowser*)classPtr;
  Gtk::MessageDialog dialog(*(Gtk::Window*)browser, "Download failed", false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, false);
  std::stringstream ss;
  ss << "Downloading of file " << webkit_download_get_destination(download) << " has failed. Error message was: " << error->message;
  dialog.set_secondary_text(ss.str());
  dialog.run();
}


static void downloadFinished(WebKitDownload* download, void* classPtr)
{
  if(downloadFailedFlag || downloadCanceledByUser)
    return;
  WebBrowser* browser = (WebBrowser*)classPtr;
  Gtk::MessageDialog dialog(*(Gtk::Window*)browser, "Download finished", false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK, false);
  std::stringstream ss;
  ss << "Downloading of file " << webkit_download_get_destination(download) << " completed.";
  dialog.set_secondary_text(ss.str());
  dialog.run();
}


static void downloadStarted(WebKitWebContext* webContext, WebKitDownload* download, void* classPtr)
{
  downloadFailedFlag = false;
  g_signal_connect(download, "decide-destination", G_CALLBACK(downloadDecideDestination), classPtr);
  g_signal_connect(download, "failed", G_CALLBACK(downloadFailed), classPtr);
  g_signal_connect(download, "finished", G_CALLBACK(downloadFinished), classPtr);
  return;
}





WebBrowser::WebBrowser(const std::string url)
: mConnHandler()
{
  /* Set window properties */
  set_title("Exam Browser");
  set_border_width(0);
  set_default_size(1000, 600);

  /* Create main container (window) and add menu and browser */
  add(mMainContainer);
  mMainContainer.pack_start(mMenuContainer, Gtk::PACK_SHRINK);
  mMainContainer.pack_start(mBrowserContainer, Gtk::PACK_EXPAND_WIDGET);

  /* Insert buttons to menu */
  mMenuContainer.pack_start(mBackButton, Gtk::PACK_SHRINK);
  mMenuContainer.pack_start(mForwardButton, Gtk::PACK_SHRINK);
  mMenuContainer.pack_start(mReloadButton, Gtk::PACK_SHRINK);
  mMenuContainer.pack_start(mZoomInButton, Gtk::PACK_SHRINK);
  mMenuContainer.pack_start(mZoomOutButton, Gtk::PACK_SHRINK);

  /* Button labels */
  mBackButton.set_label("◁");
  mForwardButton.set_label("▷");
  mReloadButton.set_label("↻");
  mZoomInButton.set_label("+");
  mZoomOutButton.set_label("-");

  /* Web stuffs */

  // Create DataManager which stores data to the system..
  std::string homedir = getenv("HOME");
  if (homedir.empty())
  {
    homedir = getpwuid(getuid())->pw_dir;
  }

  WebKitWebsiteDataManager* dataManager = webkit_website_data_manager_new("base-cache-directory", (homedir + "/.local/share/browser/basecache").c_str(), 
                                                                          "base-data-directory", (homedir + "/.local/share/browser/basedata").c_str(), 
                                                                          "disk-cache-directory", (homedir + "/.local/share/browser/diskcache").c_str(), 
                                                                          "indexeddb-directory", (homedir + "/.local/share/browser/indexeddb").c_str(),
                                                                          "local-storage-directory", (homedir + "/.local/share/browser/localstorage").c_str(),
                                                                          "offline-application-cache-directory", (homedir + "/.local/share/browser/offlineapps").c_str(),
                                                                          "websql-directory", (homedir + "/.local/share/browser/websql").c_str(),
                                                                          NULL);
  mWebContext = webkit_web_context_new_with_website_data_manager(dataManager);


  // This seems unneeded, as the datamanager doesn't actually do anything regarding cookie saving,
  // it might even be that the datamanager is not needed at all ...
  // WebKitCookieManager* cookieManager = webkit_website_data_manager_get_cookie_manager(dataManager);
  // webkit_cookie_manager_set_persistent_storage(cookieManager, "cookies.txt", WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
  // webkit_cookie_manager_set_accept_policy(cookieManager, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);

  // The actual cookie management gets the manager from the WebCtx...
  WebKitCookieManager* cookieManager = webkit_web_context_get_cookie_manager(mWebContext);
  webkit_cookie_manager_set_persistent_storage(cookieManager, (homedir + "/.local/share/browser/cookies.txt").c_str(), WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
  webkit_cookie_manager_set_accept_policy(cookieManager, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);

  // WebView creation, this is the GTK module that actually shows the website
  mWebView = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(mWebContext));


  /* Dirty fix for tim.aalto.fi, this allows every BAD TLS certs */
  webkit_web_context_set_tls_errors_policy(mWebContext, WEBKIT_TLS_ERRORS_POLICY_IGNORE);

  // Connect webkit2gtk signals where needed
  g_signal_connect(mWebView, "load-changed", G_CALLBACK(WebLoadStateChange), this);
  g_signal_connect(mWebView, "load-failed", G_CALLBACK(WebLoadFail), this);
  g_signal_connect(mWebView, "context-menu", G_CALLBACK(editContextMenu), NULL);
  g_signal_connect(mWebView, "resource-load-started", G_CALLBACK(resourceLoadStartedDebug), this);
  g_signal_connect(mWebView, "create", G_CALLBACK(createWindow), this);
  g_signal_connect(mWebView, "decide-policy", G_CALLBACK(decidePolicy), this);
  g_signal_connect(mWebContext, "download-started", G_CALLBACK(downloadStarted), this);
  g_signal_connect(mWebView, "load-failed-with-tls-errors", G_CALLBACK(loadFailedWithTLSerrors), this);

  mBackButton.signal_clicked().connect(sigc::mem_fun(*this, &WebBrowser::GoBack));
  mForwardButton.signal_clicked().connect(sigc::mem_fun(*this, &WebBrowser::GoForward));
  mReloadButton.signal_clicked().connect(sigc::mem_fun(*this, &WebBrowser::ReloadPage));
  mZoomInButton.signal_clicked().connect(sigc::mem_fun(*this, &WebBrowser::ZoomIn));
  mZoomOutButton.signal_clicked().connect(sigc::mem_fun(*this, &WebBrowser::ZoomOut));

  /* Create the WebView (and its Gtk::Widget), add to the container */
  auto widget = Gtk::manage(Glib::wrap(GTK_WIDGET(mWebView)));
  mBrowserContainer.pack_start(*widget, Gtk::PACK_EXPAND_WIDGET);

  /* Disable drag and drop */
  Glib::RefPtr<Gtk::Settings> settings = widget->get_settings();
  settings->property_gtk_dnd_drag_threshold() = 100000;

  widget->drag_source_unset();
  widget->drag_dest_unset();
  //widget->drag_source_set(std::vector<Gtk::TargetEntry>({
  //  Gtk::TargetEntry("STRING", Gtk::TargetFlags::TARGET_SAME_WIDGET, 0)/*,
  //  Gtk::TargetEntry("text/plain", static_cast<Gtk::TargetFlags>(0), 0)*/
  //}));

  show_all_children();
  widget->show();

  /* Set up message parsers for connhandler */
  mMessageParsers = {
    {"exam_uri", &WebBrowser::ParseExamURI}
  };

  /* Connect to the daemon, set up connhandler */
  mConnHandlerThread = mConnHandler.Run();

  if(url != "UNDEFINED")
  {
    webkit_web_view_load_uri(mWebView, url.c_str());
  }
  else
  {
    std::cerr << "url was UNDEFINED, requesting from daemon..." << std::endl;
    json req_url = {
          {"type", "request_exam_uri"},
          {"payload", nullptr}
        };
    mConnHandler.SendData(req_url.dump());
  }

  /* Start message polling */
  sigc::slot<bool()> pollSlot = sigc::bind(sigc::mem_fun(*this, &WebBrowser::TimeoutPoll), 0);
  mPollConn = Glib::signal_timeout().connect(pollSlot, 1000);
}


WebBrowser::~WebBrowser()
{
  mConnHandler.CloseWebSock();
  mConnHandlerThread.join();
}


/* Validates that "key" object exists */
static bool ValidateJSON(json& j, const std::string& key)
{
  //spdlog::debug("ValidateJSON: validating key: {}", key);
  if(j.find(key) != j.end())
  {
    return true;
  }
  //spdlog::critical("Could not find needed key {}!", key);
  return false;
}

template<typename ...Args>
static bool ValidateJSON(json& j, const std::string& key_first, Args... args)
{
  if(!ValidateJSON(j, key_first)) return false;
  else return ValidateJSON(j, args...);
}


void WebBrowser::ParseExamURI(json& j)
{
  if(!ValidateJSON(j, "exam_uri_string") || !j["exam_uri_string"].is_string())
  {
    // TODO: log error!
    return;
  }
  std::string examURI = j["exam_uri_string"];
  webkit_web_view_load_uri(mWebView, examURI.c_str());
  return;
}


void WebBrowser::HandleMessage(const std::string& jstring)
{
  json j;
  try
  {
    j = json::parse(jstring);
  }
  catch(const std::exception& ex)
  {
    // TODO: send error msg to daemon! 
    // maybe also log this?
  }

  // TODO: check if the payload is missing and error is present instead:
  if(j.find("error") != j.end())
  {
    return;
  }

  if(j.find("payload") == j.end())
  {
    // TODO: THIS JSON IS NOT A VALID KONEKOE JSON MESSAGE,
    //       LOG THIS AND DISCARD MESSAGE
    return;
  }

  /* Parse the payload and run the handler */
  try
  {
    std::invoke(mMessageParsers.at(j["type"]), *this, j["payload"]);
  }
  catch(const std::exception& e)
  {
    // TODO: NO PARSER FOUND! SEND THIS LOG TO THE SERVER
    //spdlog::debug("No server parser found for key: {} !", j["type"]);
  }

  return;
}


bool WebBrowser::TimeoutPoll(int& number)
{
  std::mutex& mtx = mConnHandler.GetMsgMutex();
  mtx.lock();
  std::queue<message_t>& queue = mConnHandler.GetMsgQue();
  while(!queue.empty())
  {
    message_t msg = queue.front();
    HandleMessage(msg.message);
    queue.pop();
  }
  mtx.unlock();
  return true;
}


void WebBrowser::ClearBaseName()
{
  mBaseName.clear();
}

void WebBrowser::AddName(std::string name)
{
  if(mBaseName.empty())
  {
    mBaseName = name;
  }
  else if(name == mBaseName)
  {
    return;
  }
  else
  {
    if(std::find(mNameList.begin(), mNameList.end(), name) == mNameList.end())
    {
      /* Send proposed new name to add to the whitelist to the daemon */
      json add_url = {
          {"type", "allow_url"},
          {"payload", {
            {"base_url", mBaseName},
            {"req_url", name}
          }}
        };

      mConnHandler.SendData(add_url.dump());
    }
  }
}


void WebBrowser::Login(std::string URI)
{
  size_t delim_pos = URI.find("examToken=");
  if(delim_pos == std::string::npos)
  {
    return;
  }
  std::string string_start = URI.substr(delim_pos + std::strlen("examToken="));
  delim_pos = string_start.find("&");
  if(delim_pos == std::string::npos)
  {
    std::cerr << "Unexpected login sequence in WebBrowser::Login!" << std::endl;
    return;
  }

  std::string token(string_start, 0, delim_pos);

  delim_pos = string_start.find("examUser=");
  if(delim_pos == std::string::npos)
  {
    std::cerr << "Unexpected login sequence in WebBrowser::Login!" << std::endl;
    return;
  }
  string_start = string_start.substr(delim_pos + std::strlen("examUser="));
  delim_pos = string_start.find("&");
  if(delim_pos == std::string::npos)
  {
    std::cerr << "Unexpected login sequence in WebBrowser::Login!" << std::endl;
    return;
  }

  std::string user(string_start, 0, delim_pos);

  delim_pos = string_start.find("examIp=");
  if(delim_pos == std::string::npos)
  {
    std::cerr << "Unexpected login sequence in WebBrowser::Login!" << std::endl;
    return;
  }
  string_start = string_start.substr(delim_pos + std::strlen("examIp="));
  delim_pos = string_start.find("&");
  if(delim_pos == std::string::npos)
  {
    std::cerr << "Unexpected login sequence in WebBrowser::Login!" << std::endl;
    return;
  }

  std::string server_address(string_start, 0, delim_pos);

  delim_pos = string_start.find("examPort=");
  if(delim_pos == std::string::npos)
  {
    std::cerr << "Unexpected login sequence in WebBrowser::Login!" << std::endl;
    return;
  }
  string_start = string_start.substr(delim_pos + std::strlen("examPort="));

  std::string server_port(string_start);

  json j = {
      {"type", "client_login"},
      {"payload", {
        {"token", token},
        {"user", user},
        {"address", server_address},
        {"port", server_port}
      }}
    };

  mConnHandler.SendData(std::move(j.dump()));

}


void WebBrowser::SendURLLogData(const std::string& URL)
{
  json j = {
    {"type", "log_action"},
    {"payload", {
      {"url_open", URL}
    }}
  };
  mConnHandler.SendData(std::move(j.dump()));
}


void WebBrowser::CheckStudentNo(GInputStream* stream)
{
  char block[4096];
  std::vector<char> CharHTML;
  GError* error = nullptr;
  int bytes_read = 0;
  size_t total_bytes_read = 0;

  while((bytes_read = g_input_stream_read(stream, block, 4096, nullptr, &error)) > 0)
  {
    CharHTML.resize(total_bytes_read + bytes_read);
    std::copy(&block[0], &block[bytes_read], back_inserter(CharHTML));
    total_bytes_read += bytes_read;
  }
  if(bytes_read == -1)
  {
    std::cerr << "error reading from gstream: " << error->message << std::endl;
    return;
  }

  std::string StrHTML(CharHTML.begin(), CharHTML.end());

  /* Find the exam HTML tag */
  size_t startExamInfo = StrHTML.find("<exam");
  size_t endExamInfo = StrHTML.find("</exam>");

  if(startExamInfo == std::string::npos || endExamInfo == std::string::npos || !(endExamInfo > startExamInfo))
  {
    /* No exam tag found or start was greater than end, return without checking */
    return;
  }

  std::string ExamInfo = StrHTML.substr(startExamInfo, endExamInfo - startExamInfo);

  /* Parse the username field from HTML tag */
  std::string startdelim = "username=3D\"";
  size_t startName = ExamInfo.find(startdelim);
  startName += startdelim.length();
  size_t endName = ExamInfo.find("\">", startName);

  if(startName == std::string::npos || endName == std::string::npos || !(endName > startName))
  {
    /* No username field found or start was greater than end, return without checking */
    return;
  }

  std::string parsedStudNo = ExamInfo.substr(startName, endName - startName);

  if(parsedStudNo == "Anonymous" || parsedStudNo == "")
  {
    return;
  }

  json studno_check = {
      {"type", "studno_check"},
      {"payload", {
        {"exam_taker_id", parsedStudNo}
      }}
    };

  mConnHandler.SendData(studno_check.dump());

  return;
}


void WebBrowser::HandleMessages()
{
  std::mutex& mtx = mConnHandler.GetMsgMutex();
  while(true)
  {
    mtx.lock();
    std::queue<message_t>& queue = mConnHandler.GetMsgQue();
    if(queue.empty())
    {
      mtx.unlock();
      return;
    }
    message_t msg = queue.front();
    queue.pop();
    mtx.unlock();

    /* Handle messages here! */
    /* Data race if the connhandler sends new signal during this ? */
  }
  return;
}

bool WebBrowser::MatchWildCards(const std::string& text, std::string wildcardPattern)
{
  /* Escape unneeded regex special chars */
  boost::replace_all(wildcardPattern, "\\", "\\\\");
  boost::replace_all(wildcardPattern, "^", "\\^");
  boost::replace_all(wildcardPattern, ".", "\\.");
  boost::replace_all(wildcardPattern, "$", "\\$");
  boost::replace_all(wildcardPattern, "*", "\\*");
  boost::replace_all(wildcardPattern, "+", "\\+");
  boost::replace_all(wildcardPattern, "?", "\\?");
  boost::replace_all(wildcardPattern, "/", "\\/");
  boost::replace_all(wildcardPattern, ",", "|");
  boost::replace_all(wildcardPattern, "{", "(");
  boost::replace_all(wildcardPattern, "}", ")");

  // Convert needed regex special chars back to "real" regex
  boost::replace_all(wildcardPattern, "\\?", ".");
  boost::replace_all(wildcardPattern, "\\*", ".*");


  bool ret = false;

  try
  {
    boost::regex pattern(wildcardPattern, boost::regex::icase);
    ret = boost::regex_match(text, pattern);
  }
  catch(const std::exception& ex)
  {
    // TODO: log error!
  }

  return ret;
}

void WebBrowser::GoBack()
{
  if(webkit_web_view_can_go_back(mWebView))
  {
    webkit_web_view_go_back(mWebView);
  }
}


void WebBrowser::GoForward()
{
  if(webkit_web_view_can_go_forward(mWebView))
  {
    webkit_web_view_go_forward(mWebView);
  }
}


void WebBrowser::ReloadPage()
{
  webkit_web_view_reload(mWebView);
}


void WebBrowser::ZoomIn()
{
  double zoom = webkit_web_view_get_zoom_level(mWebView);
  webkit_web_view_set_zoom_level(mWebView, zoom + 0.1);
  return;
}


void WebBrowser::ZoomOut()
{
  double zoom = webkit_web_view_get_zoom_level(mWebView);
  webkit_web_view_set_zoom_level(mWebView, zoom - 0.1);
  return;
}