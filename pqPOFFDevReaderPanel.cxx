/*=========================================================================

   Program: ParaView
   Module:    pqPOFFReaderPanel.cxx

   Copyright (c) 2005-2010 Sandia Corporation, Kitware Inc.
   All rights reserved.

   ParaView is a free software; you can redistribute it and/or modify it
   under the terms of the ParaView license version 1.2.

   See License_v1.2.txt for the full ParaView license.
   A copy of this license can be obtained by contacting
   Kitware Inc.
   28 Corporate Drive
   Clifton Park, NY 12065
   USA

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

=========================================================================*/
// This class was developed by Takuya Oshima at Niigata University,
// Japan (oshima@eng.niigata-u.ac.jp).
// OPENFOAM(R) is a registered trade mark of OpenCFD Limited, the
// producer of the OpenFOAM software and owner of the OPENFOAM(R) and
// OpenCFD(R) trade marks. This code is not approved or endorsed by
// OpenCFD Limited.

#include "pqPOFFDevReaderPanel.h"

#include "vtkPVConfig.h" // for PARAVIEW_VERSIONs

// Qt
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

// UI
#include "pqAnimationScene.h"
#include "pqApplicationCore.h"
#include "pqCollapsedGroup.h"
#include "pqNamedWidgets.h"
#include "pqObjectBuilder.h"
#include "pqPipelineRepresentation.h"
#include "pqPipelineSource.h"
#include "pqPropertyLinks.h"
#include "pqServerManagerModel.h"
#include "pqView.h"

// server manager
#include "vtkSMDoubleVectorProperty.h"
#include "vtkSMEnumerationDomain.h"
#include "vtkSMIntVectorProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMRenderViewProxy.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMStringVectorProperty.h"

// VTK
#include "vtkClientServerStream.h"
#include "vtkEventQtSlotConnect.h"
#include "vtkProcessModule.h"
#include "vtkSmartPointer.h"
#include "vtkVariantArray.h"

// Detect if the ParaView version is 3.11.1 or newer which has
// Collaboration support
// cf. http://www.paraview.org/pipermail/paraview/2011-April/020853.html
#if PARAVIEW_VERSION_MAJOR > 3 \
    || (PARAVIEW_VERSION_MAJOR == 3 && PARAVIEW_VERSION_MINOR > 11) \
    || (PARAVIEW_VERSION_MAJOR == 3 && PARAVIEW_VERSION_MINOR == 11 \
        && PARAVIEW_VERSION_PATCH >= 1)
#define PQ_POPENFOAMPANEL_COLLABORATION 1
#define PQ_POPENFOAMPANEL_UPDATE_RENDER 1
#include "vtkSMSession.h"
#else
#define PQ_POPENFOAMPANEL_COLLABORATION 0
#define PQ_POPENFOAMPANEL_UPDATE_RENDER 0
#endif

// Check if API changes for multi-server support
// (d0cdf44e00562479730af3b33abf4fada10034a4) is present
#if PARAVIEW_VERSION_MAJOR > 3 \
    || (PARAVIEW_VERSION_MAJOR == 3 && PARAVIEW_VERSION_MINOR >= 14)
#define PQ_POPENFOAMPANEL_MULTI_SERVER 1
#include "vtkSMSessionProxyManager.h"
#else
#define PQ_POPENFOAMPANEL_MULTI_SERVER 0
#include "vtkSMProxyManager.h"
#endif

// Check if AddPropToNonCompositedRenderer() is present
// (Removed by ef2cf1aefd09cd534197dabd7332a6c2c6473e9b)
#ifndef PQ_POPENFOAMPANEL_NO_ADDPROP
#define PQ_POPENFOAMPANEL_NO_ADDPROP 0
#endif

///////////////////////////////////////////////////////////////////////////////
// pqPOFFReaderPanel::pqImplementation

class pqPOFFReaderPanel::pqImplementation
{
public:
  pqImplementation()
  {
    this->VTKConnect = vtkSmartPointer<vtkEventQtSlotConnect>::New();
    this->RegionNameActors = vtkSmartPointer<vtkVariantArray>::New();
  }

  ~pqImplementation()
  {
  }

  pqPipelineSource *Ps;
  pqPropertyLinks Links;
  // Whenever a Qt class has a member variable that is a VTK object,
  // the member must be a smart pointer.
  // cf. http://paraview.org/ParaView3/index.php/Coding_Standards
  vtkSmartPointer<vtkEventQtSlotConnect> VTKConnect;

  QTimer *Timer;
  QPushButton *Refresh;
  QLabel *IntervalLabel;
  QToolButton *Watch;

  vtkSmartPointer<vtkVariantArray> RegionNameActors;
  vtkSmartPointer<vtkSMProxy> RegionTextCentered;
  vtkSmartPointer<vtkSMProxy> RegionTextTop;
  vtkSmartPointer<vtkSMProxy> RegionTextBottom;
  vtkSmartPointer<vtkSMProxy> RegionTextGreen;

  int CurrentTime;
};

///////////////////////////////////////////////////////////////////////////////
// pqPOFFReaderPanel

