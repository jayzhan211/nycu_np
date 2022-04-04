#include <arpa/inet.h>
#include <fcntl.h>  //open()
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>   //fopen
#include <stdlib.h>  //clearenv()
#include <sys/socket.h>
#include <sys/stat.h>   //open()
#include <sys/types.h>  //open()
#include <sys/wait.h>   //wait
#include <unistd.h>     //close(pipefd)
#include <unistd.h>     //execv

#include <cstring>  //strcpy
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>  //istringstream
#include <string>
#include <vector>

using namespace std;

const int kMaxClient = 30;
const int kMaxMessageSize = 15000;

enum Sign { Pipe, NumberPipe, ErrorPipe, Write, WriteUserPipe, None };

struct PipeInfo {
  int pipeNum;
  int senderId;
  int recverId;
  int clientId;
  int *pipefd;
  Sign sign;
};

struct Fd {
  int in;
  int out;
  int error;
};

class ClientInfo {
 public:
  explicit ClientInfo(int id = -1, string name = "(no name)",
                      string ip = "None", uint16_t port = 0, int sockfd = -1)
      : id(id), name(name), ip(ip), port(port), sockfd(sockfd) {
    env_table.emplace("PATH", "bin:.");
  }

  int get_id() { return id; }
  string get_name() { return name; }
  void set_name(string new_name) { name = new_name; }
  string get_ip() { return ip; }
  uint16_t get_port() { return port; }
  string get_ip_and_port() { return ip + ":" + to_string(port); }
  int get_sockfd() { return sockfd; }

  string get_env(string key) {
    if (env_table.find(key) != env_table.end()) return env_table[key];
    return "";
  }

  void set_env(string key, string value) {
    env_table.insert_or_assign(key, value);
  }

  void init_env() {
    clearenv();
    for (auto kv : env_table) {
      setenv(kv.first.c_str(), kv.second.c_str(), 1);
    }
  }

 private:
  int id;
  string name;
  string ip;
  uint16_t port;
  int sockfd;
  map<string, string> env_table;
};

bool ids[kMaxClient];
vector<ClientInfo> clientV;
vector<struct PipeInfo> pipeV;
int mSock;
int nfds;
fd_set afds;

int RunShell(string cmdLine, vector<ClientInfo>::iterator &client);
void PrintUserLogin();
vector<ClientInfo>::iterator IdentifyClientById(int id);
vector<ClientInfo>::iterator IdentifyClientByFd(int fd);
void BroadCast(vector<ClientInfo>::iterator iter, string msg, string action,
               int id);
int SetId();

vector<ClientInfo>::iterator IdentifyClientById(int id) {
  auto iter = clientV.begin();
  while (iter != clientV.end()) {
    if (iter->get_id() == id) return iter;
    iter++;
  }
  return clientV.end();
}

vector<ClientInfo>::iterator IdentifyClientByFd(int fd) {
  auto iter = clientV.begin();
  while (iter != clientV.end()) {
    if (iter->get_sockfd() == fd) return iter;
    iter++;
  }
  return clientV.end();
}

void RemoveEndline(string &str) {
  while (!str.empty() && (str.back() == '\n' || str.back() == '\r'))
    str.pop_back();
}

