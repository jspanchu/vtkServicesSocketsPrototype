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

#include <vtkLogger.h>       // for vtkLog()
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

// remove the service guid and return only the message.
std::string getMessage(const std::string &packet) {
  auto colonSep = packet.find(":");
  return packet.substr(colonSep + 1, packet.length() - colonSep);
}

// send messages using a vtkClientSocket.
void sendLoop(vtkSmartPointer<vtkClientSocket> socket) {
  auto runLoop = std::make_shared<rxcpp::schedulers::run_loop>();
  vtkLogger::SetThreadName("comm.send");
  unsigned long sendCounter = 1;

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
        int size[1] = {static_cast<int>(msg.size())};
        // send the size of message first, then the message itself.
        int status = socket->Send(size, sizeof(size)) &&
                     socket->Send(msg.data(), msg.size());
        vtkLogIfF(ERROR, status != 1, "=> Send failed!");
        vtkLogF(TRACE, "=> [%lu] send \'%s\'", sendCounter, msg.c_str());
        sendCounter++;
      });

  //   runLoop->set_notify_earlier_wakeup([&](auto when) {
  //     auto duration = std::chrono::duration<double>(when - runLoop->now());
  //     TODO: figure this out.
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
  unsigned long recvCounter = 1;
  std::vector<char> buf(128, 0);

  while (!exitComm.load() && socket->GetConnected()) {
    const int socks[1] = {socket->GetSocketDescriptor()};
    int selected = -1;
    // timeout of 0 would block this thread. let's timeout after 5ms to keep it
    // running.
    int status = vtkSocket::SelectSockets(socks, 1, 1, &selected);
    if (status == 0) {
      // timeout
      continue;
    } else if (status == -1) {
      // error
      vtkLog(ERROR, << "=> Failed to select socket");
    } else if (status == 1) {
      // success
      int size[1] = {};
      int recvd = socket->Receive(size, sizeof(size), 1);
      if (recvd == 0) {
        // other end of socket closed.
        vtkLog(TRACE, << "=> Recvd 0 bytes.");
        break;
      }
      buf.resize(size[0]);
      recvd = socket->Receive(buf.data(), size[0]);
      if (recvd == 0) {
        // other end of socket closed.
        vtkLog(TRACE, << "=> Recvd 0 bytes.");
        break;
      }
      std::string msg(buf.data(), recvd);
      vtkLogF(TRACE, "=> [%lu] recv \'%s\'", recvCounter, msg.c_str());
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
  vtkLogF(INFO, "=> Registered %s:%zu", serviceItem.first.c_str(),
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
    vtkLog(INFO, << "=> Advertise service registry");
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
    int recvd = clientSocket->Receive(buf, sizeof(buf), 0);
    std::string msg(buf, recvd);
    vtkLog(INFO, << "=> Receiving service registry");
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

  // launch sender and receiver threads. communicator gets to work.
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

  // before we begin sending messages, wait for communicator to initialize.
  while (!comm.sendSbjct.has_observers()) {
    vtkLog(TRACE, "=> wait for comm initialize");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  while (++counter < 20) {
    if (clientSocket->GetConnectingSide()) {
      // pick a random message collection from the pool.
      auto poolIdx = poolIdxRnd(rng);
      auto msgIdx = msgIdxRnds[poolIdx](rng);
      // message the service
      auto msg = messagePool[poolIdx][msgIdx];
      comm.sendSbjct.get_subscriber().on_next(msg);
    }
  }
  // no longer sending messages.
  comm.sendSbjct.get_subscriber().on_completed();

  // server will run to shutdown services, so let's wait a bit.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // terminate services. (order SHOULD not matter)
  vtkLog(INFO, "=> Shutdown services");
  exitServices.store(true);
  for (auto &service : services) {
    service->join();
  }
  // terminate send, recv loops. (order SHOULD not matter)
  vtkLog(INFO, "=> Shutdown comm");
  exitComm.store(true);
  sender.join();
  receiver.join();

  // on server side, wait for client to exit.
  if (!clientSocket->GetConnectingSide()) {
    while (clientSocket->GetConnected()) {
      vtkLogF(INFO, "Wait for client to disconnect");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      clientSocket->CloseSocket();
    }
    vtkLog(INFO, "=> Shutdown server");
    serverSocket->CloseSocket();
  } else {
    vtkLog(INFO, "=> Shutdown client");
    clientSocket->CloseSocket();
  }

  return 0;
}