pqPOFFReaderPanel::pqPOFFReaderPanel(pqProxy *pxy, QWidget *p)
  : pqAutoGeneratedObjectPanel(pxy, p), Implementation(new pqImplementation)
{
  // should be ok with this->referenceProxy() as well but wanted to
  // avoid excess usage of qobject_cast()
  this->Implementation->Ps = qobject_cast<pqPipelineSource *>(pxy);

  // create timer
  this->Implementation->Timer = new QTimer(this);
  this->Implementation->Timer->setSingleShot(false);
  this->Implementation->Timer->setInterval(1000); // 1 sec
  QObject::connect(this->Implementation->Timer, SIGNAL(timeout()), SLOT(onTimerTimeout()));

  // create sublayout
  QGridLayout *grid = new QGridLayout;
  this->PanelLayout->addLayout(grid, 0, 0, 1, -1);
  grid->setColumnStretch(4, 1); // column 4 will be empty stretching spacing

  // create label for watching interval
  this->Implementation->IntervalLabel = new QLabel("Interval [s]", this);
  // fix width to current text width
  this->Implementation->IntervalLabel->setFixedWidth(this->Implementation->IntervalLabel->sizeHint().width());
  this->Implementation->IntervalLabel->setAlignment(Qt::AlignRight);
  grid->addWidget(this->Implementation->IntervalLabel, 0, 0, Qt::AlignLeft);

  // create rescale button
  vtkSMIntVectorProperty *uiRescale
      = vtkSMIntVectorProperty::SafeDownCast(this->proxy()->GetProperty("UiRescale"));
  uiRescale->SetImmediateUpdate(1);
  QToolButton *rescale = new QToolButton(this);
  rescale->setObjectName("UiRescale");
  rescale->setText("Rescale");
  rescale->setCheckable(true);
  // enclose with <p></p> so that the text is folded by a suitable width
  rescale->setToolTip("<p>When watching a case, whether autoscaling of scalar"
      " data is applied every time the scene is updated.</p>");
  this->Implementation->Links.addPropertyLink(rescale, "checked",
      SIGNAL(toggled(bool)), this->proxy(), uiRescale);
  grid->addWidget(rescale, 0, 2, Qt::AlignLeft);

  // create watch button
  vtkSMIntVectorProperty *uiWatch
      = vtkSMIntVectorProperty::SafeDownCast(this->proxy()->GetProperty("UiWatch"));
  uiWatch->SetImmediateUpdate(1);
  this->Implementation->Watch = new QToolButton(this);
  this->Implementation->Watch->setText("Watch");
  this->Implementation->Watch->setCheckable(true);
  // connect before link so that the slots are fired as necessary
  QObject::connect(this->Implementation->Watch, SIGNAL(toggled(bool)),
      SLOT(onWatchToggled(bool)));
  this->Implementation->Links.addPropertyLink(this->Implementation->Watch,
      "checked", SIGNAL(toggled(bool)), this->proxy(), uiWatch);
  this->Implementation->Watch->setToolTip("<p>Watch the case with the specified"
      " interval and update the scene with the latest timestep.</p>");
  grid->addWidget(this->Implementation->Watch, 0, 3, Qt::AlignLeft);

  // create line edit for changing interval. Must be created after
  // Watch toolButton has been created in order for onEditingFinished() to work
  vtkSMIntVectorProperty *uiInterval
      = vtkSMIntVectorProperty::SafeDownCast(this->proxy()->GetProperty("UiInterval"));
  uiInterval->SetImmediateUpdate(1);
  QLineEdit *intEdit = new QLineEdit(QString("%1").arg(uiInterval->GetElement(0)), this);
  this->onEditingFinished(); // manually fire the slot
  intEdit->setMaximumSize(intEdit->minimumSizeHint().width() + 30,
      intEdit->maximumSize().height());
  QIntValidator *valid = new QIntValidator(intEdit);
  valid->setBottom(1);
  intEdit->setValidator(valid);
  intEdit->setToolTip("<p>The interval for watching a case.</p>");
  this->Implementation->Links.addPropertyLink(intEdit, "text",
      SIGNAL(editingFinished()), this->proxy(), uiInterval);
  // connect after prperty link so that the interval is properly updated
  QObject::connect(intEdit, SIGNAL(editingFinished()), SLOT(onEditingFinished()));
  grid->addWidget(intEdit, 0, 1, Qt::AlignLeft);

  // create refresh button and place the button at top-right of the layout grid
  this->proxy()->GetProperty("Refresh")->SetImmediateUpdate(1);
  this->Implementation->Refresh = new QPushButton("Rescan", this);
  this->Implementation->Refresh->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  this->Implementation->Refresh->setToolTip(
      "<p>When the Apply button is not highlighted, rescan timesteps, refresh"
      " properties and reload everything. When the Apply button is highlighted,"
      " only rescan timesteps and refresh properties.</p>");
  QObject::connect(this->Implementation->Refresh, SIGNAL(clicked()), SLOT(onRefresh()));
  grid->addWidget(this->Implementation->Refresh, 0, 5, Qt::AlignRight);

  // reflect the modified state to the Refresh button label
  QObject::connect(this->Implementation->Ps, SIGNAL(modifiedStateChanged(pqServerManagerModelItem *)),
      SLOT(onModifiedStateChanged()));

  // region names

  // setup Show region names checkbox
  vtkSMIntVectorProperty *showRegionNames
      = vtkSMIntVectorProperty::SafeDownCast(
      this->proxy()->GetProperty("ShowRegionNames"));
  showRegionNames->SetImmediateUpdate(1);
  QCheckBox *regionNames = this->findChild<QCheckBox*>("ShowRegionNames");
  // unlink the default property link and setup/take care of
  // alternative link for immediate updating by ourselves.
  pqNamedWidgets::unlinkObject(regionNames, pqSMProxy(this->proxy()),
      "ShowRegionNames", this->propertyManager());
  this->Implementation->Links.addPropertyLink(regionNames, "checked",
      SIGNAL(toggled(bool)), this->proxy(), showRegionNames);
  this->Implementation->VTKConnect->Connect(showRegionNames,
      vtkCommand::ModifiedEvent, this, SLOT(onShowRegionNamesModified()));

  // initialize text properties
  // centered
#if PQ_POPENFOAMPANEL_MULTI_SERVER
  vtkSMSessionProxyManager *pxm
      = this->proxy()->GetSession()->GetSessionProxyManager();
#else
  vtkSMProxyManager *pxm = this->proxy()->GetProxyManager();
#endif
  this->Implementation->RegionTextCentered.TakeReference(
      pxm->NewProxy("properties", "TextProperty"));
#if PQ_POPENFOAMPANEL_COLLABORATION
  this->Implementation->RegionTextCentered->SetLocation(
      vtkProcessModule::CLIENT | vtkProcessModule::RENDER_SERVER);
  // there is no need to do SetSession() because session is set at the
  // proxy creation
#else
  this->Implementation->RegionTextCentered->SetServers(
      vtkProcessModule::CLIENT | vtkProcessModule::RENDER_SERVER);
  this->Implementation->RegionTextCentered->SetConnectionID(
      this->proxy()->GetConnectionID());
#endif
  vtkSMDoubleVectorProperty::SafeDownCast(
      this->Implementation->RegionTextCentered->GetProperty("Color"))
      ->SetElements3(1.0, 0.0, 1.0);
  vtkSMPropertyHelper(this->Implementation->RegionTextCentered,
      "FontSize").Set(16);
  vtkSMPropertyHelper(this->Implementation->RegionTextCentered,
      "Justification").Set(1);
  vtkSMPropertyHelper(this->Implementation->RegionTextCentered,
      "VerticalJustification").Set(VTK_TEXT_CENTERED);
  this->Implementation->RegionTextCentered->UpdateVTKObjects();
  // top-aligned
  this->Implementation->RegionTextTop.TakeReference(
      pxm->NewProxy("properties", "TextProperty"));
#if PQ_POPENFOAMPANEL_COLLABORATION
  this->Implementation->RegionTextTop->SetLocation(
      vtkProcessModule::CLIENT | vtkProcessModule::RENDER_SERVER);
#else
  this->Implementation->RegionTextTop->SetServers(
      vtkProcessModule::CLIENT | vtkProcessModule::RENDER_SERVER);
  this->Implementation->RegionTextTop->SetConnectionID(
      this->proxy()->GetConnectionID());
#endif
  this->Implementation->RegionTextTop->Copy(
      this->Implementation->RegionTextCentered);
  vtkSMPropertyHelper(this->Implementation->RegionTextTop,
      "VerticalJustification").Set(VTK_TEXT_TOP);
  this->Implementation->RegionTextTop->UpdateVTKObjects();
  // bottom-aligned
  this->Implementation->RegionTextBottom.TakeReference(
      pxm->NewProxy("properties", "TextProperty"));
#if PQ_POPENFOAMPANEL_COLLABORATION
  this->Implementation->RegionTextBottom->SetLocation(
      vtkProcessModule::CLIENT | vtkProcessModule::RENDER_SERVER);
#else
  this->Implementation->RegionTextBottom->SetServers(
      vtkProcessModule::CLIENT | vtkProcessModule::RENDER_SERVER);
  this->Implementation->RegionTextBottom->SetConnectionID(
      this->proxy()->GetConnectionID());
#endif
  this->Implementation->RegionTextBottom->Copy(
      this->Implementation->RegionTextCentered);
  vtkSMPropertyHelper(this->Implementation->RegionTextBottom,
      "VerticalJustification").Set(VTK_TEXT_BOTTOM);
  this->Implementation->RegionTextBottom->UpdateVTKObjects();
  // green
  this->Implementation->RegionTextGreen.TakeReference(
      pxm->NewProxy("properties", "TextProperty"));
#if PQ_POPENFOAMPANEL_COLLABORATION
  this->Implementation->RegionTextGreen->SetLocation(
      vtkProcessModule::CLIENT | vtkProcessModule::RENDER_SERVER);
#else
  this->Implementation->RegionTextGreen->SetServers(
      vtkProcessModule::CLIENT | vtkProcessModule::RENDER_SERVER);
  this->Implementation->RegionTextGreen->SetConnectionID(
      this->proxy()->GetConnectionID());
#endif
  this->Implementation->RegionTextGreen->Copy(
      this->Implementation->RegionTextCentered);
  vtkSMDoubleVectorProperty::SafeDownCast(this->Implementation->RegionTextGreen
      ->GetProperty("Color"))->SetElements3(0.0, 1.0, 0.0);
  this->Implementation->RegionTextGreen->UpdateVTKObjects();

  // needs this special connection because views are already removed
  // from the source proxies when the destructor of this class is
  // being executed.
  // cf. pqObjectInspectorWidget::deleteProxy(),
  // pqObjectBuilder::destroy(pqPipelineSource* source)
  pqApplicationCore *app = pqApplicationCore::instance();
  QObject::connect(app->getObjectBuilder(), SIGNAL(destroying(
      pqPipelineSource *)), SLOT(onSourceDestroying(pqPipelineSource *)));

  // do care about visibility change but don't care about
  // representationAdded/Removed signals because the texts show up
  // magically (not sure about why) when added, and don't have to be
  // removed anyway when the representation is being removed.
  QObject::connect(this->Implementation->Ps, SIGNAL(visibilityChanged(
      pqPipelineSource *, pqDataRepresentation *)),
      SLOT(onVisibilityChanged(pqPipelineSource *, pqDataRepresentation *)));

  // proxy name to follow file name changes
  this->Implementation->VTKConnect->Connect(
      this->proxy()->GetProperty("FileName"), vtkCommand::ModifiedEvent, this,
      SLOT(onFileNameModified()));
  QString smName(this->Implementation->Ps->getSMName());
  if (smName == "controlDict")
    {
    this->onFileNameModified(); // fire manually
    }

  // rearrange array selection lists
  QTreeWidget *cellArrays = this->findChild<QTreeWidget*>("CellArrays");
  QTreeWidget *surfaceArrays = this->findChild<QTreeWidget*>("SurfaceArrays");
  QTreeWidget *pointArrays = this->findChild<QTreeWidget*>("PointArrays");
  QTreeWidget *lagrangianArrays
      = this->findChild<QTreeWidget*>("LagrangianArrays");
  int cellArrayRow, dummy;
  this->PanelLayout->getItemPosition(this->PanelLayout->indexOf(cellArrays),
      &cellArrayRow, &dummy, &dummy, &dummy);
  // set minimum size to width a bit smaller than what is recommended
  // by sizeHint() so that the list fits in the original panel size
  const int minWidth = cellArrays->sizeHint().width() - 4;
  cellArrays->setMinimumSize(minWidth, 0);
  surfaceArrays->setMinimumSize(minWidth, 0);
  pointArrays->setMinimumSize(minWidth, 0);
  lagrangianArrays->setMinimumSize(minWidth, 0);
  QGridLayout *grid2 = new QGridLayout;
  // no need to do removeWidget() from PanelLayout where they
  // originally were -- they will be removed automatically
  grid2->addWidget(cellArrays, 0, 0);
  grid2->addWidget(lagrangianArrays, 0, 1);
  grid2->addWidget(surfaceArrays, 1, 0);
  grid2->addWidget(pointArrays, 1, 1);
  this->PanelLayout->addLayout(grid2, cellArrayRow, 0, 1, 2);

  // group misc options to a collapsed group
  pqCollapsedGroup *const group = new pqCollapsedGroup(this);
  group->setTitle(tr("Options"));
  // this circumlocutory layout is needed in order for collapsing to
  // work as expected
  QVBoxLayout *l1 = new QVBoxLayout(group), *l2 = new QVBoxLayout;
  l2->addWidget(this->findChild<QCheckBox*>("CacheMesh"));
  l2->addWidget(this->findChild<QCheckBox*>("DecomposePolyhedra"));
  l2->addWidget(this->findChild<QCheckBox*>("IsSinglePrecisionBinary"));
  l2->addWidget(this->findChild<QCheckBox*>("ListTimeStepsByControlDict"));
  l2->addWidget(this->findChild<QCheckBox*>("PositionsIsIn13Format"));
  l2->addWidget(this->findChild<QCheckBox*>("ReadZones"));
  l2->setMargin(0);
  l1->addLayout(l2);
  this->PanelLayout->addWidget(group, cellArrayRow + 1, 0, 1, 2);
}

