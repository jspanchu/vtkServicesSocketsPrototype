// clang-format off
/**
 * Description: 
 *  Investigate communication b/w threads and processes asynchronously with vtkSocket and rxcpp.
 * Compile: 
 *  $ export RXCPP_DIR=/path/to/rxcpp/source
 *  $ export VTK_DIR=/path/to/vtk/build
 *  $ mkdir-p build && cd build && cmake -GNinja
 *  $ ninja
 * Run:
 *  Launch a server on port 1234
 *  $ ./main -s 1234 &
 *  Connect to a server and execute 100 remote commands.
 *  $ ./main -c localhost 1234 -n100 
 *
 */
// clang-format on

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <future>
#include <sstream>
#include <unordered_map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>

// cross-platform includes for socket API
#if defined(_WIN32) && !defined(__CYGWIN__)
#define VTK_WINDOWS_FULL
#include "vtkWindows.h" // has winsock2 headers
#define WSA_VERSION MAKEWORD(1, 1)
// these defines are copied from vtkSocket.cxx
#define vtkErrnoMacro (WSAGetLastError())
#define vtkStrerrorMacro(_num) (wsaStrerror(_num))
#else
#include <sys/socket.h> // required for shutdown
// these defines are copied from vtkSocket.cxx
#define vtkErrnoMacro (errno)
#define vtkStrerrorMacro(_num) (strerror(_num))
#endif

#include <vtkLogger.h>                  // for vtkLog()
#include <vtksys/SystemInformation.hxx> // for load
#include <vtkServerSocket.h>            // for server
#include <vtkClientSocket.h>            // for client
#include <vtkSmartPointer.h>            // for memory management of VTK objects

#include "rxcpp/rx-includes.hpp" // for rxcpp

/**
 * Encapsulate two SimpleSubject(s) used to enqueue incoming/outgoing messages.
 */
struct Communicator {
  // services can use the send subject to enqueue outgoing messages.
  rxcpp::subjects::subject<std::string> sendSbjct;
  unsigned long sendCounter = 1;
  // use the recvSubject to act upon new incoming messages.
  rxcpp::subjects::subject<std::string> recvSbjct;
  unsigned long recvCounter = 1;
};

// global communicator instance used by all services and the send, recv loops.
static Communicator comm;
// used to send exit signal to the server.
static std::promise<bool> exitServer;
// <serviceName, serviceGid>
static std::map<std::string, std::size_t> serviceRegistry;
// service subscriptions
static std::vector<rxcpp::composite_subscription> subscriptions;
// on client: socket used to communicate with server, on server: socket used to
// communicate with client
static vtkSmartPointer<vtkClientSocket> clientSocket;
// on client: does not exist, on server: used to accept client connections.
static vtkSmartPointer<vtkServerSocket> serverSocket;

// remove the service gid and return only the message.
std::string getMessage(const std::string &packet) {
  auto colonSep = packet.find(":");
  return packet.substr(colonSep + 1, packet.length() - colonSep);
}

// cross platform socket shutdown
void shutdownSocket(vtkSmartPointer<vtkSocket> socket) {
  int sockfd = socket->GetSocketDescriptor();
#if defined(_WIN32) && !defined(__CYGWIN__)
  shutdown(sockfd, SD_BOTH);
#else
  shutdown(sockfd, SHUT_RDWR);
#endif
}

// send messages using a vtkClientSocket.
void sendLoop(vtkSmartPointer<vtkClientSocket> socket) {
  comm.sendSbjct
      .get_observable()
      // enqueue on main thread.
      .filter([](std::string msg) { return (msg.length() != 0); })
      .tap([](std::string msg) {
        vtkLogF(INFO, "=> Enqueue msg: %s", getMessage(msg).c_str());
      })
      // switch to send thread and transmit from there.
      .observe_on(rxcpp::observe_on_new_thread())
      .subscribe(
          [socket](std::string msg) {
            vtkLogger::SetThreadName("comm.send");
            int size[1] = {static_cast<int>(msg.size())};
            // send the size of message first, then the message itself.
            int status = socket->Send(size, sizeof(size));
            vtkLogIfF(ERROR, status != 1, "=> Send size failed! %s",
                      vtkStrerrorMacro(vtkErrnoMacro));
            status = socket->Send(msg.data(), size[0]);
            vtkLogIfF(ERROR, status != 1, "=> Send data failed! %s",
                      vtkStrerrorMacro(vtkErrnoMacro));
            vtkLogF(TRACE, "=> [%lu] send \'%s\'", comm.sendCounter,
                    msg.c_str());
            comm.sendCounter++;
          },
          []() { vtkLog(INFO, "Send complete") });
}