void BroadCast(vector<ClientInfo>::iterator iter, string msg, string action,
               int id) {
  string news;
  vector<ClientInfo>::iterator partner;
  if (id != 0) partner = IdentifyClientById(id);

  if (action == "login")
    news = "*** User '" + iter->get_name() + "' entered from " +
           iter->get_ip_and_port() + ". ***\n";
  else if (action == "logout")
    news = "*** User '" + iter->get_name() + "' left. ***\n";
  else if (action == "yell")
    news = "*** " + iter->get_name() + " yelled ***:" + msg + "\n";
  else if (action == "name")
    news = "*** User from " + iter->get_ip_and_port() + " is named '" + msg +
           "'. ***\n";
  else if (action == "writeuser") {
    RemoveEndline(msg);
    news = "*** " + iter->get_name() + " (#" + to_string(iter->get_id()) +
           ") just piped '" + msg + "' to " + partner->get_name() + " (#" +
           to_string(partner->get_id()) + ") ***\n";
  } else if (action == "readuser") {
    /*  erase null at the last character    */
    RemoveEndline(msg);
    news = "*** " + iter->get_name() + " (#" + to_string(iter->get_id()) +
           ") just received from " + partner->get_name() + " (#" +
           to_string(partner->get_id()) + ") by '" + msg + "' ***\n";
  }

  for (int fd = 0; fd < nfds; fd++) {
    if (fd != mSock && FD_ISSET(fd, &afds)) {
      if (write(fd, news.c_str(), news.size()) < 0)
        cerr << "Fail to send " + news + ", errno: " << errno << endl;
    }
  }
}

int SetId() {
  for (int i = 0; i < kMaxClient; i++) {
    if (!ids[i]) {
      ids[i] = true;
      return i + 1;
    }
  }
  return 0;
}

void SetStdInOut(Sign sign, Fd &fd, int pipeNum, int writeId, int readId,
                 vector<ClientInfo>::iterator client, bool UserPipeInError,
                 bool UserPipeOutError) {
  bool setIn = false;
  bool setOut = false;
  if (UserPipeInError) {
    fd.in = -1;
    setIn = true;
  }
  if (UserPipeOutError) {
    fd.out = -1;
    setOut = true;
  }

  if (sign == Write || sign == None) setOut = true;

  auto iter = pipeV.begin();
  while (iter != pipeV.end()) {
    if (!setIn) {
      if (iter->senderId == readId && iter->recverId == client->get_id() &&
          iter->sign == WriteUserPipe) {
        close(iter->pipefd[1]);
        fd.in = iter->pipefd[0];
        setIn = true;
      } else if (iter->pipeNum == 0 && iter->clientId == client->get_id() &&
                 readId == 0) {
        close(iter->pipefd[1]);
        fd.in = iter->pipefd[0];
        setIn = true;
      }
    }
    if (!setOut) {
      if (sign == WriteUserPipe) {
        if (iter->senderId == client->get_id() && iter->recverId == writeId) {
          fd.out = iter->pipefd[1];
          setOut = true;
        }
      } else {
        if (iter->pipeNum == pipeNum && iter->clientId == client->get_id()) {
          fd.out = iter->pipefd[1];
          if (sign == ErrorPipe) fd.error = iter->pipefd[1];
          setOut = true;
        }
      }
    }
    if (setIn && setOut) break;
    iter++;
  }
}

bool HasUserPipe(int senderId, int recverId) {
  for (auto iter : pipeV) {
    if (iter.senderId == senderId && iter.recverId == recverId &&
        iter.sign == WriteUserPipe)
      return true;
  }
  return false;
}

bool HasNumberedPipe(int pipeNum, Sign sign, int clientId) {
  for (auto iter : pipeV) {
    if (iter.pipeNum == pipeNum && iter.clientId == clientId &&
        iter.sign == sign)
      return true;
  }
  return false;
}
/*  reduce pipe's number */
void ReducePipe(int clientId) {
  for (auto &iter : pipeV) {
    if (iter.sign == Pipe && iter.clientId == clientId) iter.pipeNum--;
  }
}

void ReducePipeNum(int clientId) {
  for (auto &iter : pipeV) {
    if ((iter.sign == NumberPipe || iter.sign == ErrorPipe) &&
        iter.clientId == clientId)
      iter.pipeNum--;
  }
}

void ClosePipe(int clientId, int readId) {
  auto iter = pipeV.begin();
  while (iter != pipeV.end()) {
    bool toErase = false;
    // ordinary pipe or number pipe
    if (iter->pipeNum == 0 && iter->clientId == clientId) toErase = true;
    // user pipe (read)
    if (iter->senderId == readId && iter->recverId == clientId &&
        iter->sign == WriteUserPipe)
      toErase = true;

    if (toErase) {
      close(iter->pipefd[0]);
      close(iter->pipefd[1]);
      delete[] iter->pipefd;
      iter = pipeV.erase(iter);
    } else {
      iter++;
    }
  }
}

