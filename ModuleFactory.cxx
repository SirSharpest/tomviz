/******************************************************************************

  This source file is part of the TEM tomography project.

  Copyright Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/
#include "ModuleFactory.h"

#ifdef DAX_DEVICE_ADAPTER
#  include "dax/ModuleAccelThreshold.h"
#endif

#include "ModuleContour.h"
#include "ModuleOrthogonalSlice.h"
#include "ModuleOutline.h"
#include "ModuleThreshold.h"
#include "ModuleVolume.h"
#include "pqView.h"
#include "Utilities.h"
#include "vtkSMViewProxy.h"

#include <QtAlgorithms>

namespace TEM
{

//-----------------------------------------------------------------------------
ModuleFactory::ModuleFactory()
{
}

//-----------------------------------------------------------------------------
ModuleFactory::~ModuleFactory()
{
}

//-----------------------------------------------------------------------------
QList<QString> ModuleFactory::moduleTypes(
  vtkSMSourceProxy* dataSource, vtkSMViewProxy* view)
{
  QList<QString> reply;
  if (dataSource && view)
    {

    // based on the data type and view, return module types.
    reply << "Outline"
      << "Volume"
      << "Contour"
      << "Threshold"
      << "Orthogonal Slice";
    qSort(reply);
    }
  return reply;
}

//-----------------------------------------------------------------------------
Module* ModuleFactory::createModule(
  const QString& type, vtkSMSourceProxy* dataSource, vtkSMViewProxy* view)
{
  Module* module = NULL;
  if (type == "Outline")
    {
    module = new ModuleOutline();
    }
  else if (type == "Contour")
    {
    module = new ModuleContour();
    }
  else if (type == "Volume")
    {
    module = new ModuleVolume();
    }
  else if (type == "Orthogonal Slice")
    {
    module = new ModuleOrthogonalSlice();
    }
  else if (type == "Threshold")
    {
#ifdef DAX_DEVICE_ADAPTER
    module = new ModuleAccelThreshold();
#else
    module = new ModuleThreshold();
#endif
    }

  if (module)
    {
    if (!module->initialize(dataSource, view))
      {
      delete module;
      return NULL;
      }

    pqView* pqview = TEM::convert<pqView*>(view);
    pqview->resetDisplay();
    pqview->render();
    }
  return module;
}

} // end of namespace TEM