// recv messages using a vtkClientSocket
void recvLoop(vtkSmartPointer<vtkClientSocket> socket) {
  vtkLogger::SetThreadName("comm.recv");
  std::vector<char> buf(128, 0);

  // Blocking on a receive is a non-busy wait system call.
  // Closing the socket on either side will return 0 and exit the loop
  while (true) {
    int size[1] = {};
    int recvd = socket->Receive(size, sizeof(size), 1);
    if (recvd == 0) {
      // socket closed. (this or other end)
      vtkLog(TRACE, << "=> Recvd 0 bytes.");
      break;
    } else if (recvd == -1) {
      vtkLog(ERROR, << vtkStrerrorMacro(vtkErrnoMacro));
      break;
    }
    buf.resize(size[0]);
    recvd = socket->Receive(buf.data(), size[0]);
    if (recvd == 0) {
      // socket closed. (this or other end)
      vtkLog(TRACE, << "=> Recvd 0 bytes.");
      break;
    } else if (recvd == -1) {
      vtkLog(ERROR, << vtkStrerrorMacro(vtkErrnoMacro));
      break;
    }
    std::string msg(buf.data(), recvd);
    vtkLogF(TRACE, "=> [%lu] recv \'%s\'", comm.recvCounter, msg.c_str());
    comm.recvCounter++;
    comm.recvSbjct.get_subscriber().on_next(msg);
  }
  // no longer receiving messages.
  comm.recvSbjct.get_subscriber().on_completed();
  vtkLogF(INFO, "exit");
  // server no longer needs to run.
  try {
    exitServer.set_value(true);
  } catch (std::future_error&) {
    vtkLog(INFO, "Server interrupted");
  }
}

// emulate a service.
rxcpp::composite_subscription serviceEmulator(const std::string serviceName,
                                              const std::size_t serviceId) {
  return comm.recvSbjct
      .get_observable()
      // ignore zero-length messages
      .filter([](auto msg) { return (msg.length() != 0); })
      // route to correct destination
      .filter([serviceId](std::string msg) {
        return (msg.find(std::to_string(serviceId)) != std::string::npos);
      })
      // remove that service gid.
      .map([](std::string msg) {
        auto colonSep = msg.find(":");
        return msg.substr(colonSep + 1, msg.length() - colonSep);
      })
      // log message without gid.
      .tap([](std::string msg) {
        vtkLogF(TRACE, "=> Enqueue msg: %s", msg.c_str());
      })
      // switch to service thread.
      .observe_on(rxcpp::observe_on_new_thread())
      // service can now act accordingly. here, we log the message.
      .subscribe(
          // on_next
          [serviceName](auto msg) {
            vtkLogger::SetThreadName(serviceName);
            vtkLogF(INFO, "%s", msg.c_str());
            // send a reply.
            std::string reply = std::string("response-") + msg;
            comm.sendSbjct.get_subscriber().on_next(reply);
          },
          // on_completed
          []() { vtkLogF(INFO, "exit"); });
}

// called from server (client can also call it)
void createService(const std::string serviceName) {
  const auto gid = std::hash<std::string>{}(serviceName);
  const auto serviceItem =
      std::make_pair(std::string("services.") + serviceName, gid);
  serviceRegistry.emplace(serviceItem);
  // establishes role of service by setting up service subscription.
  subscriptions.emplace_back(
      serviceEmulator(serviceItem.first, serviceItem.second));
  vtkLogF(INFO, "=> Registered %s:%zu", serviceItem.first.c_str(),
          serviceItem.second);
}