void CreatePipe(Sign sign, int pipeNum, int clientId, int senderId,
                int recverId) {
  int *pipefd = new int[2];
  struct PipeInfo newPipe;

  if (pipe(pipefd) < 0) {
    cerr << "Pipe create fail"
         << " eerrno:" << errno << endl;
    exit(1);
  }
  newPipe.pipefd = pipefd;
  newPipe.sign = sign;
  newPipe.pipeNum = pipeNum;
  newPipe.clientId = clientId;
  newPipe.senderId = senderId;
  newPipe.recverId = recverId;
  pipeV.push_back(newPipe);
}

vector<char *> SetArgv(string cmd, vector<string> argv) {
  vector<char *> execvp_argv;
  execvp_argv.push_back(const_cast<char *>(cmd.c_str()));
  for (size_t i = 0; i < argv.size(); i++) {
    execvp_argv.push_back(const_cast<char *>(argv[i].c_str()));
  }
  execvp_argv.push_back(nullptr);
  return execvp_argv;
}

vector<string> SplitEnvPath(string path, char delim) {
  vector<string> pathV;
  string temp;
  stringstream ss(path);

  while (getline(ss, temp, delim)) {
    pathV.push_back(temp);
  }

  return pathV;
}

bool LegalCmd(string cmd, vector<string> pathV) {
  string path;
  vector<string>::iterator iter;
  iter = pathV.begin();

  FILE *file;
  while (iter != pathV.end()) {
    path = *iter + "/" + cmd;
    file = fopen(path.c_str(), "r");
    if (file != NULL) {
      fclose(file);
      return true;
    }
    iter++;
  }

  return false;
}

void DoCmd(string cmd, vector<string> argV, vector<string> pathV, Fd fd,
           int sockfd) {
  int devNullIn, devNullOut;
  dup2(sockfd, 1);
  dup2(sockfd, 2);
  close(sockfd);
  if (fd.in != 0) {
    /*  user pipe in error  */
    if (fd.in == -1) {
      if ((devNullIn = open("/dev/null", O_RDONLY)) < 0)
        cout << "Fail to redirect /dev/null, errno: " << errno << endl;
      if ((dup2(devNullIn, 0)) < 0)
        cout << "Fail to dup2 /dev/null, errno: " << errno << endl;
    } else
      dup2(fd.in, 0);
  }
  if (fd.out != sockfd) {
    /*  user pipe out error */
    if (fd.out == -1) {
      if ((devNullOut = open("/dev/null", O_WRONLY)) < 0)
        cout << "Fail to redirect dev/null, errno: " << errno << endl;
      if ((dup2(devNullOut, 1)) < 0)
        cout << "Fail to dup2 /dev/null, errno: " << errno << endl;
    } else
      dup2(fd.out, 1);
  }
  if (fd.error != sockfd) dup2(fd.error, 2);
  if (fd.in != 0) {
    if (fd.in == -1)
      close(devNullIn);
    else
      close(fd.in);
  }
  if (fd.out != sockfd) {
    if (fd.in == -1)
      close(devNullOut);
    else
      close(fd.out);
  }
  if (fd.error != sockfd) close(fd.error);

  auto arg = SetArgv(cmd, argV);
  vector<string>::iterator iter = pathV.begin();
  while (iter != pathV.end()) {
    string path = (*iter) + "/" + cmd;
    if ((execv(path.c_str(), &arg[0])) == -1) iter++;
  }

  cerr << "Fail to exec" << endl;
  exit(1);
}

