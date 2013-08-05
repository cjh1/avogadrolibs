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

#ifndef AVOREMOTEMOLECULESERVICE_H_
#define AVOREMOTEMOLECULESERVICE_H_

#include "RemoteMoleculeService.pb.h"

class AvoRemoteMoleculeService : public RemoteMoleculeService
{
public:
  AvoRemoteMoleculeService();
  virtual ~AvoRemoteMoleculeService();

  void open(const OpenRequest* input, OpenResponse* output, ::google::protobuf::Closure* done);


};

#endif /* AVOREMOTEMOLECULESERVICE_H_ */
