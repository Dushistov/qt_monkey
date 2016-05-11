#include "mainwin.hpp"

#include <QStandardItem>

static QList<QStandardItem *>
prepareRow(const QString &first, const QString &second, const QString &third)
{
    QList<QStandardItem *> rowItems;
    rowItems << new QStandardItem(first);
    rowItems << new QStandardItem(second);
    rowItems << new QStandardItem(third);
    return rowItems;
}

MainWin::MainWin()
{
    setupUi(this);
    QList<QStandardItem *> preparedRow = prepareRow("first", "second", "third");
    auto standardModel = new QStandardItemModel(this);
    QStandardItem *item = standardModel->invisibleRootItem();
    // adding a row to the invisible root item produces a root element
    item->appendRow(preparedRow);

    QList<QStandardItem *> secondRow = prepareRow("111", "222", "333");
    // adding a row to an item starts a subtree
    preparedRow.first()->appendRow(secondRow);

    treeView->setModel(standardModel);


    listView->setModel(standardModel);

    auto btn = new QPushButton("dynamic widget", this);
    verticalLayout_2->addWidget(btn);
}

void MainWin::on_actionQuit_triggered() { qApp->quit(); }