int DoBuildinCmd(string cmd, vector<string> argV, string &path,
                 vector<string> &pathV, vector<ClientInfo>::iterator &client) {
  if (cmd == "printenv") {
    string env = argV[0];
    string msg = getenv(env.c_str());
    msg += "\n";
    write(client->get_sockfd(), msg.c_str(), msg.size());
    return 0;
  } else if (cmd == "setenv") {
    string env, assign;
    env = argV[0];
    assign = argV[1];
    client->set_env(env, assign);
    setenv(env.c_str(), assign.c_str(), 1);
    if (env == "PATH") {
      path = getenv("PATH");
      pathV.clear();
      pathV = SplitEnvPath(path, ':');
    }
    return 0;
  } else if (cmd == "who") {
    string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
    write(client->get_sockfd(), msg.c_str(), msg.size());
    for (int i = 0; i < kMaxClient; i++) {
      if (ids[i]) {
        int id = i + 1;
        vector<ClientInfo>::iterator iter = IdentifyClientById(id);
        msg = to_string(iter->get_id()) + "\t" + iter->get_name() + "\t" +
              iter->get_ip_and_port();

        if (id == client->get_id())
          msg += "\t<-me\n";
        else
          msg += "\n";
        write(client->get_sockfd(), msg.c_str(), msg.size());
      }
    }
    return 0;
  } else if (cmd == "tell") {
    int sendId = atoi(argV[0].c_str());
    string msg = "*** " + client->get_name() + " told you ***:";
    for (size_t i = 1; i < argV.size(); i++) msg += (" " + argV[i]);
    msg += "\n";

    vector<ClientInfo>::iterator receiver = IdentifyClientById(sendId);
    if (receiver != clientV.end()) {
      if (write(receiver->get_sockfd(), msg.c_str(), msg.size()) < 0)
        cerr << "Fail to send msg: " + msg + ", errno: " << errno << endl;
    } else {
      msg = "*** Error: user #" + to_string(sendId) +
            " does not exist yet. ***\n";
      write(client->get_sockfd(), msg.c_str(), msg.size());
    }

    return 0;
  } else if (cmd == "yell") {
    string msg;
    for (size_t i = 0; i < argV.size(); i++) msg += (" " + argV[i]);

    BroadCast(client, msg, "yell", 0);

    return 0;
  } else if (cmd == "name") {
    string name = argV[0];

    for (int i = 0; i < kMaxClient; i++) {
      if (ids[i]) {
        int id = i + 1;
        vector<ClientInfo>::iterator iter = IdentifyClientById(id);
        if (iter->get_name() == name && id != client->get_id()) {
          string msg = "*** User '" + name + "' already exists. ***\n";
          write(client->get_sockfd(), msg.c_str(), msg.size());
          return 0;
        }
      }
    }

    BroadCast(client, name, "name", 0);
    client->set_name(name);
    return 0;
  } else  // exit or EOF
    return -1;
}

bool BuildCmd(string cmd) {
  if (cmd == "setenv" || cmd == "printenv" || cmd == "exit" || cmd == "EOF" ||
      cmd == "who" || cmd == "tell" || cmd == "yell" || cmd == "name")
    return true;
  return false;
}

