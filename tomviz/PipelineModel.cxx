/******************************************************************************

  This source file is part of the tomviz project.

  Copyright Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/
#include "PipelineModel.h"

#include "ActiveObjects.h"
#include "DataSource.h"
#include "Module.h"
#include "ModuleManager.h"
#include "Operator.h"
#include "OperatorResult.h"

#include <QFileInfo>

namespace tomviz {

struct PipelineModel::Item
{
  Item(DataSource* source) : tag(DATASOURCE), s(source) {}
  Item(Module* module) : tag(MODULE), m(module) {}
  Item(Operator* op) : tag(OPERATOR), o(op) {}
  Item(OperatorResult* result) : tag(RESULT), r(result) {}

  DataSource* dataSource() { return tag == DATASOURCE ? s : nullptr; }
  Module* module() { return tag == MODULE ? m : nullptr; }
  Operator* op() { return tag == OPERATOR ? o : nullptr; }
  OperatorResult* result() { return tag == RESULT ? r : nullptr; }

  enum
  {
    DATASOURCE,
    MODULE,
    OPERATOR,
    RESULT
  } tag;
  union
  {
    DataSource* s;
    Module* m;
    Operator* o;
    OperatorResult* r;
  };
};

class PipelineModel::TreeItem
{
public:
  explicit TreeItem(const PipelineModel::Item& item,
                    TreeItem* parent = nullptr);
  ~TreeItem();

  TreeItem* parent() { return m_parent; }
  TreeItem* child(int index);
  int childCount() const { return m_children.count(); }
  QList<TreeItem*>& children() { return m_children; }
  int childIndex() const;
  bool appendChild(const PipelineModel::Item& item);
  bool appendAndMoveChildren(const PipelineModel::Item& item);
  bool insertChild(int position, const PipelineModel::Item& item);
  bool removeChild(int position);
  bool moveChildren(TreeItem* newParent);

  bool remove(DataSource* source);
  bool remove(Module* module);
  bool remove(Operator* op);

  bool hasOp(Operator* op);

  /// Recursively search entire tree for given object.
  TreeItem* find(Module* module);
  TreeItem* find(Operator* op);
  TreeItem* find(OperatorResult* result);

  DataSource* dataSource() { return m_item.dataSource(); }
  Module* module() { return m_item.module(); }
  Operator* op() { return m_item.op(); }
  OperatorResult* result() { return m_item.result(); }

private:
  QList<TreeItem*> m_children;
  PipelineModel::Item m_item;
  TreeItem* m_parent;
};

PipelineModel::TreeItem::TreeItem(const PipelineModel::Item& i, TreeItem* p)
  : m_item(i), m_parent(p)
{
}

PipelineModel::TreeItem::~TreeItem()
{
  qDeleteAll(m_children);
}

PipelineModel::TreeItem* PipelineModel::TreeItem::child(int index)
{
  return m_children.value(index);
}

int PipelineModel::TreeItem::childIndex() const
{
  if (m_parent) {
    return m_parent->m_children.indexOf(const_cast<TreeItem*>(this));
  }
  return 0;
}

bool PipelineModel::TreeItem::insertChild(int pos,
                                          const PipelineModel::Item& item)
{
  if (pos < 0 || pos >= m_children.size()) {
    return false;
  }
  TreeItem* treeItem = new TreeItem(item, this);
  m_children.insert(pos, treeItem);
  return true;
}

bool PipelineModel::TreeItem::appendChild(const PipelineModel::Item& item)
{
  TreeItem* treeItem = new TreeItem(item, this);
  m_children.append(treeItem);
  return true;
}

bool PipelineModel::TreeItem::appendAndMoveChildren(
  const PipelineModel::Item& item)
{
  auto treeItem = new TreeItem(item, this);
  // This enforces our desired hierarchy, if modules then move all to the
  // operator added, otherwise if already operators move to the last operator.
  if (childCount() && child(0)->module()) {
    moveChildren(treeItem);
  } else if (childCount() && child(0)->op()) {
    child(childCount() - 1)->moveChildren(treeItem);
  }
  m_children.append(treeItem);
  return true;
}

bool PipelineModel::TreeItem::removeChild(int pos)
{
  if (pos < 0 || pos >= m_children.size()) {
    return false;
  }
  delete m_children.takeAt(pos);
  return true;
}

bool PipelineModel::TreeItem::moveChildren(TreeItem* newParent)
{
  newParent->m_children.append(m_children);
  foreach (TreeItem* item, m_children) {
    item->m_parent = newParent;
  }
  m_children.clear();
  return true;
}

bool PipelineModel::TreeItem::remove(DataSource* source)
{
  if (source != dataSource()) {
    return false;
  }

  // This item is a DataSource item. Remove all children.
  foreach (auto childItem, m_children) {
    if (childItem->op()) {
      remove(childItem->op());
    } else if (childItem->module()) {
      remove(childItem->module());
    }
  }
  if (parent()) {
    parent()->removeChild(childIndex());
    return true;
  }
  return false;
}

bool PipelineModel::TreeItem::remove(Module* module)
{
  foreach (auto childItem, m_children) {
    if (childItem->module() == module) {
      removeChild(childItem->childIndex());
      // Not sure I like this, an alternative is to make TreeItem a
      // QObject and use a signal?
      ModuleManager::instance().removeModule(module);
      return true;
    }
  }
  return false;
}

bool PipelineModel::TreeItem::remove(Operator* o)
{
  foreach (auto childItem, m_children) {
    if (childItem->op() == o) {
      // Remove results
      foreach (auto resultItem, childItem->children()) {
        childItem->removeChild(resultItem->childIndex());
      }
      removeChild(childItem->childIndex());
      return true;
    }
  }
  return false;
}

bool PipelineModel::TreeItem::hasOp(Operator* o)
{
  foreach (auto childItem, m_children) {
    if (childItem->op() == o) {
      return true;
    }
  }
  return false;
}

PipelineModel::TreeItem* PipelineModel::TreeItem::find(Module* module)
{
  if (this->module() == module) {
    return this;
  } else {
    foreach (auto childItem, m_children) {
      auto moduleItem = childItem->find(module);
      if (moduleItem) {
        return moduleItem;
      }
    }
  }
  return nullptr;
}

PipelineModel::TreeItem* PipelineModel::TreeItem::find(Operator* op)
{
  if (this->op() == op) {
    return this;
  } else {
    foreach (auto childItem, m_children) {
      auto operatorItem = childItem->find(op);
      if (operatorItem) {
        return operatorItem;
      }
    }
  }
  return nullptr;
}

PipelineModel::TreeItem* PipelineModel::TreeItem::find(OperatorResult* result)
{
  if (this->result() == result) {
    return this;
  } else {
    foreach (auto childItem, m_children) {
      auto resultItem = childItem->find(result);
      if (resultItem) {
        return resultItem;
      }
    }
  }
  return nullptr;
}

PipelineModel::PipelineModel(QObject* p) : QAbstractItemModel(p)
{
  connect(&ModuleManager::instance(), SIGNAL(dataSourceAdded(DataSource*)),
          SLOT(dataSourceAdded(DataSource*)));
  connect(&ModuleManager::instance(), SIGNAL(moduleAdded(Module*)),
          SLOT(moduleAdded(Module*)));

  connect(&ActiveObjects::instance(), SIGNAL(viewChanged(vtkSMViewProxy*)),
          SIGNAL(modelReset()));
  connect(&ModuleManager::instance(), SIGNAL(dataSourceRemoved(DataSource*)),
          SLOT(dataSourceRemoved(DataSource*)));
  connect(&ModuleManager::instance(), SIGNAL(moduleRemoved(Module*)),
          SLOT(moduleRemoved(Module*)));
}

PipelineModel::~PipelineModel()
{
}

QVariant PipelineModel::data(const QModelIndex& index, int role) const
{
  if (!index.isValid() || index.column() > 2)
    return QVariant();

  // Data source
  if (!index.parent().isValid()) {
    auto treeItem = static_cast<TreeItem*>(index.internalPointer());
    auto source = treeItem->dataSource();

    if (index.column() == 0) {
      switch (role) {
        case Qt::DecorationRole:
          return QIcon(":/pqWidgets/Icons/pqInspect22.png");
        case Qt::DisplayRole:
          return QFileInfo(source->filename()).baseName();
        case Qt::ToolTipRole:
          return source->filename();
        default:
          return QVariant();
      }
    }
  } else {
    // Module or operator
    auto treeItem = this->treeItem(index);
    auto module = treeItem->module();
    auto op = treeItem->op();
    auto result = treeItem->result();
    if (module) {
      if (index.column() == 0) {
        switch (role) {
          case Qt::DecorationRole:
            return module->icon();
          case Qt::DisplayRole:
            return module->label();
          case Qt::ToolTipRole:
            return module->label();
          default:
            return QVariant();
        }
      } else if (index.column() == 1) {
        if (role == Qt::DecorationRole) {
          if (module->visibility()) {
            return QIcon(":/pqWidgets/Icons/pqEyeball16.png");
          } else {
            return QIcon(":/pqWidgets/Icons/pqEyeballd16.png");
          }
        }
      }
    } else if (op) {
      if (index.column() == 0) {
        switch (role) {
          case Qt::DecorationRole:
            return op->icon();
          case Qt::DisplayRole:
            return op->label();
          case Qt::ToolTipRole:
            return op->label();
          default:
            return QVariant();
        }
      } else if (index.column() == 1) {
        if (role == Qt::DecorationRole) {
          return QIcon(":/QtWidgets/Icons/pqDelete32.png");
        }
      }
    } else if (result) {
      if (index.column() == 0) {
        switch (role) {
          case Qt::DecorationRole:
            return tr("Result decoration");
          case Qt::DisplayRole:
            return result->label();
          case Qt::ToolTipRole:
            return tr("Result tooltip role");
          default:
            return QVariant();
        }
      }
    }
  }
  return QVariant();
}

bool PipelineModel::setData(const QModelIndex& index, const QVariant& value,
                            int role)
{
  if (role != Qt::CheckStateRole) {
    return false;
  }

  auto treeItem = this->treeItem(index);
  if (index.column() == 1 && treeItem->module()) {
    treeItem->module()->setVisibility(value == Qt::Checked);
    emit dataChanged(index, index);
  }
  return true;
}

Qt::ItemFlags PipelineModel::flags(const QModelIndex& index) const
{
  if (!index.isValid())
    return 0;

  auto treeItem = this->treeItem(index);
  auto module = treeItem->module();
  auto view = ActiveObjects::instance().activeView();

  if (module && module->view() != view) {
    return Qt::NoItemFlags;
  }
  return QAbstractItemModel::flags(index);
}

QVariant PipelineModel::headerData(int, Qt::Orientation, int) const
{
  return QVariant();
}

QModelIndex PipelineModel::index(int row, int column,
                                 const QModelIndex& parent) const
{
  if (!parent.isValid() && row < m_treeItems.count()) {
    // Data source
    return createIndex(row, column, m_treeItems[row]);
  } else {
    // Module or operator
    auto treeItem = this->treeItem(parent);
    if (treeItem && row < treeItem->childCount()) {
      return createIndex(row, column, treeItem->child(row));
    }
  }

  return QModelIndex();
}

QModelIndex PipelineModel::parent(const QModelIndex& index) const
{
  if (!index.isValid()) {
    return QModelIndex();
  }
  auto treeItem = this->treeItem(index);
  if (!treeItem->parent()) {
    return QModelIndex();
  }
  return createIndex(treeItem->parent()->childIndex(), 0, treeItem->parent());
}

int PipelineModel::rowCount(const QModelIndex& parent) const
{
  if (!parent.isValid()) {
    return m_treeItems.count();
  } else {
    auto treeItem = this->treeItem(parent);
    return treeItem->childCount();
  }
}

int PipelineModel::columnCount(const QModelIndex&) const
{
  return 2;
}

DataSource* PipelineModel::dataSource(const QModelIndex& idx)
{
  auto treeItem = this->treeItem(idx);
  return (treeItem ? treeItem->dataSource() : nullptr);
}

Module* PipelineModel::module(const QModelIndex& idx)
{
  auto treeItem = this->treeItem(idx);
  return (treeItem ? treeItem->module() : nullptr);
}

Operator* PipelineModel::op(const QModelIndex& idx)
{
  auto treeItem = this->treeItem(idx);
  return (treeItem ? treeItem->op() : nullptr);
}

OperatorResult* PipelineModel::result(const QModelIndex& idx)
{
  if (!idx.isValid()) {
    return nullptr;
  }
  auto treeItem = this->treeItem(idx);
  return (treeItem ? treeItem->result() : nullptr);
}

QModelIndex PipelineModel::dataSourceIndex(DataSource* source)
{
  for (int i = 0; i < m_treeItems.count(); ++i) {
    if (m_treeItems[i]->dataSource() == source) {
      return createIndex(i, 0, m_treeItems[i]);
    }
  }
  return QModelIndex();
}

QModelIndex PipelineModel::moduleIndex(Module* module)
{
  foreach (auto treeItem, m_treeItems) {
    auto moduleItem = treeItem->find(module);
    if (moduleItem) {
      return createIndex(moduleItem->childIndex(), 0, moduleItem);
    }
  }
  return QModelIndex();
}

QModelIndex PipelineModel::operatorIndex(Operator* op)
{
  foreach (auto treeItem, m_treeItems) {
    auto operatorItem = treeItem->find(op);
    if (operatorItem) {
      return createIndex(operatorItem->childIndex(), 0, operatorItem);
    }
  }
  return QModelIndex();
}

QModelIndex PipelineModel::resultIndex(OperatorResult* result)
{
  foreach (auto treeItem, m_treeItems) {
    auto resultItem = treeItem->find(result);
    if (resultItem) {
      return createIndex(resultItem->childIndex(), 0, resultItem);
    }
  }
  return QModelIndex();
}

void PipelineModel::dataSourceAdded(DataSource* dataSource)
{
  auto treeItem = new PipelineModel::TreeItem(PipelineModel::Item(dataSource));
  beginInsertRows(QModelIndex(), 0, 0);
  m_treeItems.append(treeItem);
  endInsertRows();
  connect(dataSource, SIGNAL(operatorAdded(Operator*)),
          SLOT(operatorAdded(Operator*)));

  // When restoring a data source from a state file it will have its operators
  // before we can listen to the signal above. Display those operators.
  foreach (auto op, dataSource->operators()) {
    this->operatorAdded(op);
  }
}

void PipelineModel::moduleAdded(Module* module)
{
  Q_ASSERT(module);
  auto dataSource = module->dataSource();
  auto index = this->dataSourceIndex(dataSource);
  if (index.isValid()) {
    auto dataSourceItem = this->treeItem(index);
    // Modules are placed at the bottom of the list. Let's just append it
    // to the data source item.
    auto row = dataSourceItem->childCount();
    beginInsertRows(index, row, row);
    dataSourceItem->appendChild(PipelineModel::Item(module));
    endInsertRows();
  }
}

void PipelineModel::operatorAdded(Operator* op)
{
  // Operators are special, they operate on all data and are shown in the
  // visualization modules. So there are some moves necessary to show this.
  Q_ASSERT(op);
  auto dataSource = op->dataSource();
  Q_ASSERT(dataSource);
  connect(op, SIGNAL(labelModified()), this, SLOT(operatorModified()));

  auto index = this->dataSourceIndex(dataSource);
  auto dataSourceItem = this->treeItem(index);
  // Find the last operator if there is one, and insert the operator there.
  int insertionRow = 0;
  for (int j = 0; j < dataSourceItem->childCount(); ++j) {
    if (!dataSourceItem->child(j)->op()) {
      insertionRow = j;
      break;
    }
  }

  beginInsertRows(index, insertionRow, insertionRow);
  dataSourceItem->insertChild(insertionRow, PipelineModel::Item(op));
  endInsertRows();

  // Insert operator results in the operator tree item
  int numResults = op->numberOfResults();
  if (numResults) {
    auto operatorTreeItem = dataSourceItem->find(op);
    auto operatorIndex = this->operatorIndex(op);

    beginInsertRows(operatorIndex, 0, numResults);
    for (int j = 0; j < numResults; ++j) {
      OperatorResult* result = op->resultAt(j);
      operatorTreeItem->appendChild(PipelineModel::Item(result));
    }
    endInsertRows();
  }
}

void PipelineModel::operatorModified()
{
  auto op = qobject_cast<Operator*>(sender());
  Q_ASSERT(op);

  auto index = this->operatorIndex(op);
  dataChanged(index, index);
}

void PipelineModel::dataSourceRemoved(DataSource* source)
{
  auto index = this->dataSourceIndex(source);

  if (index.isValid()) {
    auto item = this->treeItem(index);
    beginRemoveRows(this->parent(index), index.row(), index.row());
    item->remove(source);
    m_treeItems.removeAll(item);
    endRemoveRows();
  }
}

void PipelineModel::moduleRemoved(Module* module)
{
  auto index = this->moduleIndex(module);

  if (index.isValid()) {
    beginRemoveRows(this->parent(index), index.row(), index.row());
    auto item = this->treeItem(index);
    item->parent()->remove(module);
    endRemoveRows();
  }
}

bool PipelineModel::removeDataSource(DataSource* source)
{
  dataSourceRemoved(source);
  ModuleManager::instance().removeDataSource(source);
  return true;
}

bool PipelineModel::removeModule(Module* module)
{
  moduleRemoved(module);
  return true;
}

bool PipelineModel::removeOp(Operator* o)
{
  auto index = this->operatorIndex(o);

  if (index.isValid()) {
    beginRemoveRows(this->parent(index), index.row(), index.row());
    auto item = this->treeItem(index);
    item->parent()->remove(o);
    endRemoveRows();
    o->dataSource()->removeOperator(o);

    return true;
  }

  return false;
}

PipelineModel::TreeItem* PipelineModel::treeItem(const QModelIndex& index) const
{
  if (!index.isValid()) {
    return nullptr;
  }

  return static_cast<PipelineModel::TreeItem*>(index.internalPointer());
}

} // tomviz namespace
