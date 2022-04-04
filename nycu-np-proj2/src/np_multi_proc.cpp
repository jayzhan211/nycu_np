#include <arpa/inet.h>
#include <bits/stdc++.h>
#include <fcntl.h>  //open()
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>  //fopen
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>   //open()
#include <sys/types.h>  //open()
#include <sys/wait.h>   //wait
#include <unistd.h>     //close(pipefd)
#include <unistd.h>     //execv

#include <cstring>  //strcpy
#include <iomanip>  //setfill()
#include <sstream>  //istringstream

#define SHMKEY ((key_t)345)

const int kMaxClient = 30;
const int kMaxMessageSize = 15000;

using namespace std;

// Add WriteUserPipeForRead for Read Side because pipeV is not shared
enum PipeSign {
  Pipe,
  NumberPipe,
  ErrorPipe,
  Write,
  WriteUserPipe,
  WriteUserPipeForRead,
  None
};

enum BroadcastSign { YELL, NAME, EXIT, SEND, RECV, ENTER, RECV_SEND };

struct PipeInfo {
  int pipeNum;
  int senderId;
  int recverId;
  int clientId;
  int *pipefd;
  PipeSign sign;
  int fifofd;
};

struct Fd {
  int in;
  int out;
  int error;
};

struct Env {
  string name;
  string value;
};

struct ClientInfo {
  int fd;
  int pid;
  int uid;
  char name[20];
  char msg[kMaxMessageSize];
  char ip[20];
  uint16_t port;
  bool sendInfo[kMaxClient];  // client -> [i]
  bool recvInfo[kMaxClient];  // [i] -> client
};

int shmID;
ClientInfo *shmStartAddr;
ClientInfo *shmCurrentAddr;
vector<struct PipeInfo> pipeV;
vector<vector<string>> pathNameTable;

void CreateFifo(int, int);
void BroadCast(string, BroadcastSign, int, string = "", int = -1);

void RemoveLineFeed(string &str) {
  while (!str.empty() && (str.back() == '\n' || str.back() == '\r'))
    str.pop_back();
}

bool HasClient(int uid) {
  for (int i = 0; i < kMaxClient; i++) {
    if (shmStartAddr[i].uid == uid) return true;
  }
  return false;
}

void ReadFifo(int senderId, int recverId, int fifofd, int &fd_in) {
  fd_in = fifofd;
  shmCurrentAddr->recvInfo[senderId - 1] = false;
  (shmStartAddr + senderId - 1)->sendInfo[recverId - 1] = false;
}

void PrintPipeV() {
  printf("PIPEV Start\n");
  for (auto p : pipeV) {
    printf("sign=%d, send=%d, recv=%d, clientid=%d, pipeNum=%d\n", p.sign,
           p.senderId, p.recverId, p.clientId, p.pipeNum);
    if (p.pipefd) {
      printf("in=%d out=%d\n", p.pipefd[0], p.pipefd[1]);
    }
  }
  printf("PIPEV End");
  cout << endl;
}