//-----------------------------------------------------------------------------
pqPOFFReaderPanel::~pqPOFFReaderPanel()
{
  delete this->Implementation->Timer;
  this->Implementation->Links.removeAllPropertyLinks();

  delete this->Implementation;
}

//-----------------------------------------------------------------------------
void pqPOFFReaderPanel::onFileNameModified()
{
#if defined(_WIN32)
  const QRegExp pathFindSeparator("[/\\\\]");
  const QString pathSeparator("\\");
#else
  const QRegExp pathFindSeparator("/");
  const QString pathSeparator("/");
#endif

  QString fileName(vtkSMPropertyHelper(this->proxy(),
      "FileName").GetAsString());

  // determine the case name from path components whenever possible
  int loc[5];
  loc[0] = 0; // not used for now

  loc[1] = fileName.lastIndexOf(pathFindSeparator, -1); // -1 = loc[0] - 1
  QString lastCmpt(fileName.mid(loc[1] + 1));
  if (lastCmpt != "controlDict" && lastCmpt != "fvSchemes"
      && lastCmpt != "fvSolution")
    {
    this->Implementation->Ps->rename(lastCmpt);
    return;
    }

  loc[2] = fileName.lastIndexOf(pathFindSeparator, loc[1] - 1);
  if (loc[2] == -1)
    {
    // can't determine what to do; use lastCmpt
    this->Implementation->Ps->rename(lastCmpt);
    return;
    }

  // the case could be a multiregion case. Go up two steps
  for (int i = 3; i < 5; i++)
    {
    loc[i] = fileName.lastIndexOf(pathFindSeparator, loc[i - 1] - 1);
    if (fileName.midRef(loc[i - 1] + 1, loc[i - 2] - loc[i - 1] - 1)
        == "system")
      {
      QString cmpt(fileName.mid(loc[i] + 1, loc[i - 1] - loc[i] - 1));
      if (cmpt != "")
        {
        this->Implementation->Ps->rename(cmpt + ".foam");
        return;
        }
      break; // can't determine what to do
      }
    if (loc[i] == -1)
      {
      break; // can't determine what to do
      }
    }
  this->Implementation->Ps->rename(lastCmpt); // use lastComponent
}