void IdentifyCmd(vector<string> &splitCmdLine,
                 vector<string>::iterator &iterLine, string &cmd,
                 vector<string> &argV, Sign &sign, int &pipeNum, int &writeId,
                 int &readId) {
  string temp;
  bool isCmd = true;

  /*  who, tell yell and name are user cmd, and set all remaining iter as
   * argument    */
  bool isBuildinCmd = BuildCmd(splitCmdLine[0]);

  while (iterLine != splitCmdLine.end()) {
    temp = *iterLine;

    if (temp[0] == '|' && !isBuildinCmd) {
      if (temp.size() == 1) {
        sign = Pipe;
        pipeNum = 1;
      } else {
        sign = NumberPipe;
        string num;
        for (size_t i = 1; i < temp.size(); i++) num += temp[i];
        pipeNum = stoi(num);
      }

      iterLine++;
      break;
    } else if (temp[0] == '!' && !isBuildinCmd) {
      sign = ErrorPipe;
      string num;

      for (size_t i = 1; i < temp.size(); i++) num += temp[i];

      pipeNum = stoi(num);

      iterLine++;
      break;
    } else if (temp[0] == '>' && temp.size() == 1 && !isBuildinCmd) {
      sign = Write;
      iterLine++;
      argV.push_back(*iterLine);

      iterLine++;
      break;
    } else if (temp[0] == '>' && !isBuildinCmd) {
      sign = WriteUserPipe;
      string id;

      for (size_t i = 1; i < temp.size(); i++) id += temp[i];

      writeId = stoi(id);

      /*  check cat >2 <1 case    */
      if ((iterLine + 1) != splitCmdLine.end()) {
        temp = *(iterLine + 1);
        if (temp[0] == '<') {
          iterLine++;
          continue;
        }
      }
      iterLine++;
      break;
    } else if (temp[0] == '<' && !isBuildinCmd) {
      string id;
      for (size_t i = 1; i < temp.size(); i++) id += temp[i];

      readId = stoi(id);
    } else if (isCmd) {
      cmd = temp;
      isCmd = false;
    } else {
      argV.push_back(temp);
    }

    iterLine++;
  }
}
vector<string> SplitCmdWithSpace(string cmdLine) {
  istringstream ss(cmdLine);
  vector<string> splitCmdLine;
  string temp;

  while (ss >> temp) {
    splitCmdLine.push_back(temp);
  }

  return splitCmdLine;
}

int RunShell(string cmdLine, vector<ClientInfo>::iterator &client) {
  vector<string> splitCmdLine;
  vector<string>::iterator iterLine;
  vector<pid_t> pidV;

  bool isNumPipe = false;
  string path = getenv("PATH");
  vector<string> pathV = SplitEnvPath(path, ':');

  splitCmdLine = SplitCmdWithSpace(cmdLine);
  iterLine = splitCmdLine.begin();

  while (iterLine != splitCmdLine.end() && *iterLine != "\0") {
    Fd fd = {0, client->get_sockfd(), client->get_sockfd()};

    vector<string> argV;
    string cmd;
    Sign sign = None;
    int pipeNum = 0;
    int writeId = 0;
    int readId = 0;
    bool UserPipeInError = false;
    bool UserPipeOutError = false;

    IdentifyCmd(splitCmdLine, iterLine, cmd, argV, sign, pipeNum, writeId,
                readId);

    /*  Do buildin command  */
    if (BuildCmd(cmd)) {
      int status;
      status = DoBuildinCmd(cmd, argV, path, pathV, client);
      ClosePipe(client->get_id(), readId);
      ReducePipe(client->get_id());

      if (status == -1)
        return status;
      else
        break;
    }

    /*  Test legal command  */
    if (!LegalCmd(cmd, pathV)) {
      string msg = "Unknown command: [" + cmd + "].\n";
      write(client->get_sockfd(), msg.c_str(), msg.size());
      continue;
    }

    /*  handle cmd <n   */
    if (readId != 0) {
      if (!ids[readId - 1]) {
        string msg = "*** Error: user #" + to_string(readId) +
                     " does not exist yet. ***\n";
        write(client->get_sockfd(), msg.c_str(), msg.size());
        UserPipeInError = true;
      } else if (!HasUserPipe(readId, client->get_id())) {
        string msg = "*** Error: the pipe #" + to_string(readId) + "->#" +
                     to_string(client->get_id()) + " does not exist yet. ***\n";
        write(client->get_sockfd(), msg.c_str(), msg.size());
        UserPipeInError = true;
      } else
        BroadCast(client, cmdLine, "readuser", readId);
    }

    if (sign == Pipe) {
      CreatePipe(sign, pipeNum, client->get_id(), 0, 0);
    } else if (sign == NumberPipe || sign == ErrorPipe) {
      if (!HasNumberedPipe(pipeNum, sign, client->get_id()))
        CreatePipe(sign, pipeNum, client->get_id(), 0, 0);
      isNumPipe = true;
    } else if (sign == Write) {
      string fileName = argV.back();
      argV.pop_back();
      FILE *file = fopen(fileName.c_str(), "w");

      if (file == NULL) {
        cerr << "Fail to open file" << endl;
        return -1;
      }

      fd.out = fileno(file);
    } else if (sign == WriteUserPipe) {
      isNumPipe = true;
      if (!ids[writeId - 1]) {
        string msg = "*** Error: user #" + to_string(writeId) +
                     " does not exist yet. ***\n";
        write(client->get_sockfd(), msg.c_str(), msg.size());
        UserPipeOutError = true;
      } else if (!HasUserPipe(client->get_id(), writeId)) {
        BroadCast(client, cmdLine, "writeuser", writeId);
        CreatePipe(sign, -1, client->get_id(), client->get_id(), writeId);
      } else {
        string msg = "*** Error: the pipe #" + to_string(client->get_id()) +
                     "->#" + to_string(writeId) + " already exists. ***\n";
        write(client->get_sockfd(), msg.c_str(), msg.size());
        UserPipeOutError = true;
      }
    }

    SetStdInOut(sign, fd, pipeNum, writeId, readId, client, UserPipeInError,
                UserPipeOutError);

    pid_t pid;
    while ((pid = fork()) < 0) {
      /*  parent process no resource to fork child process, so wait for child
       * exit and release recourse   */
      int status = 0;
      waitpid(-1, &status, 0);
    }
    if (pid == 0)
      DoCmd(cmd, argV, pathV, fd, client->get_sockfd());
    else if (pid > 0)
      pidV.push_back(pid);

    if (fd.in !=
        0)  // close read from pipe, the other entrance is closed in SetStdInOut
      close(fd.in);

    ClosePipe(client->get_id(), readId);
    ReducePipe(client->get_id());
  }

  if (!isNumPipe) {
    vector<pid_t>::iterator iter = pidV.begin();
    while (iter != pidV.end()) {
      int status;
      waitpid((*iter), &status, 0);
      iter++;
    }
  }

  ReducePipeNum(client->get_id());

  string title = "% ";
  if (write(client->get_sockfd(), title.c_str(), title.size()) < 0)
    cerr << "Fail to send %, errno: " << errno << endl;

  return 0;
}

