#include <array>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/format.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

namespace beast = boost::beast; // from <boost/beast.hpp>
namespace http = beast::http;   // from <boost/beast/http.hpp>
namespace net = boost::asio;    // from <boost/asio.hpp>

using boost::format;
using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>

string encode(string data) {
  using boost::algorithm::replace_all;
  replace_all(data, "&", "&amp;");
  replace_all(data, "\"", "&quot;");
  replace_all(data, "\'", "&apos;");
  replace_all(data, "<", "&lt;");
  replace_all(data, ">", "&gt;");
  replace_all(data, "\n", "&#13;");
  replace_all(data, "\r", "");
  return data;
}

class Console {
public:
  static Console &getInstance() {
    static Console console;
    return console;
  }

  struct Server {
    string host, port, file_name;
    string name() { return host + ":" + port; }
    bool exist() {
      return !(host.empty() || port.empty() || file_name.empty());
    }
  };

  bool server_is_exist(int index) { return servers[index].exist(); }

  string get_host(int index) {
    auto host = servers[index].host;
    return host;
  }

  string get_port(int index) {
    auto port = servers[index].port;
    return port;
  }

  string get_file_name(int index) {
    auto file_name = servers[index].file_name;
    return file_name;
  }

  void set_host(int index, string host) { servers[index].host = host; }
  void set_port(int index, string port) { servers[index].port = port; }
  void set_file_name(int index, string file_name) {
    servers[index].file_name = file_name;
  }

  void init() {
    string html_string =
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <title>NP Project 3</title>\n"
        "  <meta charset=\"utf-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, "
        "initial-scale=1\">\n"
        "  <link rel=\"stylesheet\" "
        "href=\"https://maxcdn.bootstrapcdn.com/bootstrap/4.5.2/css/"
        "bootstrap.min.css\">\n"
        "  <script "
        "src=\"https://ajax.googleapis.com/ajax/libs/jquery/3.5.1/"
        "jquery.min.js\"></script>\n"
        "  <script "
        "src=\"https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.16.0/umd/"
        "popper.min.js\"></script>\n"
        "  <script "
        "src=\"https://maxcdn.bootstrapcdn.com/bootstrap/4.5.2/js/"
        "bootstrap.min.js\"></script>\n"
        "<style>\n"
        "pre {color:whitesmoke}\n"
        "</style>\n"
        "</head>\n"
        "<body>\n"
        "\n"
        "<div class=\"container\">\n"
        "  <h2>Console</h2>\n"
        "  <table class=\"table table-dark\">\n"
        "    <thead>\n"
        "      <tr>\n"
        "%1%"
        "      </tr>\n"
        "    </thead>\n"
        "    <tbody>\n"
        "      <tr>\n"
        "%2%"
        "      </tr>\n"
        "    </tbody>\n"
        "  </table>\n"
        "</div>\n"
        "\n"
        "</body>\n"
        "</html>\n";

    string table;
    string content;
    for (size_t i = 0; i < 5; i++) {
      if (servers[i].exist()) {
        table += "<th>" + servers[i].name() + "</th>\n";
        content += "<td><pre id=s" + to_string(i) + "></pre></td>\n";
      }
    }
    cout << format(html_string) % table % content << endl;
  }

  void read_data(const string &id, const string &content) {
    cout << format(
                "<script>document.getElementById(\"%1%\").innerHTML+=\"%2%\";</"
                "script>") %
                id % content
         << endl;
  }
  void write_command(const string &id, const string &content) {
    cout << format("<script>document.getElementById(\"%1%\").innerHTML+=\"<b "
                   "style=color:lime>%2%</b>\";</"
                   "script>") %
                id % content
         << endl;
  }

private:
  std::array<Server, 5> servers;
};

// Report a failure
void fail(beast::error_code ec, char const *what) {
  std::cerr << what << ": " << ec.message() << "\n";
}

// Performs an HTTP GET and prints the response
class session : public std::enable_shared_from_this<session> {
  tcp::resolver resolver_;
  tcp::socket tcp_scoket;
  beast::tcp_stream stream_;
  beast::flat_buffer buffer_; // (Must persist between reads)
  std::array<char, 4096> bytes;
  http::request<http::empty_body> req_;
  http::response<http::string_body> res_;
  fstream fs;
  string session_id;

public:
  // Objects are constructed with a strand to
  // ensure that handlers do not execute concurrently.
  explicit session(net::io_context &ioc)
      : resolver_(net::make_strand(ioc)), tcp_scoket(net::make_strand(ioc)),
        stream_(net::make_strand(ioc)) {}
  // Start the asynchronous operation