//-----------------------------------------------------------------------------
void pqPOFFReaderPanel::onSourceDestroying(pqPipelineSource *source)
{
  if (source == this->Implementation->Ps)
    {
    this->removeAllRegionNameActors();
    this->deleteRegionNameActors();
    }
}

//-----------------------------------------------------------------------------
void pqPOFFReaderPanel::onVisibilityChanged(pqPipelineSource *,
    pqDataRepresentation *repr)
{
  // mechanism to ignore redundant visibilityChanged signals (fired
  // when representation is being added or has been removed) is
  // necessary, which is done by checking if repr->getViews() is null.
  if (repr->isVisible())
    {
    this->addRegionNameActors(repr->getView());
    }
  else
    {
    if (repr->getView())
      {
      // not set properties of actors invisible but remove actors from
      // the renderer since marking invisible affests other
      // representations as well
      this->removeRegionNameActors(repr->getView());
      }
    }
}

//-----------------------------------------------------------------------------
// Do actual rescaling.
void pqPOFFReaderPanel::rescalePipelineSource(pqPipelineSource *ps)
{
  // By following downstream pipeline objects after the rescaling, the
  // downstream-most scalings are used as the final scaling for each
  // Color-By attribute.

  // getRepresentations(0): get all representations of this source
  QList<pqDataRepresentation *> reprs = ps->getRepresentations(0);
  foreach (pqDataRepresentation *repr, reprs)
    {
    pqPipelineRepresentation *pipe
        = qobject_cast<pqPipelineRepresentation *>(repr);
    if (pipe)
      {
      // rescale scalar ranges
      pipe->resetLookupTableScalarRange();
      }
    }

  // rescale downstream pipeline objects as well
  QList<pqPipelineSource *> consumers = ps->getAllConsumers();
  foreach (pqPipelineSource *consumer, consumers)
    {
    this->rescalePipelineSource(consumer);
    }
}

