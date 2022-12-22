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
 *  $ ./main -s 1234 &
 *  $ ./main -c localhost 1234
 *
 */
// clang-format on

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>

#include <vtkLogger.h>       // for vtkLog(..)
#include <vtkServerSocket.h> // for server
#include <vtkClientSocket.h> // for client
#include <vtkSmartPointer.h> // for memory management of VTK objects

#include "rxcpp/rx-includes.hpp" // for rxcpp

/**
 * Encapsulate two SimpleSubject(s) used to enqueue incoming/outgoing messages.
 */
struct Communicator {
  // services can use the send subject to enqueue outgoing messages.
  rxcpp::subjects::subject<std::string> sendSbjct;
  // use the recvSubject to act upon new incoming messages.
  rxcpp::subjects::subject<std::string> recvSbjct;
};

// global communicator instance used by all services and the send, recv loops.
static Communicator comm;
static std::atomic<bool> exitComm;
static std::atomic<bool> exitServices;
static std::map<std::string, std::size_t> serviceRegistry;
std::vector<std::unique_ptr<std::thread>> services;
static vtkSmartPointer<vtkClientSocket> clientSocket;
static vtkSmartPointer<vtkServerSocket> serverSocket;

std::string getMessage(const std::string &packet) {
  auto colonSep = packet.find(":");
  return packet.substr(colonSep + 1, packet.length() - colonSep);
}

// send messages using a vtkClientSocket.
void sendLoop(vtkSmartPointer<vtkClientSocket> socket) {
  auto runLoop = std::make_shared<rxcpp::schedulers::run_loop>();
  vtkLogger::SetThreadName("comm.send");
  uint64_t sendCounter = 1;

  comm.sendSbjct
      .get_observable()
      // enqueue on main thread.
      .filter([](std::string msg) { return (msg.length() != 0); })
      .tap([](std::string msg) {
        vtkLogF(INFO, "=> Enqueue msg: %s", getMessage(msg).c_str());
      })
      // switch to send thread and transmit from there.
      .observe_on(rxcpp::observe_on_run_loop(*runLoop))
      .subscribe([&socket, &sendCounter](std::string msg) {
        int status = socket->Send(msg.data(), msg.size());
        vtkLogF(TRACE, "=> [%ld] send \'%s\'", sendCounter, msg.c_str());
        sendCounter++;
      });

  //   runLoop->set_notify_earlier_wakeup([&](auto when) {
  //     auto duration = std::chrono::duration<double>(when - runLoop->now());
  //     std::this_thread::sleep_for(duration);
  //   });
  while (!exitComm.load() && socket->GetConnected()) {
    while (!runLoop->empty() && runLoop->peek().when < runLoop->now()) {
      runLoop->dispatch();
    }
  }
  vtkLogF(INFO, "exit");
}

// recv messages using a vtkClientSocket
void recvLoop(vtkSmartPointer<vtkClientSocket> socket) {
  vtkLogger::SetThreadName("comm.recv");
  uint64_t recvCounter = 1;

  while (!exitComm.load() && socket->GetConnected()) {
    char buf[64] = {};
    const int socks[1] = {socket->GetSocketDescriptor()};
    int selected = -1;
    // timeout of 0 would block this thread. let's timeout after 5ms to keep it
    // running.
    int status = vtkSocket::SelectSockets(socks, 1, 5, &selected);
    if (status == 0) {
      // timeout
      continue;
    } else if (status == -1) {
      // error
      vtkLog(ERROR, << "=> Failed to select socket");
    } else if (status == 1) {
      // success
      int recvd = socket->Receive(buf, sizeof(buf) * sizeof(char), 0);
      if (recvd == 0) {
        // other end of socket closed.
        vtkLog(TRACE, << "=> Recvd 0 bytes.");
        break;
      }
      std::string msg(buf, recvd);
      vtkLogF(TRACE, "=> [%ld] recv \'%s\'", recvCounter, msg.c_str());
      recvCounter++;
      comm.recvSbjct.get_subscriber().on_next(msg);
    }
  }
  // no longer receiving messages.
  comm.recvSbjct.get_subscriber().on_completed();
  vtkLogF(INFO, "exit");
}