void RemoveClient(vector<ClientInfo>::iterator client) {
  /*  close all pipe related to client    */
  vector<struct PipeInfo>::iterator iter = pipeV.begin();
  while (iter != pipeV.end()) {
    if (iter->clientId == client->get_id() ||
        iter->recverId == client->get_id()) {
      close(iter->pipefd[0]);
      close(iter->pipefd[1]);
      delete[] iter->pipefd;
      pipeV.erase(iter);
      continue;
    }
    iter++;
  }

  ids[client->get_id() - 1] = false;
  clientV.erase(client);
}

void WelcomeUser(int sockfd) {
  string msg = "****************************************\n";
  write(sockfd, msg.c_str(), msg.size());
  msg = "** Welcome to the information server. **\n";
  write(sockfd, msg.c_str(), msg.size());
  msg = "****************************************\n";
  write(sockfd, msg.c_str(), msg.size());
}

int PassiveTcp(int port) {
  struct sockaddr_in servAddr;
  int mSock, type;

  bzero((char *)&servAddr, sizeof(servAddr));
  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = INADDR_ANY;
  servAddr.sin_port = htons(port);

  type = SOCK_STREAM;

  if ((mSock = socket(AF_INET, type, 0)) < 0) {
    cerr << "Fail to create master socket, errno: " << errno << endl;
    exit(1);
  }

  int optval = 1;
  if (setsockopt(mSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1) {
    cout << "Error: Set socket failed, errno: " << errno << endl;
    exit(1);
  }

  if (bind(mSock, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
    cerr << "Fail to bind master socket, errno: " << errno << endl;
    exit(1);
  }

  if (listen(mSock, 0) < 0) {
    cerr << "Fail to listen master socket, errno: " << errno << endl;
    exit(1);
  }

  return mSock;
}

void PrintCmd(vector<ClientInfo>::iterator client, string cmd) {
  cout << "------Cmd------" << endl;
  cout << "Id: " << client->get_id() << " Cmd: " << cmd << endl;
  cout << "---------------" << endl;
}
void PrintServer(int mSock) {
  cout << "------Server------" << endl;
  cout << "fd: " << mSock << endl;
  cout << "------------------" << endl;
}
void PrintUserLogout(vector<ClientInfo>::iterator client) {
  cout << "------User logout------" << endl;
  cout << "Id: " << client->get_id() << " fd: " << client->get_sockfd()
       << " ip:port" << client->get_ip_and_port() << endl;
  cout << "----------------------" << endl;
}
void PrintUserLogin() {
  cout << "------User login------" << endl;
  vector<ClientInfo>::iterator client = clientV.end() - 1;
  cout << "Id: " << client->get_id() << " fd: " << client->get_sockfd()
       << " ip:port " << client->get_ip() + ":" + to_string(client->get_port())
       << endl;
  cout << "----------------------" << endl;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s [port]\n", argv[0]);
    return 0;
  }

  struct sockaddr_in client_addr;

  int port = atoi(argv[1]);
  fd_set rfds;

  memset((void *)ids, (int)false, kMaxClient * sizeof(bool));

  /*  socket() bind() listen()    */
  mSock = PassiveTcp(port);

  /*  kill zombie */
  signal(SIGCHLD, SIG_IGN);

  nfds = getdtablesize();
  FD_ZERO(&afds);
  FD_SET(mSock, &afds);  // afds[mSock] = true

  while (true) {
    memcpy(&rfds, &afds, sizeof(rfds));

    /*  if there is a client enter, then keep going, otherwise just stop here */
    // cout << "Wait for client entering..." << endl;
    if (select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) <
        0) {
      cerr << "Fail to select, errno: " << errno << endl;
      if (errno == EINTR) continue;
      exit(EXIT_FAILURE);
    }

    if (FD_ISSET(mSock, &rfds)) {
      int sSock;
      socklen_t client_len = sizeof(client_addr);
      if ((sSock = accept(mSock, (struct sockaddr *)&client_addr,
                          &client_len)) < 0) {
        cerr << "Fail to accept, errno: " << errno << endl;
        exit(EXIT_FAILURE);
      }

      ClientInfo client =
          ClientInfo(SetId(), "(no name)", inet_ntoa(client_addr.sin_addr),
                     ntohs(client_addr.sin_port), sSock);

      clientV.push_back(client);

      FD_SET(sSock, &afds);

      WelcomeUser(sSock);
      BroadCast(prev(clientV.end()), "", "login", 0);

      string command_prompt = "% ";
      write(sSock, command_prompt.c_str(), command_prompt.size());
    }

    for (int fd = 0; fd < nfds; fd++) {
      if (fd != mSock && FD_ISSET(fd, &rfds)) {
        int n;
        char cmdLine[kMaxMessageSize];
        memset(&cmdLine, '\0', sizeof(cmdLine));

        vector<ClientInfo>::iterator client = IdentifyClientByFd(fd);
        if ((n = read(fd, cmdLine, kMaxMessageSize)) < 0) {
          if (errno == EINTR) continue;
          cerr << "Fail to recv, errno: " << errno << endl;
          BroadCast(client, "", "logout", 0);
          RemoveClient(client);
          close(fd);
          FD_CLR(fd, &afds);
        } else if (n == 0) {
          /*  logout  */
          BroadCast(client, "", "logout", 0);
          RemoveClient(client);
          close(fd);
          FD_CLR(fd, &afds);
        } else {
          string str(cmdLine);
          client->init_env();
          int status;
          if ((status = RunShell(cmdLine, client) < 0)) {
            BroadCast(client, "", "logout", 0);
            RemoveClient(client);
            close(fd);
            FD_CLR(fd, &afds);
          }
        }
      }
    }
  }

  return 0;
}