//-----------------------------------------------------------------------------
void pqPOFFReaderPanel::onEditingFinished()
{
  // onEditingFinished is called only when the entered text is acceptable
  this->Implementation->CurrentTime
      = vtkSMPropertyHelper(this->proxy(), "UiInterval").GetAsInt();

  if (this->Implementation->Watch->isChecked())
    {
    this->Implementation->IntervalLabel
        ->setText(QString("%1 /").arg(this->Implementation->CurrentTime));
    }
}

//-----------------------------------------------------------------------------
void pqPOFFReaderPanel::onWatchToggled(const bool checked)
{
  if (checked)
    {
    this->onEditingFinished();
    this->Implementation->Timer->start();
    }
  else
    {
    this->Implementation->IntervalLabel->setText("Interval [s]");
    this->Implementation->Timer->stop();
    }
}

//-----------------------------------------------------------------------------
void pqPOFFReaderPanel::onRefresh()
{
  // When the Apply button is not highlighted (the proxy is
  // UNMODIFIED), refresh and reload everything. Otherwise (the proxy
  // is either UNINITIALIZED or MODIFIED) only refresh properties
  // (including timesteps).
  if (this->Implementation->Ps->modifiedState() == pqProxy::UNMODIFIED)
    {
    // force updating everything (RequestInformation() + RequestData())
    this->proxy()->GetProperty("Refresh")->Modified();
    vtkSMSourceProxy::SafeDownCast(this->proxy())->UpdatePipeline();
    if (vtkSMPropertyHelper(this->proxy(), "UiRescale").GetAsInt())
      {
      // rescale scalar ranges of all relevant representations and render views
      this->rescalePipelineSource(this->Implementation->Ps);
      }
    // true: force rendering now so as not to overlap the rendering
    // process and the timer countdown
    this->Implementation->Ps->renderAllViews(true);
    // If one is happy with only updating the active view
    //   if (this->view())
    //    {
    //    this->view()->render();
    //    }
    // All relevant views may be rendered this way as well
    // QList<pqView *> views = this->Implementation->Ps->getViews();
    // foreach (pqView *pqv, views)
    //   {
    //   // render the view
    //   pqv->render();
    //   }
    // All views including irrelevant ones may be rendered by
    //   pqApplicationCore::instance()->render();
    // Or there's even another way
    //   vtkSMProxyIterator *pIt = vtkSMProxyIterator::New();
    //   pIt->SetModeToOneGroup();
    //   for (pIt->Begin("views"); !pIt->IsAtEnd(); pIt->Next())
    //     {
    //     vtkSMViewProxy::SafeDownCast(pIt->GetProxy())->StillRender();
    //     }
    //   pIt->Delete();
    }
  else
    {
    // Update CaseType and ListTimeStepsByControlDict first so that
    // the timesteps for the correct case type will be scanned.

    // Everything has to be coded despite unchecked values of the
    // properties are refreshed upon GUI changes, since the unchecked
    // values are not set to the default values when initialized.
    // cf. Qt/Core/pqSMAdaptor.cxx, Qt/Components/pqNamedWidgets.cxx
    vtkSMIntVectorProperty *ctp = vtkSMIntVectorProperty::SafeDownCast(
        this->proxy()->GetProperty("CaseType"));
    vtkSMEnumerationDomain *cted
        = vtkSMEnumerationDomain::SafeDownCast(ctp->GetDomain("enum"));
    const unsigned int nEntries = cted->GetNumberOfEntries();
    const QString ctct(this->findChild<QComboBox*>("CaseType")->currentText());
    for (unsigned int entryI = 0; entryI < nEntries; entryI++)
      {
      if (ctct == cted->GetEntryText(entryI))
        {
        ctp->SetElement(0, cted->GetEntryValue(entryI));
        }
      }
    this->proxy()->UpdateProperty("CaseType");

    const bool lcc = this->findChild<QCheckBox*>("ListTimeStepsByControlDict")->isChecked();
    vtkSMPropertyHelper(this->proxy(),"ListTimeStepsByControlDict").Set(
        static_cast<int>(lcc));
    this->proxy()->UpdateProperty("ListTimeStepsByControlDict");

    // The Refresh property does not have to be marked as modified
    // since the proxy itself is already modified.

    // only refresh properties and timesteps
    // Sp->UpdatePropertyInformation() no longer seems to be required
    vtkSMSourceProxy::SafeDownCast(this->proxy())->UpdatePipelineInformation();
    }
}

