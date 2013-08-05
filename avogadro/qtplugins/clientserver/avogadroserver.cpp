/******************************************************************************

 This source file is part of the Avogadro project.

 Copyright 2013 Kitware, Inc.

 This source code is released under the New BSD License, (the "License").

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.

 ******************************************************************************/

#include "avogadroserver.h"
#include "avoremotemoleculeservice.h"
#include "RemoteMoleculeService_Dispatcher.pb.h"

#include <vtkSocketController.h>
#include <vtkClientSocket.h>
#include <vtkServerSocket.h>
#include <vtkSocketCommunicator.h>

#include <protocall/runtime/servicemanager.h>
#include <protocall/runtime/vtkcommunicatorchannel.h>

#include <algorithm>

using std::vector;
using std::list;
using ProtoCall::Runtime::vtkCommunicatorChannel;
using ProtoCall::Runtime::RpcChannel;


AvogadroServer::~AvogadroServer()
{
// TODO Auto-generated destructor stub
}

AvogadroServer::AvogadroServer()
{
  // It's essential to initialize the socket controller to initialize sockets on
  // Windows.
  vtkSocketController* controller =  vtkSocketController::New();
  controller->Initialize();
  controller->Delete();
}

void AvogadroServer::listen(int port)
{
  m_socket =  vtkServerSocket::New();
  if (m_socket->CreateServer(port) != 0)
  {
     std::cerr << "Failed to set up server socket.\n";
     m_socket->Delete();
     return;
  }

  std::cout << "Listening on localhost:8888" << std::endl;

  while (true)
    processConnectionEvents();
}

void AvogadroServer::accept()
{
  std::cout << "accept\n";
  vtkCommunicatorChannel *channel = NULL;

  while (!channel) {
    std::cout << "channel\n";
    vtkClientSocket* client_socket = NULL;
    client_socket = m_socket->WaitForConnection(100);

    if (!client_socket)
      return;

    vtkSocketController* controller = vtkSocketController::New();
    vtkSocketCommunicator* comm = vtkSocketCommunicator::SafeDownCast(
        controller->GetCommunicator());
    comm->SetReportErrors(0);
    comm->SetSocket(client_socket);
    client_socket->FastDelete();
    channel = new vtkCommunicatorChannel(comm);

    comm->ServerSideHandshake();
  }

  m_clientChannels.push_back(channel);


}

void AvogadroServer::processConnectionEvents()
{
  int timeout = 200;
  vector<int> socketsToSelect;
  vector<vtkCommunicatorChannel *> channels;

  for (list<vtkCommunicatorChannel *>::iterator it
    = m_clientChannels.begin(); it != m_clientChannels.end(); ++it) {

    vtkCommunicatorChannel *channel = *it;

    vtkSocketCommunicator *comm = channel->communicator();
    vtkSocket* socket = comm->GetSocket();
    if (socket && socket->GetConnected()) {
      socketsToSelect.push_back(socket->GetSocketDescriptor());
      channels.push_back(channel);
    }
  }

  // Add server socket first, we are looking for incoming connections
  socketsToSelect.push_back(m_socket->GetSocketDescriptor());

  int selectedIndex = -1;
  int result = vtkSocket::SelectSockets(&socketsToSelect[0],
                                        socketsToSelect.size(), timeout,
                                        &selectedIndex);
  if (result < 0) {
    std::cerr << "Socket select failed with error code: " << result << std::endl;
    return;
  }

  if (selectedIndex == -1)
    return;

  // Are we dealing with an incoming connection?
  if (selectedIndex == socketsToSelect.size()-1) {
    accept();
  }
  // We have a message waiting from a client
  else {
    std::cout << "recieve\n";
    RpcChannel *channel = channels[selectedIndex];
    if (!channel->receive(true)) {
      // Connection lost remove channel from list
      list<vtkCommunicatorChannel *>::iterator it
        = std::find(m_clientChannels.begin(), m_clientChannels.end(), channel);
      m_clientChannels.erase(it);
    }

  }
}



int main(int argc, char *argv[])
{
  // Register the RPC service
  ProtoCall::Runtime::ServiceManager *mgr
    = ProtoCall::Runtime::ServiceManager::instance();
  AvoRemoteMoleculeService service;
  AvoRemoteMoleculeService::Dispatcher dispatcher(&service);
  mgr->registerService(&dispatcher);

  AvogadroServer server;
  server.listen(8888);

}
