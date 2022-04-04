#include <fcntl.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <ios>
#include <iostream>
#include <map>
#include <sstream>
#include <unordered_set>
#include <vector>

using namespace std;

struct Node {
  int input_stream = STDIN_FILENO;
  int output_stream = STDOUT_FILENO;
  int error_stream = STDERR_FILENO;
  int pipe_write_end = -1;
};

deque<Node> redirection_info(4096);
int pipes_fd[64][2];
vector<pid_t> pid_table;

void child_handler(int signo) {
  int status;
  while (waitpid(-1, &status, WNOHANG) > 0) {
  };
}

void convert_args(const vector<string> &args, vector<char *> &execvp_args) {
  execvp_args.clear();
  for (size_t i = 0; i < args.size(); i++) {
    execvp_args.push_back(const_cast<char *>(args[i].c_str()));
  }
  execvp_args.push_back(nullptr);
}

void preprocess_commands(vector<vector<string>> &piped_commands,
                         const string &commands) {
  // tokenize arguments
  stringstream ss(commands);
  vector<string> args;
  string arg;

  while (ss >> arg) {
    if (arg == "|") {
      if (!args.empty()) {
        piped_commands.push_back(args);
        args.clear();
      }
    } else if (arg[0] == '|' || arg[0] == '!') {
      // TODO: better error handling
      if (!args.empty()) {
        piped_commands.push_back(args);
        args.clear();
      }
      piped_commands.push_back({arg});
    } else {
      args.push_back(arg);
    }
  }
  if (!args.empty()) {
    piped_commands.push_back(args);
    args.clear();
  }
}

pid_t forkProcess(const vector<string> &args, size_t pipe_cnt, int input_stream,
                  int output_stream, int error_stream) {
  pid_t cpid;
  while ((cpid = fork()) < 0) {
    usleep(1000);
  }

  if (cpid == 0) {
    if (input_stream != STDIN_FILENO) {
      if (dup2(input_stream, STDIN_FILENO)) throw std::exception();
      if (input_stream == redirection_info[0].input_stream) {
        // Close write so read will not be blocked
        close(redirection_info[0].pipe_write_end);
      }
    }

    if (output_stream != STDOUT_FILENO) {
      if (dup2(output_stream, STDOUT_FILENO) == -1) throw std::exception();
    }

    if (output_stream != STDOUT_FILENO) {
      if (dup2(output_stream, STDOUT_FILENO) == -1) throw std::exception();
    }
    if (error_stream != STDERR_FILENO) {
      if (dup2(error_stream, STDERR_FILENO) == -1) throw std::exception();
    }
    for (size_t i = 0; i < pipe_cnt; ++i) {
      close(pipes_fd[i][0]);
      close(pipes_fd[i][1]);
    }

    vector<string> cleanup_args;

    // assume token after > is filepath, and ignore token after all, from i+2 to
    // end
    for (size_t i = 0; i < args.size(); i++) {
      if (args[i] == ">" && i + 1 < args.size()) {
        // 644, user rw, group r, others r
        int fd_output = open(args[i + 1].c_str(), O_RDWR | O_CREAT | O_TRUNC,
                             S_IRWXU | S_IRGRP | S_IROTH);

        if (dup2(fd_output, STDOUT_FILENO) == -1) {
          cout << "dup2 errors" << endl;
        };
        close(fd_output);
        break;
      }
      cleanup_args.push_back(args[i]);
    }

    // Convert vector<string> to array of char*
    vector<char *> execvp_args;
    convert_args(cleanup_args, execvp_args);
    execvp(args[0].c_str(), &execvp_args[0]);
    cerr << "Unknown command: [" << args[0] << "]." << endl;
    exit(EXIT_FAILURE);
  }
  return cpid;
}