//-----------------------------------------------------------------------------
// Decrement of timer
void pqPOFFReaderPanel::onTimerTimeout()
{
  --this->Implementation->CurrentTime;
  if (this->Implementation->CurrentTime > 0)
    {
    if (this->Implementation->CurrentTime < 5 || this->Implementation->CurrentTime % 5 == 0)
      {
      this->Implementation->IntervalLabel
          ->setText(QString("%1 /").arg(this->Implementation->CurrentTime));
      }
    return;
    }

  this->Implementation->Timer->stop(); // stop timer while processing
  this->Implementation->IntervalLabel->setText("Updating");
  // process the setText() event above now (IntervalLabel->repaint()
  // did not work)
  QCoreApplication::processEvents(QEventLoop::AllEvents, 1000);

  // only update pipeline information (RequestInformation())
  this->proxy()->GetProperty("Refresh")->Modified();
  vtkSMSourceProxy::SafeDownCast(this->proxy())->UpdatePipelineInformation();

  // get the last timestep
  vtkSMDoubleVectorProperty *tsv = vtkSMDoubleVectorProperty::SafeDownCast(
      this->proxy()->GetProperty("TimestepValues"));
  if (tsv->GetNumberOfElements() > 0)
    {
    const double lastStep = tsv->GetElement(tsv->GetNumberOfElements() - 1);
    pqApplicationCore *app = pqApplicationCore::instance();

    // set animation time to the last timestep
    QList<pqAnimationScene*> scenes
        = app->getServerManagerModel()->findItems<pqAnimationScene *>();
    foreach (pqAnimationScene *scene, scenes)
      {
      scene->setAnimationTime(lastStep);
      }

    if (vtkSMPropertyHelper(this->proxy(), "UiRescale").GetAsInt())
      {
      this->rescalePipelineSource(this->Implementation->Ps);
      // in this case re-renders only when rescaled
      this->Implementation->Ps->renderAllViews(true);
      }
    }

  // watching may have been disabled while updating
  if (this->Implementation->Watch->isChecked())
    {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1000);
    this->onEditingFinished();
    this->Implementation->Timer->start();
    }
}

//-----------------------------------------------------------------------------
// Change the button label according to its behavior
void pqPOFFReaderPanel::onModifiedStateChanged()
{
  if (this->Implementation->Ps->modifiedState() == pqProxy::UNMODIFIED)
    {
    this->Implementation->Refresh->setText("Refresh");
    }
  else
    {
    this->Implementation->Refresh->setText("Rescan");
    }
}

//-----------------------------------------------------------------------------
void pqPOFFReaderPanel::addRegionNameActors(pqView *pqv)
{
  if (pqv)
    {
    vtkSMRenderViewProxy *renderView
        = vtkSMRenderViewProxy::SafeDownCast(pqv->getViewProxy());
    if (renderView)
      {
      const vtkIdType nRegions
          = this->Implementation->RegionNameActors->GetNumberOfTuples();
      // Using AddPropToNonCompositedRenderer() works better when in
      // interaction than using HiddenProps (AddPropToRenderer())
      vtkClientServerStream stream;
      for (vtkIdType regionI = 0; regionI < nRegions; regionI++)
        {
#if PQ_POPENFOAMPANEL_COLLABORATION
#if PQ_POPENFOAMPANEL_NO_ADDPROP
        stream << vtkClientServerStream::Invoke
            << VTKOBJECT(renderView) << "GetNonCompositedRenderer"
            << vtkClientServerStream::End
            << vtkClientServerStream::Invoke
            << vtkClientServerStream::LastResult << "AddActor"
            << VTKOBJECT(vtkSMProxy::SafeDownCast(
            this->Implementation->RegionNameActors->GetValue(
            regionI).ToVTKObject())) << vtkClientServerStream::End;
#else
        stream << vtkClientServerStream::Invoke
            << VTKOBJECT(renderView) << "AddPropToNonCompositedRenderer"
            << VTKOBJECT(vtkSMProxy::SafeDownCast(
            this->Implementation->RegionNameActors->GetValue(
            regionI).ToVTKObject())) << vtkClientServerStream::End;
#endif
#else
        stream << vtkClientServerStream::Invoke
            << renderView->GetID() << "AddPropToNonCompositedRenderer"
            << vtkSMProxy::SafeDownCast(
            this->Implementation->RegionNameActors->GetValue(
            regionI).ToVTKObject())->GetID() << vtkClientServerStream::End;
#endif
        }
#if PQ_POPENFOAMPANEL_COLLABORATION
      // Invoke ExecuteStream() via session since
      // vtkSMProxy::ExecuteStream() is protected
      renderView->GetSession()->ExecuteStream(renderView->GetLocation(),
          stream);
#else
      vtkProcessModule::GetProcessModule()->SendStream(
          renderView->GetConnectionID(), renderView->GetServers(), stream);
#endif
      }
    }
}