void SetStdInOut(PipeSign sign, Fd &fd, int pipeNum, int writeId, int readId,
                 bool UserPipeInError, bool UserPipeOutError) {
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
      if (iter->senderId == readId && iter->recverId == shmCurrentAddr->uid &&
          iter->sign == WriteUserPipeForRead) {
        if (iter->pipefd) {
          close(iter->pipefd[1]);
          fd.in = iter->pipefd[0];
        } else {
          ReadFifo(iter->senderId, iter->recverId, iter->fifofd, fd.in);
        }
        setIn = true;
      } else if (iter->pipeNum == 0 && iter->clientId == shmCurrentAddr->uid &&
                 readId == 0) {
        close(iter->pipefd[1]);
        fd.in = iter->pipefd[0];
        setIn = true;
      }
    }
    if (!setOut) {
      if (sign == WriteUserPipe) {
        if (iter->senderId == shmCurrentAddr->uid &&
            iter->recverId == writeId) {
          fd.out = iter->fifofd;
          setOut = true;
        }
      } else {
        if (iter->pipeNum == pipeNum && iter->clientId == shmCurrentAddr->uid) {
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

/*  check there already is a number pipe or not */
bool HasNumberedPipe(int pipeNum, PipeSign sign, int clientId) {
  for (auto &it : pipeV) {
    if (it.pipeNum == pipeNum && it.sign == sign && it.clientId == clientId)
      return true;
  }
  return false;
}

bool HasUserPipe(int senderId, int recverId, int clientId) {
  if (clientId == senderId) {
    return shmCurrentAddr->sendInfo[recverId - 1] &&
           shmStartAddr[recverId - 1].recvInfo[senderId - 1];
  } else {
    return shmCurrentAddr->recvInfo[senderId - 1] &&
           shmStartAddr[senderId - 1].sendInfo[recverId - 1];
  }
}

/*  reduce pipe's number */
void ReducePipe() {
  for (auto &iter : pipeV) {
    if (iter.sign == Pipe && iter.clientId == shmCurrentAddr->uid)
      iter.pipeNum--;
  }
}

void ReducePipeNum() {
  for (auto &iter : pipeV) {
    if ((iter.sign == NumberPipe || iter.sign == ErrorPipe) &&
        iter.clientId == shmCurrentAddr->uid)
      iter.pipeNum--;
  }
}

void ClosePipe(int readId, int writeId) {
  vector<struct PipeInfo>::iterator iter = pipeV.begin();
  while (iter != pipeV.end()) {
    /*  | cmd or |0 cmd */
    if (iter->pipeNum == 0 && iter->clientId == shmCurrentAddr->uid) {
      close(iter->pipefd[0]);
      close(iter->pipefd[1]);
      delete[] iter->pipefd;
      iter = pipeV.erase(iter);

    } /* cmd <id */
    else if (iter->senderId == shmCurrentAddr->uid &&
             iter->recverId == writeId && iter->sign == WriteUserPipe) {
      if (iter->pipefd == nullptr) {
        // fifo type
        close(iter->fifofd);
        iter = pipeV.erase(iter);

      } else {
        close(iter->pipefd[0]);
        close(iter->pipefd[1]);
        delete[] iter->pipefd;
        iter = pipeV.erase(iter);
      }
    }
    /* cmd >id */
    else if (iter->senderId == readId &&
             iter->recverId == shmCurrentAddr->uid &&
             iter->sign == WriteUserPipeForRead) {
      if (iter->pipefd == nullptr) {
        // fifofd is closed in pipe loop
        iter = pipeV.erase(iter);
        // continue;
      } else {
        close(iter->pipefd[0]);
        close(iter->pipefd[1]);
        delete[] iter->pipefd;
        iter = pipeV.erase(iter);
      }
    } else {
      iter++;
    }
  }
}

void CreateFifo(int senderId, int recverId) {
  string pathName = pathNameTable[senderId][recverId];
  int fd = open(pathName.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
  close(fd);

  mkfifo(pathName.c_str(), 0644);

  fd = open(pathName.c_str(), O_WRONLY);
  if (fd < 0) {
    cerr << "Open File Fail: " << strerror(errno) << endl;
    exit(EXIT_FAILURE);
  }
  shmCurrentAddr->sendInfo[recverId - 1] = true;
  (shmStartAddr + recverId - 1)->recvInfo[senderId - 1] = true;

  struct PipeInfo newPipe;
  newPipe.fifofd = fd;
  newPipe.pipefd = nullptr;
  newPipe.sign = WriteUserPipe;
  newPipe.pipeNum = -1;
  newPipe.clientId = senderId;
  newPipe.senderId = senderId;
  newPipe.recverId = recverId;
  pipeV.push_back(newPipe);
}

void CreatePipe(PipeSign sign, int pipeNum, int clientId, int senderId,
                int recverId) {
  int *pipefd = new int[2];
  struct PipeInfo newPipe;

  if (pipe(pipefd) < 0) {
    cerr << "Pipe create fail"
         << " eerrno:" << errno << endl;
    exit(1);
  }

  newPipe.fifofd = -1;
  newPipe.pipefd = pipefd;
  newPipe.sign = sign;
  newPipe.pipeNum = pipeNum;
  newPipe.clientId = clientId;
  newPipe.senderId = senderId;
  newPipe.recverId = recverId;
  pipeV.push_back(newPipe);
}

/*  Set argv to exec    */
char **SetArgv(string cmd, vector<string> argV) {
  char **argv = new char *[argV.size() + 2];
  argv[0] = new char[cmd.size() + 1];
  strcpy(argv[0], cmd.c_str());
  for (size_t i = 0; i < argV.size(); i++) {
    argv[i + 1] = new char[argV[i].size() + 1];
    strcpy(argv[i + 1], argV[i].c_str());
  }
  argv[argV.size() + 1] = NULL;
  return argv;
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
           int sSock) {

  int devNullIn, devNullOut;
  dup2(sSock, 1);
  dup2(sSock, 2);
  close(sSock);
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
  if (fd.out != sSock) {
    /*  user pipe out error */
    if (fd.out == -1) {
      if ((devNullOut = open("/dev/null", O_WRONLY)) < 0)
        cout << "Fail to redirect dev/null, errno: " << errno << endl;
      if ((dup2(devNullOut, 1)) < 0)
        cout << "Fail to dup2 /dev/null, errno: " << errno << endl;
    } else
      dup2(fd.out, 1);
  }
  if (fd.error != sSock) dup2(fd.error, 2);
  if (fd.in != 0) {
    if (fd.in == -1)
      close(devNullIn);
    else
      close(fd.in);
  }
  if (fd.out != sSock) {
    if (fd.in == -1)
      close(devNullOut);
    else
      close(fd.out);
  }
  if (fd.error != sSock) close(fd.error);

  char **arg = SetArgv(cmd, argV);
  vector<string>::iterator iter = pathV.begin();
  while (iter != pathV.end()) {
    string path = (*iter) + "/" + cmd;
    if ((execv(path.c_str(), arg)) == -1) iter++;
  }

  cerr << "Fail to exec" << endl;
  exit(1);
}
int DoBuildinCmd(string cmd, vector<string> argV, string &path,
                 vector<string> &pathV) {
  if (cmd == "printenv") {
    string env = argV[0];
    string msg = getenv(env.c_str());
    msg += "\n";
    write(shmCurrentAddr->fd, msg.c_str(), msg.size());
    return 0;
  } else if (cmd == "setenv") {
    string env, assign;
    env = argV[0];
    assign = argV[1];
    setenv(env.c_str(), assign.c_str(), 1);
    if (env == "PATH") {
      path = getenv("PATH");
      pathV.clear();
      pathV = SplitEnvPath(path, ':');
    }
    return 0;
  } else if (cmd == "yell") {
    string msg;
    for (size_t i = 0; i < argV.size(); i++) msg += (" " + argV[i]);
    BroadCast(msg, YELL, -1);
    return 0;
  } else if (cmd == "name") {
    string name = argV[0];
    for (int i = 0; i < kMaxClient; i++) {
      if (shmStartAddr[i].name == name) {
        string msg = "*** User '" + name + "' already exists. ***\n";
        write(shmCurrentAddr->fd, msg.c_str(), msg.size());
        return 0;
      }
    }
    strcpy(shmCurrentAddr->name, name.c_str());
    BroadCast("", NAME, -1);
    return 0;
  } else if (cmd == "tell") {
    int sendId = atoi(argV[0].c_str());
    string msg = "*** " + string(shmCurrentAddr->name) + " told you ***:";
    for (size_t i = 1; i < argV.size(); i++) msg += (" " + argV[i]);
    if (HasClient(sendId)) {
      for (int i = 0; i < kMaxClient; i++) {
        if ((shmStartAddr + i)->uid == sendId) {
          strcpy((shmStartAddr + i)->msg, msg.c_str());
          kill((shmStartAddr + i)->pid, SIGUSR1);
          break;
        }
      }
    } else {
      string msg = "*** Error: user #" + to_string(sendId) +
                   " does not exist yet. ***\n";
      write(shmCurrentAddr->fd, msg.c_str(), msg.size());
    }
    return 0;
  } else if (cmd == "who") {
    string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
    write(shmCurrentAddr->fd, msg.c_str(), msg.size());
    for (int i = 0; i < kMaxClient; i++) {
      if (shmStartAddr[i].pid != -1) {
        msg = to_string(shmStartAddr[i].uid) + "\t" + shmStartAddr[i].name +
              "\t" + shmStartAddr[i].ip + ":" + to_string(shmStartAddr[i].port);
        if (shmStartAddr[i].uid == shmCurrentAddr->uid)
          msg += "\t<-me\n";
        else
          msg += "\n";
        write(shmCurrentAddr->fd, msg.c_str(), msg.size());
      }
    }
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
                 vector<string> &argV, PipeSign &sign, int &pipeNum,
                 int &writeId, int &readId) {
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

int Shell(int sSock) {
  string cmdLine;
  string cmd;
  string path = getenv("PATH");
  vector<string> splitCmdLine;
  vector<string>::iterator iterLine;
  vector<string> pathV = SplitEnvPath(path, ':');
  vector<pid_t> pidV;

  int pipeNum;
  int writeId;
  int readId;

  bool isNumPipe;
  bool UserPipeInError;
  bool UserPipeOutError;

  PipeSign sign;

  while (1) {
    string msg = "% ";
    write(shmCurrentAddr->fd, msg.c_str(), msg.size());
    getline(cin, cmdLine);

    splitCmdLine = SplitCmdWithSpace(cmdLine);
    iterLine = splitCmdLine.begin();

    isNumPipe = false;
    pidV.clear();

    while (iterLine != splitCmdLine.end() && *iterLine != "\0") {
      Fd fd = {sSock, sSock, sSock};
      vector<string> argV;
      sign = None;
      pipeNum = 0;
      writeId = 0;
      readId = 0;
      UserPipeInError = false;
      UserPipeOutError = false;

      IdentifyCmd(splitCmdLine, iterLine, cmd, argV, sign, pipeNum, writeId,
                  readId);

      /*  Do buildin command  */
      if (BuildCmd(cmd)) {
        int status = DoBuildinCmd(cmd, argV, path, pathV);
        ClosePipe(readId, writeId);
        ReducePipe();
        if (status < 0)
          return -1;
        else
          break;
      }

      /*  Test legal command  */
      if (!LegalCmd(cmd, pathV)) {
        string msg = "Unknown command: [" + cmd + "].\n";
        write(shmCurrentAddr->fd, msg.c_str(), msg.size());
        continue;
      }

      int readBufferId = -1;
      string readBufferCmdLine = "";

      /*  handle cmd <n   */
      if (readId != 0) {
        if (!HasClient(readId)) {
          string msg = "*** Error: user #" + to_string(readId) +
                       " does not exist yet. ***\n";
          write(shmCurrentAddr->fd, msg.c_str(), msg.size());
          UserPipeInError = true;
        } else {
          int senderId = readId, recverId = shmCurrentAddr->uid;
          if (!HasUserPipe(senderId, recverId, recverId)) {
            string msg = "*** Error: the pipe #" + to_string(senderId) + "->#" +
                         to_string(recverId) + " does not exist yet. ***\n";
            write(shmCurrentAddr->fd, msg.c_str(), msg.size());
            UserPipeInError = true;
          } else {
            readBufferCmdLine = cmdLine;
            readBufferId = readId;

            string pathName = pathNameTable[senderId][recverId];
            int fd = open(pathName.c_str(), O_RDONLY);
            if (fd < 0) {
              cerr << "Open File Fail: " << strerror(errno) << endl;
              return -1;
            }
            // create WriteUserPipeForRead for reciver
            struct PipeInfo newPipe;
            newPipe.fifofd = fd;
            newPipe.pipefd = nullptr;
            newPipe.sign = WriteUserPipeForRead;
            newPipe.pipeNum = -1;
            newPipe.clientId = recverId;
            newPipe.senderId = senderId;
            newPipe.recverId = recverId;
            pipeV.push_back(newPipe);
          }
        }
      }

      if (sign == Pipe) {
        CreatePipe(sign, pipeNum, shmCurrentAddr->uid, 0, 0);
      } else if (sign == NumberPipe || sign == ErrorPipe) {
        if (!HasNumberedPipe(pipeNum, sign, shmCurrentAddr->uid))
          CreatePipe(sign, pipeNum, shmCurrentAddr->uid, 0, 0);
        isNumPipe = true;
      } else if (sign == Write) {
        string fileName = argV.back();
        argV.pop_back();
        int fd_out = open(fileName.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) {
          cerr << "Open File Fail: " << strerror(errno) << endl;
          return -1;
        }
        fd.out = fd_out;
      } else if (sign == WriteUserPipe) {
        isNumPipe = true;
        int senderId = shmCurrentAddr->uid;
        int recverId = writeId;
        if (!HasClient(writeId)) {
          string msg = "*** Error: user #" + to_string(writeId) +
                       " does not exist yet. ***\n";
          write(shmCurrentAddr->fd, msg.c_str(), msg.size());
          UserPipeOutError = true;
        } else if (!HasUserPipe(senderId, recverId, senderId)) {
          if (readBufferId == -1)
            BroadCast(cmdLine, SEND, writeId);
          else {
            BroadCast(cmdLine, RECV_SEND, writeId, readBufferCmdLine,
                      readBufferId);
            readBufferId = -1;
          }
          CreateFifo(senderId, recverId);

        } else {
          string msg = "*** Error: the pipe #" + to_string(senderId) + "->#" +
                       to_string(recverId) + " already exists. ***\n";
          write(shmCurrentAddr->fd, msg.c_str(), msg.size());
          UserPipeOutError = true;
        }
      }

      if (readBufferId != -1) {
        BroadCast(readBufferCmdLine, RECV, readBufferId);
      }

      SetStdInOut(sign, fd, pipeNum, writeId, readId, UserPipeInError,
                  UserPipeOutError);
      pid_t pid;
      while ((pid = fork()) < 0) {
        /*  parent process no resource to fork child process, so wait for child
         * exit and release recourse   */
        int status = 0;
        waitpid(-1, &status, 0);
      }
      if (pid == 0)
        DoCmd(cmd, argV, pathV, fd, sSock);
      else if (pid > 0)
        pidV.push_back(pid);

      if (fd.in != sSock) {  // close read from pipe, the other entrance is
                             // closed in SetStdInOut
        close(fd.in);
      }

      ClosePipe(readId, writeId);
      ReducePipe();
    }

    if (!isNumPipe) {
      vector<pid_t>::iterator iter = pidV.begin();
      while (iter != pidV.end()) {
        int status;
        waitpid((*iter), &status, 0);
        iter++;
      }
    }
    ReducePipeNum();
  }
  return 0;
}

int PassiveTcp(int port) {
  const char *protocal = "tcp";

  struct sockaddr_in servAddr;
  struct protoent *proEntry;
  int mSock, type;

  bzero((char *)&servAddr, sizeof(servAddr));
  servAddr.sin_family = AF_INET;
  servAddr.sin_addr.s_addr = INADDR_ANY;
  servAddr.sin_port = htons(port);

  if ((proEntry = getprotobyname(protocal)) == NULL) {
    cerr << "Fail to get protocal entry" << endl;
    exit(1);
  }

  type = SOCK_STREAM;

  if ((mSock = socket(PF_INET, type, proEntry->p_proto)) < 0) {
    cerr << "Fail to create master socket, errno: " << errno << endl;
    exit(1);
  }

  int optval = 1;
  if (setsockopt(mSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1) {
    cout << "Error: Set socket failed" << endl;
    exit(1);
  }

  if (::bind(mSock, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
    cerr << "Fail to bind master socket, errno: " << errno << endl;
    exit(1);
  }

  if (listen(mSock, 5) < 0) {
    cerr << "Fail to listen master socket, errno: " << errno << endl;
    exit(1);
  }

  return mSock;
}
void BroadCast(string input, BroadcastSign sign, int sendrecvuid,
               string readBufferCmdLine, int readBufferId) {
  string buffer = "";
  switch (sign) {
    case YELL:
      buffer = "*** " + string(shmCurrentAddr->name) + " yelled ***: " + input;
      break;
    case ENTER:
      buffer = "*** User '(no name)' entered from " +
               string(shmCurrentAddr->ip) + ":" +
               to_string(shmCurrentAddr->port) + ". ***";
      break;
    case EXIT:
      buffer = "*** User '" + string(shmCurrentAddr->name) + "' left. ***";
      break;
    case NAME:
      buffer = "*** User from " + string(shmCurrentAddr->ip) + ":" +
               to_string(shmCurrentAddr->port) + " is named '" +
               string(shmCurrentAddr->name) + "'. ***";
      break;
    case RECV_SEND:
      RemoveLineFeed(readBufferCmdLine);
      RemoveLineFeed(input);
      buffer = "*** " + string(shmCurrentAddr->name) + " (#" +
               to_string(shmCurrentAddr->uid) + ") just received from " +
               string(shmStartAddr[readBufferId - 1].name) + " (#" +
               to_string(shmStartAddr[readBufferId - 1].uid) + ") by '" +
               readBufferCmdLine + "' ***";
      buffer += '\n';
      buffer += "*** " + string(shmCurrentAddr->name) + " (#" +
                to_string(shmCurrentAddr->uid) + ") just piped '" + input +
                "' to " + string(shmStartAddr[sendrecvuid - 1].name) + " (#" +
                to_string(shmStartAddr[sendrecvuid - 1].uid) + ") ***";
      break;
    case SEND:
      RemoveLineFeed(input);
      buffer = "*** " + string(shmCurrentAddr->name) + " (#" +
               to_string(shmCurrentAddr->uid) + ") just piped '" + input +
               "' to " + string(shmStartAddr[sendrecvuid - 1].name) + " (#" +
               to_string(shmStartAddr[sendrecvuid - 1].uid) + ") ***";
      break;
    case RECV:
      RemoveLineFeed(input);
      buffer = "*** " + string(shmCurrentAddr->name) + " (#" +
               to_string(shmCurrentAddr->uid) + ") just received from " +
               string(shmStartAddr[sendrecvuid - 1].name) + " (#" +
               to_string(shmStartAddr[sendrecvuid - 1].uid) + ") by '" + input +
               "' ***";
      break;
    default:
      break;
  }

  for (int i = 0; i < kMaxClient; i++) {
    if ((shmStartAddr + i)->pid != -1) {
      strcpy((shmStartAddr + i)->msg, buffer.c_str());
      kill((shmStartAddr + i)->pid, SIGUSR1);
    }
  }
}
void ClearClientTable() {
  for (int i = 0; i < kMaxClient; i++) {
    (shmCurrentAddr->recvInfo)[i] = false;
    (shmCurrentAddr->sendInfo)[i] = false;

    if (shmStartAddr[i].pid != -1) {
      for (int j = 0; j < kMaxClient; j++) {
        if ((shmStartAddr[i].sendInfo)[j] == shmCurrentAddr->uid)
          (shmStartAddr[i].sendInfo)[j] = false;
        if ((shmStartAddr[i].recvInfo)[j] == shmCurrentAddr->uid)
          (shmStartAddr[i].recvInfo)[j] = false;
      }
    }
  }
}
void ClearPipe() {
  /*  close all pipe related to client    */
  auto iter = pipeV.begin();
  while (iter != pipeV.end()) {
    if (iter->clientId == shmCurrentAddr->uid ||
        iter->recverId == shmCurrentAddr->uid) {
      close(iter->pipefd[0]);
      close(iter->pipefd[1]);
      delete[] iter->pipefd;
      pipeV.erase(iter);
      continue;
    }
    iter++;
  }
}

void ResetShmCurrent() {
  shmCurrentAddr->pid = -1;
  shmCurrentAddr->uid = -1;
  shmCurrentAddr->fd = -1;
  for (int i = 0; i < kMaxClient; i++) {
    shmCurrentAddr->sendInfo[i] = false;
    shmCurrentAddr->recvInfo[i] = false;
  }
  strcpy(shmCurrentAddr->msg, "");
  shmCurrentAddr = nullptr;
}

void ClientExit() {
  BroadCast("", EXIT, -1);
  ResetShmCurrent();
  ClearPipe();
}

void WelcomeUser(int sockfd) {
  string msg = "****************************************\n";
  write(sockfd, msg.c_str(), msg.size());
  msg = "** Welcome to the information server. **\n";
  write(sockfd, msg.c_str(), msg.size());
  msg = "****************************************\n";
  write(sockfd, msg.c_str(), msg.size());
}

void InitEnv() { setenv("PATH", "bin:.", 1); }

void InitFifo() {
  pathNameTable.resize(31, vector<string>(31));
  for (int i = 1; i <= 30; i++) {
    for (int j = 1; j <= 30; j++) {
      pathNameTable[i][j] =
          "user_pipe/fifo" + to_string(i) + "_" + to_string(j);
    }
  }
}

void InitShm() {
  if ((shmID = shmget(SHMKEY, sizeof(ClientInfo) * kMaxClient,
                      IPC_CREAT | 0600)) < 0) {
    cerr << "Fail to create shm space, errno: " << strerror(errno) << endl;
    exit(1);
  }

  if ((shmStartAddr = (ClientInfo *)shmat(shmID, nullptr, 0)) <
      (ClientInfo *)0) /* Attach a pointer to the first element of shm .*/
  {
    cerr << "Fail to attach shm, errno: " << errno << endl;
    exit(1);
  }
}

void InitClientTable() {
  for (int i = 0; i < kMaxClient; i++) {
    (shmStartAddr + i)->pid = -1;
    (shmStartAddr + i)->fd = -1;
    (shmStartAddr + i)->uid = -1;
    for (int j = 0; j < kMaxClient; j++) {
      ((shmStartAddr + i)->sendInfo)[j] = false;
      ((shmStartAddr + i)->recvInfo)[j] = false;
    }
    strcpy((shmStartAddr + i)->msg, "");
  }
}

ClientInfo *SetClient() {
  for (int i = 0; i < kMaxClient; i++) {
    if (shmStartAddr[i].pid == -1) {
      (shmStartAddr + i)->uid = i + 1;
      return shmStartAddr + i;
    }
  }
  return nullptr;
}

/*Signal(SIGINT) handler*/
void SigIntHandler(int signo) {
  while (wait(nullptr) > 0)
    ;

  shmdt(shmStartAddr);
  shmctl(shmID, IPC_RMID, NULL);

  exit(EXIT_SUCCESS);
}

/* Signal(SIGUSR1) handler */
void SigClient(int signo) {
  string msg(shmCurrentAddr->msg);
  strcpy(shmCurrentAddr->msg, "");
  msg += '\n';
  write(shmCurrentAddr->fd, msg.c_str(), msg.size());
}

int main(int argc, char *const argv[]) {
  if (argc > 2) {
    cerr << "Argument set error" << endl;
    exit(1);
  }

  struct sockaddr_in cliAddr;
  socklen_t cliLen;
  int mSock, sSock;
  int port = atoi(argv[1]);
  pid_t pid;

  /*  socket() bind() listen()    */
  mSock = PassiveTcp(port);

  /*  kill zombie */
  signal(SIGCHLD, SIG_IGN);
  signal(SIGUSR1, SigClient);

  // clean fifo and shm
  signal(SIGINT, SigIntHandler);

  InitShm();
  InitClientTable();
  InitFifo();

  while (1) {
    cliLen = sizeof(cliAddr);

    if ((sSock = accept(mSock, (struct sockaddr *)&cliAddr, &cliLen)) < 0) {
      /*  there is not client connect */
      if (errno == EINTR) continue;
      cerr << "Server fail to accept, errno: " << errno << endl;
      exit(1);
    }

    if ((pid = fork()) < 0) {
      cerr << "Server fail to fork" << endl;
      exit(1);
    } else if (pid == 0) {
      close(mSock);
      dup2(sSock, 0);
      dup2(sSock, 1);
      dup2(sSock, 2);
      InitEnv();

      shmCurrentAddr = SetClient();
      shmCurrentAddr->fd = sSock;
      shmCurrentAddr->pid = getpid();
      strcpy(shmCurrentAddr->name, "(no name)");
      strcpy(shmCurrentAddr->ip, inet_ntoa(cliAddr.sin_addr));
      shmCurrentAddr->port = ntohs(cliAddr.sin_port);

      WelcomeUser(sSock);
      BroadCast("", ENTER, -1);
      Shell(sSock);
      ClientExit();
      close(sSock);
      exit(EXIT_SUCCESS);
    } else
      close(sSock);
  }

  return 0;
}