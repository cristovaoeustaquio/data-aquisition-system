#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <fstream>

using boost::asio::ip::tcp;

#pragma pack(push, 1)
struct LogRecord {
    char sensor_id[32]; // supondo um ID de sensor de até 32 caracteres
    std::time_t timestamp; // timestamp UNIX
    double value; // valor da leitura
};
#pragma pack(pop)

std::time_t string_to_time_t(const std::string& time_string) {
    std::tm tm = {};
    std::istringstream ss(time_string);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::mktime(&tm);
}

std::string time_t_to_string(std::time_t time) {
    std::tm* tm = std::localtime(&time);
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

class session
  : public std::enable_shared_from_this<session>
{
public:
  session(tcp::socket socket)
    : socket_(std::move(socket))
  {
  }

  void start()
  {
    read_message();
  }

private:
  bool starts_with(const std::string &str, const std::string &prefix)
  {
    if (prefix.length() > str.length())
    {
        return false; // Prefix is longer than the string, so it can't be a prefix.
    }
    // Compare the first 'prefix.length()' characters of 'str' with 'prefix'.
    return str.compare(0, prefix.length(), prefix) == 0;
  }
  void read_message()
  {
    auto self(shared_from_this());
    boost::asio::async_read_until(socket_, buffer_, "\r\n",
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            std::istream is(&buffer_);
            std::string message(std::istreambuf_iterator<char>(is), {});
            std::string message_srv(std::istreambuf_iterator<char>(is), {});
            if (starts_with(message, "LOG"))
            {
                std::vector<std::string> parts;
                // Split the string using the comma delimiter
                boost::split(parts, message, boost::is_any_of("|"));
                std::fstream file(parts[1]+".dat", std::fstream::out | std::fstream::in | std::fstream::binary | std::fstream::app);
                if(file.is_open())
                {
                    LogRecord log;
                    std::strcpy(log.sensor_id, parts[1].c_str());
                    log.timestamp = string_to_time_t(parts[2]);
                    log.value = std::stod(parts[3]);
                    file.write((char*)&log, sizeof(LogRecord));
                    file.close();
                }
                for (const std::string &part : parts)
                {
                    std::cout << part << std::endl;
                }
                read_message(); //para nao fechar a conexao
            }
            else if (starts_with(message, "GET"))
            {
                std::vector<std::string> parts;
                // Split the string using the pipe delimiter
                boost::split(parts, message, boost::is_any_of("|"));
                std::fstream file(parts[1]+".dat", std::fstream::in | std::fstream::binary);
                if (file.is_open())
                {
                  std::cout << "Entrou no file " << std::endl;
                  file.seekg(0, std::ios_base::end);
                  int file_size = file.tellg();
                  int num_reg_total = file_size / sizeof(LogRecord);
                  int num_reg_request = std::stoi(parts[2]);
                  LogRecord log;
                  std::cout << "file: " << num_reg_total << std::endl;
                  if (num_reg_request > num_reg_total)
                  {
                    num_reg_request = num_reg_total;
                  }
                  int start_pos = file_size - (num_reg_request * sizeof(LogRecord));
                  file.seekg(start_pos);
                  message_srv = std::to_string(num_reg_request);
                  for (int i = 0; i < num_reg_request; i++)
                  {
                    file.read((char *)&log, sizeof(LogRecord));
                    message_srv += ";" + time_t_to_string(log.timestamp) + "|" + std::to_string(log.value);
                  }
                  message_srv += "\r\n";
                  file.close();

                  std::cout << "RESULT: " + message_srv << std::endl;
                  write_message(message_srv);
                }
                else
                {
                  message_srv = "ERROR|INVALID_SENSOR_ID\r\n";
                  write_message(message_srv);
                }
            }
            std::cout << "Received: " << message << std::endl;
          }
        });
  }

  void write_message(const std::string& message)
  {
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(message),
        [this, self, message](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            read_message();
          }
        });
  }

  tcp::socket socket_;
  boost::asio::streambuf buffer_;
};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    accept();
  }

private:
  void accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            std::make_shared<session>(std::move(socket))->start();
          }

          accept();
        });
  }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: chat_server <port>\n";
    return 1;
  }

  boost::asio::io_context io_context;

  server s(io_context, std::atoi(argv[1]));

  io_context.run();

  return 0;
}