//-----------------------------------------------------------------------------
void pqPOFFReaderPanel::removeRegionNameActors(pqView *pqv)
{
  vtkSMRenderViewProxy *renderView
      = vtkSMRenderViewProxy::SafeDownCast(pqv->getViewProxy());
  if (renderView)
    {
    const vtkIdType nRegions
        = this->Implementation->RegionNameActors->GetNumberOfTuples();
    vtkClientServerStream stream;
    for (vtkIdType regionI = 0; regionI < nRegions; regionI++)
      {
#if PQ_POPENFOAMPANEL_COLLABORATION
#if PQ_POPENFOAMPANEL_NO_ADDPROP
        stream << vtkClientServerStream::Invoke
            << VTKOBJECT(renderView) << "GetNonCompositedRenderer"
            << vtkClientServerStream::End
            << vtkClientServerStream::Invoke
            << vtkClientServerStream::LastResult << "RemoveActor"
            << VTKOBJECT(vtkSMProxy::SafeDownCast(
            this->Implementation->RegionNameActors->GetValue(
            regionI).ToVTKObject())) << vtkClientServerStream::End;
#else
      stream << vtkClientServerStream::Invoke
          << VTKOBJECT(renderView) << "RemovePropFromNonCompositedRenderer"
          << VTKOBJECT(vtkSMProxy::SafeDownCast(
          this->Implementation->RegionNameActors->GetValue(
          regionI).ToVTKObject())) << vtkClientServerStream::End;
#endif
#else
      stream << vtkClientServerStream::Invoke
          << renderView->GetID() << "RemovePropFromNonCompositedRenderer"
          << vtkSMProxy::SafeDownCast(
          this->Implementation->RegionNameActors->GetValue(
          regionI).ToVTKObject())->GetID() << vtkClientServerStream::End;
#endif
      }
#if PQ_POPENFOAMPANEL_COLLABORATION
    renderView->GetSession()->ExecuteStream(renderView->GetLocation(), stream);
#else
    vtkProcessModule::GetProcessModule()->SendStream(
        renderView->GetConnectionID(), renderView->GetServers(), stream);
#endif
    }
}

//-----------------------------------------------------------------------------
void pqPOFFReaderPanel::removeAllRegionNameActors()
{
  QList<pqView *> views = this->Implementation->Ps->getViews();
  foreach (pqView *pqv, views)
    {
    this->removeRegionNameActors(pqv);
    }
}

//-----------------------------------------------------------------------------
void pqPOFFReaderPanel::deleteRegionNameActors()
{
  const vtkIdType nActors
      = this->Implementation->RegionNameActors->GetNumberOfTuples();
  for (vtkIdType actorI = 0; actorI < nActors; actorI++)
    {
    this->Implementation->RegionNameActors->GetValue(actorI).ToVTKObject()
        ->Delete();
    }
  this->Implementation->RegionNameActors->Initialize();
}

//-----------------------------------------------------------------------------
void pqPOFFReaderPanel::onShowRegionNamesModified()
{
  if (vtkSMIntVectorProperty::SafeDownCast(
      this->proxy()->GetProperty("ShowRegionNames"))->GetElement(0))
    {
    QObject::connect(this->Implementation->Ps,
        SIGNAL(dataUpdated(pqPipelineSource *)), SLOT(onDataUpdated()));
    }
  else
    {
    QObject::disconnect(this->Implementation->Ps,
        SIGNAL(dataUpdated(pqPipelineSource *)), this, SLOT(onDataUpdated()));
    this->removeAllRegionNameActors();
    this->deleteRegionNameActors();
    }
  this->Implementation->Ps->renderAllViews();
}