// emulate a service.
void serviceEmulator(const std::string serviceName,
                     const std::size_t serviceId) {
  auto runLoop = std::make_shared<rxcpp::schedulers::run_loop>();
  vtkLogger::SetThreadName(serviceName);

  comm.recvSbjct
      .get_observable()
      // ignore zero-length messages
      .filter([](auto msg) { return (msg.length() != 0); })
      // route to correct destination
      .filter([&serviceId](std::string msg) {
        return (msg.find(std::to_string(serviceId)) != std::string::npos);
      })
      // remove that service guid.
      .map([](std::string msg) {
        auto colonSep = msg.find(":");
        return msg.substr(colonSep + 1, msg.length() - colonSep);
      })
      // log message without guid.
      .tap([](std::string msg) {
        vtkLogF(TRACE, "=> Enqueue msg: %s", msg.c_str());
      })
      // switch to service thread.
      .observe_on(rxcpp::observe_on_run_loop(*runLoop))
      // service can now act accordingly. here, we log the message.
      .subscribe([](auto msg) { vtkLogF(INFO, "%s", msg.c_str()); });
  while (!exitServices.load()) {
    while (!runLoop->empty() && runLoop->peek().when < runLoop->now()) {
      runLoop->dispatch();
    }
  }
  vtkLogF(INFO, "exit");
}

// called from server (client can also call it)
void createService(const std::string serviceName) {
  const auto guid = std::hash<std::string>{}(serviceName);
  const auto serviceItem =
      std::make_pair(std::string("services.") + serviceName, guid);
  serviceRegistry.emplace(serviceItem);
  services.emplace_back(std::make_unique<std::thread>(
      &serviceEmulator, serviceItem.first, serviceItem.second));
  vtkLogF(INFO, "Registered %s:%zu", serviceItem.first.c_str(),
          serviceItem.second);
}

// tie everything together here.
int main(int argc, char *argv[]) {

  std::string addr;
  int port;

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

  exitServices.store(false);
  if (!clientSocket->GetConnectingSide()) {
    createService("data");
    createService("render");
    createService("io");
    std::stringstream msg;
    for (const auto &serviceItem : serviceRegistry) {
      msg << serviceItem.first << ":" << serviceItem.second << '|';
    }
    clientSocket->Send(msg.str().data(), msg.str().size());
  } else {
    // client can have it's own services.
    // however, it can get the advertised services from the server.
    std::stringstream msgStream;
    // what if message is bigger than 1024?
    char buf[1024] = {};
    int recvd = clientSocket->Receive(buf, sizeof(buf) * sizeof(char), 0);
    std::string msg(buf, recvd);
    vtkLog(INFO, << "=>Receiving service registry..");
    while (!msg.empty()) {
      auto itemSep = msg.find_first_of('|');
      auto colonSep = msg.find_first_of(':');
      if (itemSep != std::string::npos && colonSep != std::string::npos) {
        std::string serviceName = msg.substr(0, colonSep);
        msg = msg.replace(0, colonSep + 1, "");
        std::string serviceGuid = msg.substr(0, itemSep - colonSep - 1);
        msg = msg.replace(0, itemSep - colonSep, "");
        std::stringstream guidStream;
        guidStream << serviceGuid;
        std::size_t guid;
        guidStream >> guid;
        serviceRegistry.emplace(std::make_pair(serviceName, guid));
        vtkLogF(INFO, "=> %s:%zu", serviceName.c_str(), guid);
      }
    }
  }

  exitComm.store(false);
  std::thread sender(&sendLoop, clientSocket);
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

  while (++counter < 20) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (clientSocket->GetConnectingSide()) {
      // pick a random message collection from the pool.
      auto poolIdx = poolIdxRnd(rng);
      auto msgIdx = msgIdxRnds[poolIdx](rng);
      // message the service
      auto msg = messagePool[poolIdx][msgIdx];
      comm.sendSbjct.get_subscriber().on_next(msg);
    }
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
  // no longer sending messages.
  comm.sendSbjct.get_subscriber().on_completed();

  // terminate services. (order SHOULD not matter)
  vtkLog(INFO, "=> Shutdown services..");
  exitServices.store(true);
  for (auto &service : services) {
    service->join();
  }
  // terminate send, recv loops. (order SHOULD not matter)
  vtkLog(INFO, "=> Shutdown comm..");
  exitComm.store(true);
  sender.join();
  receiver.join();

  // on server side, wait for client to exit.
  if (!clientSocket->GetConnectingSide()) {
    while (clientSocket->GetConnected()) {
      vtkLogF(INFO, "Wait for client to disconnect..");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      clientSocket->CloseSocket();
    }
    vtkLog(INFO, "=> Shutdown server..");
    serverSocket->CloseSocket();
  } else {
    vtkLog(INFO, "=> Shutdown client..");
    clientSocket->CloseSocket();
  }

  return 0;
}