void run(vector<vector<string>> &piped_commands) {
  int number_pipe = 0;
  bool pipe_stderr = false;
  if (piped_commands.back()[0][0] == '|' ||
      piped_commands.back()[0][0] == '!') {
    number_pipe = stoi(piped_commands.back()[0].substr(1));
    if (piped_commands.back()[0][0] == '!') pipe_stderr = true;
    piped_commands.pop_back();
  }

  if (number_pipe > 0) {
    if (redirection_info[number_pipe].input_stream == STDIN_FILENO) {
      int pipe_fd[2];
      pipe(pipe_fd);
      redirection_info[0].output_stream = pipe_fd[1];
      if (pipe_stderr) redirection_info[0].error_stream = pipe_fd[1];

      redirection_info[number_pipe].input_stream = pipe_fd[0];
      redirection_info[number_pipe].pipe_write_end = pipe_fd[1];
    } else {
      redirection_info[0].output_stream =
          redirection_info[number_pipe].pipe_write_end;
    }
  }

  // limit batch size to avoid too many pipes error
  size_t batch_size = 50;
  for (size_t j = 0; j < piped_commands.size();) {
    size_t k = j + batch_size <= piped_commands.size() ? j + batch_size
                                                       : piped_commands.size();

    if (k != piped_commands.size()) {
      redirection_info.insert(redirection_info.begin() + 1, Node());
      int pipe_fd[2];
      pipe(pipe_fd);
      redirection_info[0].output_stream = pipe_fd[1];
      redirection_info[1].input_stream = pipe_fd[0];
      redirection_info[1].pipe_write_end = pipe_fd[1];
    }

    signal(SIGCHLD, child_handler);

    size_t pipe_cnt = k - j - 1;
    for (size_t i = 0; i < pipe_cnt; i++) {
      if (pipe(pipes_fd[i]) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
      }
    }
    int input_stream = -1;
    int output_stream = -1;
    int error_stream = -1;

    for (size_t i = j; i < k; i++) {
      input_stream = (i == j) ? redirection_info.front().input_stream
                              : pipes_fd[i - j - 1][0];
      output_stream = (i + 1 == k) ? redirection_info.front().output_stream
                                   : pipes_fd[i - j][1];
      if (pipe_stderr)
        error_stream = (i + 1 == k) ? redirection_info.front().error_stream
                                    : pipes_fd[i - j][1];
      else
        error_stream = STDERR_FILENO;

      pid_t cpid = forkProcess(piped_commands[i], pipe_cnt, input_stream,
                               output_stream, error_stream);

      pid_table.push_back(cpid);

      if (input_stream == redirection_info.front().input_stream &&
          input_stream != STDIN_FILENO) {
        close(redirection_info.front().pipe_write_end);
        close(input_stream);
      }
    }

    for (size_t i = 0; i < pipe_cnt; i++) {
      close(pipes_fd[i][0]);
      close(pipes_fd[i][1]);
    }

    // Number pipe, dont wait, we use signal to reap child after child process
    // is terminated
    if (output_stream != STDOUT_FILENO) {
    } else {
      for (auto item : pid_table) waitpid(item, nullptr, 0);
      pid_table.clear();
    }

    if (k != piped_commands.size()) {
      redirection_info.pop_front();
    }

    j = k;
  }

  // cleanup redirection_info[0]
  redirection_info.pop_front();
  Node node;
  redirection_info.push_back(node);
}

class NPShell {
 public:
  void main() {
    // init
    setenv("PATH", "bin:.", true);

    while (1) {
      cout << "% ";
      string line;
      getline(cin, line);
      vector<vector<string>> piped_commands;
      preprocess_commands(piped_commands, line);

      if (piped_commands.empty()) continue;

      if (piped_commands[0][0] == "exit") return;
      if (piped_commands[0][0] == "printenv") {
        if (const char *env_p = getenv(piped_commands[0][1].c_str())) {
          cout << env_p << endl;
        }
        continue;
      }
      if (piped_commands[0][0] == "setenv") {
        setenv(piped_commands[0][1].c_str(), piped_commands[0][2].c_str(),
               true);
        continue;
      }
      run(piped_commands);
    }
  }
};

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s [port]\n", argv[0]);
    return 0;
  }
  int port = atoi(argv[1]);

  // Expected command: telnet [server] [port]

  int sockfd;
  int opt;
  struct sockaddr_in server_address {
  }, cli_addr{};

  // Creating socket file descriptor
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  // Forcefully attaching socket to the port 8080
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt)) < 0) {
    perror("setsockopt failed");
    exit(EXIT_FAILURE);
  }

  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(port);

  // Forcefully attaching socket to the port 8080
  if (bind(sockfd, (struct sockaddr *)&server_address, sizeof(server_address)) <
      0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  /*
  int listen(int sockfd, int backlog);
  */
  if (listen(sockfd, 3) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  while (true) {
    socklen_t cli_len = sizeof(cli_addr);
    int newsockfd =
        accept(sockfd, (struct sockaddr *)&cli_addr, (socklen_t *)&cli_len);
    if (newsockfd < 0) {
      perror("server: accept error");
    }
    pid_t childpid = fork();
    if (childpid < 0) {
      perror("server: fork error");
    } else if (childpid == 0) {
      close(sockfd);
      dup2(newsockfd, STDIN_FILENO);
      dup2(newsockfd, STDOUT_FILENO);
      dup2(newsockfd, STDERR_FILENO);
      NPShell npshell;
      npshell.main();
      exit(EXIT_SUCCESS);
    } else {
      wait(nullptr);
      close(newsockfd);
    }
  }
}