//-----------------------------------------------------------------------------
void pqPOFFReaderPanel::onDataUpdated()
{
  vtkSMIntVectorProperty *redrawRegionNames
      = vtkSMIntVectorProperty::SafeDownCast(
      this->proxy()->GetProperty("RedrawRegionNames"));
  this->proxy()->UpdatePropertyInformation(redrawRegionNames);
  if (redrawRegionNames->GetElement(0))
    {
    this->removeAllRegionNameActors();
    this->deleteRegionNameActors();

    // update properties
    vtkSMStringVectorProperty *regionNames
        = vtkSMStringVectorProperty::SafeDownCast(
        this->proxy()->GetProperty("RegionNames"));
    this->proxy()->UpdatePropertyInformation(regionNames);
    vtkSMDoubleVectorProperty *regionCentroids
        = vtkSMDoubleVectorProperty::SafeDownCast(
        this->proxy()->GetProperty("RegionCentroids"));
    this->proxy()->UpdatePropertyInformation(regionCentroids);
    vtkSMIntVectorProperty *regionStyles
        = vtkSMIntVectorProperty::SafeDownCast(
        this->proxy()->GetProperty("RegionStyles"));
    this->proxy()->UpdatePropertyInformation(regionStyles);

    // create text actors
    const double *centroids = regionCentroids->GetElements();
    const unsigned int nRegions = regionNames->GetNumberOfElements();
    this->Implementation->RegionNameActors->SetNumberOfValues(nRegions);
#if PQ_POPENFOAMPANEL_MULTI_SERVER
    vtkSMSessionProxyManager *pxm
        = this->proxy()->GetSession()->GetSessionProxyManager();
#else
    vtkSMProxyManager *pxm = this->proxy()->GetProxyManager();
#endif
    vtkClientServerStream stream;
#if !PQ_POPENFOAMPANEL_COLLABORATION
    vtkProcessModule *pm = vtkProcessModule::GetProcessModule();
#endif
    for (unsigned int regionI = 0; regionI < nRegions; regionI++)
      {
      vtkSMProxy *npxy = pxm->NewProxy("props", "TextActor");
#if PQ_POPENFOAMPANEL_COLLABORATION
      npxy->SetLocation(
          vtkProcessModule::CLIENT | vtkProcessModule::RENDER_SERVER);
#else
      npxy->SetServers(
          vtkProcessModule::CLIENT | vtkProcessModule::RENDER_SERVER);
      npxy->SetConnectionID(this->proxy()->GetConnectionID());
#endif
      vtkSMPropertyHelper(npxy, "Text").Set(regionNames->GetElement(regionI));
      vtkSMPropertyHelper(npxy, "TextScaleMode").Set(0);
      // for the moment 0x4 stands for green color
      if (regionStyles->GetElement(regionI) & 0x4)
        {
        vtkSMPropertyHelper(npxy, "TextProperty").Set(
            this->Implementation->RegionTextGreen);
        }
      else
        {
        const int regionStyle = regionStyles->GetElement(regionI) & 0x3;
        if (regionStyle == VTK_TEXT_TOP)
          {
          vtkSMPropertyHelper(npxy, "TextProperty").Set(
              this->Implementation->RegionTextTop);
          }
        else if (regionStyle == VTK_TEXT_BOTTOM)
          {
          vtkSMPropertyHelper(npxy, "TextProperty").Set(
              this->Implementation->RegionTextBottom);
          }
        else // VTK_TEXT_CENTERED
          {
          vtkSMPropertyHelper(npxy, "TextProperty").Set(
              this->Implementation->RegionTextCentered);
          }
        }
      npxy->UpdateVTKObjects();
      const unsigned int idx = regionI * 3;
      // directly chat with the server about coordinate system and
      // position since the TextActor proxy can't handle property
      // manipulations like object->GetABC()->SetXYZ().
#if PQ_POPENFOAMPANEL_COLLABORATION
      stream << vtkClientServerStream::Invoke
          << VTKOBJECT(npxy) << "GetPositionCoordinate"
          << vtkClientServerStream::End
          << vtkClientServerStream::Invoke << vtkClientServerStream::LastResult
          << "SetCoordinateSystemToWorld" << vtkClientServerStream::End
          << vtkClientServerStream::Invoke << VTKOBJECT(npxy)
          << "GetPositionCoordinate" << vtkClientServerStream::End
          << vtkClientServerStream::Invoke << vtkClientServerStream::LastResult
          << "SetValue" << centroids[idx] << centroids[idx + 1]
          << centroids[idx + 2] << vtkClientServerStream::End;
      // stream is reset after send
      npxy->GetSession()->ExecuteStream(npxy->GetLocation(), stream);
#else
      stream << vtkClientServerStream::Invoke
          << npxy->GetID() << "GetPositionCoordinate"
          << vtkClientServerStream::End
          << vtkClientServerStream::Invoke << vtkClientServerStream::LastResult
          << "SetCoordinateSystemToWorld" << vtkClientServerStream::End
          << vtkClientServerStream::Invoke << npxy->GetID()
          << "GetPositionCoordinate" << vtkClientServerStream::End
          << vtkClientServerStream::Invoke << vtkClientServerStream::LastResult
          << "SetValue" << centroids[idx] << centroids[idx + 1]
          << centroids[idx + 2] << vtkClientServerStream::End;
      // stream is reset after send
      pm->SendStream(npxy->GetConnectionID(), npxy->GetServers(), stream);
#endif
      this->Implementation->RegionNameActors->SetValue(regionI, npxy);
      }

    // getRepresentations(0): get all representations of this source
    QList<pqDataRepresentation *> reprs
        = this->Implementation->Ps->getRepresentations(0);
    foreach (pqDataRepresentation *repr, reprs)
      {
      pqView *pqv = repr->getView();
      // SetUseCache(0) is needed for animation play [>] to work
      // properly in second run in moving mesh
      vtkSMProxy *vpxy = pqv->getProxy();
      vtkSMPropertyHelper(vpxy, "UseCache").Set(0);
      vpxy->UpdateProperty("UseCache");
      if (repr->isVisible())
        {
        this->addRegionNameActors(pqv);
        }
      }
#if PQ_POPENFOAMPANEL_COLLABORATION
    stream << vtkClientServerStream::Invoke << VTKOBJECT(this->proxy())
        << "RedrewRegionNames" << vtkClientServerStream::End;
    // stream is reset after send
    this->proxy()->GetSession()->ExecuteStream(this->proxy()->GetLocation(),
        stream);
#else
    stream << vtkClientServerStream::Invoke << this->proxy()->GetID()
        << "RedrewRegionNames" << vtkClientServerStream::End;
    // stream is reset after send
    pm->SendStream(this->proxy()->GetConnectionID(),
        this->proxy()->GetServers(), stream);
#endif

    }
}