  void run(char const *host, char const *port, string file_name, int version,
           string id) {
    // Set up an HTTP GET request message
    req_.version(version);
    req_.method(http::verb::get);
    // req_.target(target);
    req_.set(http::field::host, host);
    req_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    session_id = id;

    file_name = "test_case/" + file_name;
    fs.open(file_name, std::fstream::in);
    if (!fs.is_open()) {
      std::cout << "Error occurs: " << strerror(errno) << endl;
      exit(1);
    }

    // Look up the domain name
    resolver_.async_resolve(
        host, port,
        beast::bind_front_handler(&session::on_resolve, shared_from_this()));
  }

  void on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
    if (ec)
      return fail(ec, "resolve");

    // Set a timeout on the operation
    stream_.expires_after(std::chrono::seconds(30));

    // Make the connection on the IP address we get from a lookup
    stream_.async_connect(
        results,
        beast::bind_front_handler(&session::on_connect, shared_from_this()));
  }

  void on_connect(beast::error_code ec,
                  tcp::resolver::results_type::endpoint_type) {
    if (ec)
      return fail(ec, "connect");

    // Set a timeout on the operation
    stream_.expires_after(std::chrono::seconds(30));
    handle_async_read();
  }

  void handle_async_write(const string &command) {
    auto console = Console::getInstance();    
    string cmd = command + "\n";
    console.write_command(session_id, encode(cmd));
    stream_.async_write_some(
        net::buffer(cmd),
        beast::bind_front_handler(&session::on_write, shared_from_this()));
  }

  void on_write(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    if (ec)
      return fail(ec, "write");
  }

  void handle_async_read() {
    // Receive the HTTP response
    stream_.async_read_some(
        net::buffer(bytes),
        beast::bind_front_handler(&session::on_read, shared_from_this()));
  }

  void on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec == net::error::eof) {
      fs.close();

      // Gracefully close the socket
      stream_.socket().shutdown(tcp::socket::shutdown_both, ec);

      // not_connected happens sometimes so don't bother reporting it.
      if (ec && ec != beast::errc::not_connected)
        return fail(ec, "shutdown");

      // If we get here then the connection is closed gracefully
      return;
    }

    if (ec)
      return fail(ec, "read");

    string data(bytes.begin(), bytes.begin() + bytes_transferred);
    string encData = encode(data);
    auto console = Console::getInstance();
    console.read_data(session_id, encData);

    if (encData.find("% ") != string::npos) {
      string command;
      getline(fs, command);
      handle_async_write(command);
    }

    handle_async_read();
  }
};

int get_version_from_server_protocol(string server_protocol) {
  auto version = server_protocol.substr(server_protocol.find_first_of('/') + 1);
  auto dot = version.find_first_of('.');
  int major = stoi(version.substr(0, dot));
  int minor = stoi(version.substr(dot + 1));
  return major * 10 + minor;
}

int main() {
  string query_string = getenv("QUERY_STRING");
  vector<string> parameters_vec;
  boost::algorithm::split(parameters_vec, query_string, boost::is_any_of("&"));

  cout << "Content-type: text/html" << endl << endl;

  auto console = Console::getInstance();

  unordered_map<string, string> query_table;
  for (auto p : parameters_vec) {
    auto assign = p.find('=');
    string k = p.substr(0, assign);
    string v = p.substr(assign + 1);
    if (!v.empty() && k.size() == 2) {
      int index = k[1] - '0';
      if (k[0] == 'h')
        console.set_host(index, v);
      else if (k[0] == 'p')
        console.set_port(index, v);
      else if (k[0] == 'f')
        console.set_file_name(index, v);
    }
  }

  string server_protocol = getenv("SERVER_PROTOCOL");
  auto version = get_version_from_server_protocol(server_protocol);

  // The io_context is required for all I/O
  net::io_context ioc;

  // Generate Html
  console.init();
  // Launch the asynchronous operation
  for (size_t i = 0; i < 5; i++) {
    if (console.server_is_exist(i)) {
      string host = console.get_host(i);
      string port = console.get_port(i);
      string file_name = console.get_file_name(i);

      std::make_shared<session>(ioc)->run(host.c_str(), port.c_str(), file_name,
                                          version, "s" + to_string(i));
    }
  }
  // Run the I/O service. The call will return when
  // the get operation is complete.
  ioc.run();

  return EXIT_SUCCESS;
}