// tie everything together here.
int main(int argc, char *argv[]) {

  std::string addr;
  int port = 1234;
  int numMessages = 20;

#if defined(_WIN32) && !defined(__CYGWIN__)
  WSAData wsaData;
  if (WSAStartup(WSA_VERSION, &wsaData)) {
    vtkLog(ERROR, "Could not initialize sockets !");
  }
#endif

  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    switch (arg[1]) {
    case 'c': {
      clientSocket = vtk::TakeSmartPointer(vtkClientSocket::New());
      addr = argv[i + 1];
      port = std::atoi(argv[i + 2]);
      break;
    }
    case 's': {
      serverSocket = vtk::TakeSmartPointer(vtkServerSocket::New());
      addr = "localhost";
      port = std::atoi(argv[i + 1]);
      break;
    }
    case 'v': {
      vtkLogger::SetStderrVerbosity(
          vtkLogger::ConvertToVerbosity(std::atoi(&arg[2])));
      break;
    }
    case 'n': {
      numMessages = std::atoi(&arg[2]);
      break;
    }
    default:
      break;
    }
  }

  vtkLogger::Init(argc, argv, nullptr);

  if (clientSocket == nullptr) {
    if (serverSocket->CreateServer(port) < 0) {
      vtkLogF(ERROR, "=> Failed to create server on %d", port);
    } else {
      vtkLogF(INFO, "=> Waiting for connection: %s:%d", addr.c_str(),
              serverSocket->GetServerPort());
      clientSocket = vtk::TakeSmartPointer(serverSocket->WaitForConnection());
      vtkLogF(INFO, "=> Client connected!");
    }
  } else {
    if (clientSocket->ConnectToServer(addr.c_str(), port) < 0) {
      vtkLogF(ERROR, "=> Connection to %s:%d failed!", addr.c_str(), port);
    } else {
      vtkLogF(INFO, "=> Connected to %s:%d", addr.c_str(), port);
    }
  }

  if (!clientSocket->GetConnectingSide()) {
    createService("data");
    createService("render");
    createService("io");
    std::stringstream msg;
    vtkLog(INFO, << "=> Advertise service registry");
    for (const auto &serviceItem : serviceRegistry) {
      msg << serviceItem.first << ":" << serviceItem.second << '|';
    }
    clientSocket->Send(msg.str().data(), static_cast<int>(msg.str().size()));
  } else {
    // client can have it's own services.
    // however, it can get the advertised services from the server.
    std::stringstream msgStream;
    // what if message is bigger than 1024?
    char buf[1024] = {};
    int recvd = clientSocket->Receive(buf, sizeof(buf), 0);
    std::string msg(buf, recvd);
    vtkLog(INFO, << "=> Receiving service registry");
    while (!msg.empty()) {
      auto itemSep = msg.find_first_of('|');
      auto colonSep = msg.find_first_of(':');
      if (itemSep != std::string::npos && colonSep != std::string::npos) {
        std::string serviceName = msg.substr(0, colonSep);
        msg = msg.replace(0, colonSep + 1, "");
        std::string servicegid = msg.substr(0, itemSep - colonSep - 1);
        msg = msg.replace(0, itemSep - colonSep, "");
        std::stringstream gidStream;
        gidStream << servicegid;
        std::size_t gid = 0;
        gidStream >> gid;
        serviceRegistry.emplace(std::make_pair(serviceName, gid));
        vtkLogF(INFO, "=> %s:%zu", serviceName.c_str(), gid);
      }
    }
  }

  // launch sender and receiver threads. communicator gets to work.
  sendLoop(clientSocket);
  std::thread receiver(&recvLoop, clientSocket);

  // fake some messages on three services.
  std::vector<std::string> dataMsgs = {
      std::to_string(serviceRegistry.at("services.data")) + ":update-state",
      std::to_string(serviceRegistry.at("services.data")) + ":object-delete",
      std::to_string(serviceRegistry.at("services.data")) + ":update-pipeline"};
  std::vector<std::string> renderMsgs = {
      std::to_string(serviceRegistry.at("services.render")) + ":render",
      std::to_string(serviceRegistry.at("services.render")) + ":render-2",
      std::to_string(serviceRegistry.at("services.render")) + ":render-3",
      std::to_string(serviceRegistry.at("services.render")) + ":render-4"};
  std::vector<std::string> ioMsgs = {
      std::to_string(serviceRegistry.at("services.io")) + ":read",
      std::to_string(serviceRegistry.at("services.io")) + ":read-2",
      std::to_string(serviceRegistry.at("services.io")) + ":write",
      std::to_string(serviceRegistry.at("services.io")) + ":read-3",
      std::to_string(serviceRegistry.at("services.io")) + ":write-2",
      std::to_string(serviceRegistry.at("services.io")) + ":read-4"};

  std::vector<std::vector<std::string>> messagePool(
      {dataMsgs, renderMsgs, ioMsgs});

  int counter = 0;
  std::random_device rd;
  std::mt19937 rng(rd());
  std::uniform_int_distribution<std::size_t> poolIdxRnd(0, 2);
  std::vector<std::uniform_int_distribution<std::size_t>> msgIdxRnds;
  msgIdxRnds.emplace_back(0, dataMsgs.size() - 1);
  msgIdxRnds.emplace_back(0, renderMsgs.size() - 1);
  msgIdxRnds.emplace_back(0, ioMsgs.size() - 1);

  // before we begin sending messages, wait for communicator to initialize.
  while (!comm.sendSbjct.has_observers()) {
    vtkLog(TRACE, "=> wait for comm initialize");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (clientSocket->GetConnectingSide()) {
    // client issues remote commands on the server.
    // here, it can handle responses from remote services.
    std::atomic<int> numResponses = 0;
    auto responseSubscription =
        comm.recvSbjct
            .get_observable()
            // ideally, we want to setup rxcpp notify on earlier wakeup.
            // to handle responses on main thread. but that is more work.
            // so handle responses on a new thread.
            .observe_on(rxcpp::observe_on_new_thread())
            .subscribe([&numResponses](std::string msg) {
              vtkLogger::SetThreadName("response");
              vtkLogF(INFO, "reply: %s", msg.c_str());
              numResponses++;
            });
    while (counter < numMessages) {
      // pick a random message collection from the pool.
      auto poolIdx = poolIdxRnd(rng);
      auto msgIdx = msgIdxRnds[poolIdx](rng);
      // message the service
      auto msg = messagePool[poolIdx][msgIdx];
      comm.sendSbjct.get_subscriber().on_next(msg);
      ++counter;
    }
    // wait until responses are received.
    while (numMessages != numResponses) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    vtksys::SystemInformation info;
    responseSubscription.unsubscribe();
    vtkLogF(INFO, "Average load %f", info.GetLoadAverage());
  } else {
    std::signal(SIGINT, [](int) {
      vtkLog(INFO, "Caught SIGINT");
      try {
        exitServer.set_value(true);
      } catch (std::future_error&) {
        vtkLog(INFO, "Client already disconnected");
      }
    });
    // wait for exitServer to be signalled.
    // it can be signalled from recvLoop when
    // client disconnects or SIGINT.
    auto fut = exitServer.get_future();
    fut.wait();
    vtksys::SystemInformation info;
    vtkLogF(INFO, "Average load %f", info.GetLoadAverage());
  }

  vtkLog(INFO, "=> Unsubscribe services");
  for (auto &subscription : subscriptions) {
    subscription.unsubscribe();
  }
  subscriptions.clear();
  serviceRegistry.clear();

  // on server side, wait for client to exit.
  if (!clientSocket->GetConnectingSide()) {
    while (clientSocket->GetConnected()) {
      vtkLogF(INFO, "Wait for client to disconnect");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      // shutting down the socket will unblock the recv thread
      shutdownSocket(clientSocket);
      // join receiver now, otherwise recv may raise bad file descriptor.
      vtkLog(INFO, "=> Shutdown receiver");
      receiver.join();
      clientSocket->CloseSocket();
    }
    vtkLog(INFO, "=> Shutdown server");
    serverSocket->CloseSocket();
  } else {
    vtkLog(INFO, "=> Shutdown client");
    shutdownSocket(clientSocket);
    // join receiver now, otherwise recv may raise bad file descriptor.
    vtkLog(INFO, "=> Shutdown receiver");
    receiver.join();
    clientSocket->CloseSocket();
  }

  return 0;
}
