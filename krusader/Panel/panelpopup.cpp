/*****************************************************************************
 * Copyright (C) 2003 Shie Erlich <erlich@users.sourceforge.net>             *
 * Copyright (C) 2003 Rafi Yanai <yanai@users.sourceforge.net>               *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License as published by      *
 * the Free Software Foundation; either version 2 of the License, or         *
 * (at your option) any later version.                                       *
 *                                                                           *
 * This package is distributed in the hope that it will be useful,           *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU General Public License for more details.                              *
 *                                                                           *
 * You should have received a copy of the GNU General Public License         *
 * along with this package; if not, write to the Free Software               *
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA *
 *****************************************************************************/

#include "panelpopup.h"

#include "krfiletreeview.h"
#include "krpanel.h"
#include "krview.h"
#include "krviewitem.h"
#include "panelfunc.h"
#include "viewactions.h"
#include "../kicons.h"
#include "../krmainwindow.h"
#include "../defaults.h"
#include "../Dialogs/krsqueezedtextlabel.h"
#include "../KViewer/panelviewer.h"
#include "../KViewer/diskusageviewer.h"

// QtCore
#include <QMimeDatabase>
#include <QMimeType>
// QtWidgets
#include <QAbstractItemView>
#include <QGridLayout>

#include <KConfigCore/KSharedConfig>
#include <KI18n/KLocalizedString>
#include <KIconThemes/KIconLoader>


PanelPopup::PanelPopup(QSplitter *parent, bool left, KrMainWindow *mainWindow) : QWidget(parent),
        _left(left), _hidden(true), _mainWindow(mainWindow), stack(0), viewer(0), pjob(0), splitterSizes()
{
    splitter = parent;
    QGridLayout * layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    splitterSizes << 100 << 100;

    // create the label+buttons setup
    dataLine = new KrSqueezedTextLabel(this);
    KConfigGroup lg(krConfig, "Look&Feel");
    dataLine->setFont(lg.readEntry("Filelist Font", _FilelistFont));
    // --- hack: setup colors to be the same as an inactive panel
    dataLine->setBackgroundRole(QPalette::Window);
    int sheight = QFontMetrics(dataLine->font()).height() + 4;
    dataLine->setMaximumHeight(sheight);

    btns = new QButtonGroup(this);
    btns->setExclusive(true);
    connect(btns, SIGNAL(buttonClicked(int)), this, SLOT(tabSelected(int)));

    treeBtn = new QToolButton(this);
    treeBtn->setToolTip(i18n("Tree Panel: a tree view of the local file system"));
    treeBtn->setIcon(krLoader->loadIcon("view-list-tree", KIconLoader::Toolbar, 16));
    treeBtn->setFixedSize(20, 20);
    treeBtn->setCheckable(true);
    btns->addButton(treeBtn, Tree);

    previewBtn = new QToolButton(this);
    previewBtn->setToolTip(i18n("Preview Panel: display a preview of the current file"));
    previewBtn->setIcon(krLoader->loadIcon("view-preview", KIconLoader::Toolbar, 16));
    previewBtn->setFixedSize(20, 20);
    previewBtn->setCheckable(true);
    btns->addButton(previewBtn, Preview);

    viewerBtn = new QToolButton(this);
    viewerBtn->setToolTip(i18n("View Panel: view the current file"));
    viewerBtn->setIcon(krLoader->loadIcon("zoom-original", KIconLoader::Toolbar, 16));
    viewerBtn->setFixedSize(20, 20);
    viewerBtn->setCheckable(true);
    btns->addButton(viewerBtn, View);

    duBtn = new QToolButton(this);
    duBtn->setToolTip(i18n("Disk Usage Panel: view the usage of a folder"));
    duBtn->setIcon(krLoader->loadIcon("kr_diskusage", KIconLoader::Toolbar, 16));
    duBtn->setFixedSize(20, 20);
    duBtn->setCheckable(true);
    btns->addButton(duBtn, DskUsage);

    layout->addWidget(dataLine, 0, 0);
    layout->addWidget(treeBtn, 0, 1);
    layout->addWidget(previewBtn, 0, 2);
    layout->addWidget(viewerBtn, 0, 3);
    layout->addWidget(duBtn, 0, 4);

    // create a widget stack on which to put the parts
    stack = new QStackedWidget(this);

    // create the tree part ----------
    tree = new KrFileTreeView(stack);
    tree->setAcceptDrops(true);
    tree->setDragDropMode(QTreeView::DropOnly);
    tree->setDropIndicatorShown(true);
    tree->setBriefMode(true);

    tree->setProperty("KrusaderWidgetId", QVariant(Tree));
    stack->addWidget(tree);
    tree->setDirOnlyMode(true);
    // NOTE: the F2 key press event is catched before it gets to the tree
    tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    connect(tree, SIGNAL(doubleClicked(const QModelIndex &)), this, SLOT(treeSelection()));
    connect(tree, SIGNAL(activated(const QUrl &)), this, SLOT(treeSelection()));

    // create the quickview part ------
    viewer = new KImageFilePreview(stack);
    viewer->setProperty("KrusaderWidgetId", QVariant(Preview));
    stack->addWidget(viewer);

    // create the panelview

    panelviewer = new PanelViewer(stack);
    panelviewer->setProperty("KrusaderWidgetId", QVariant(View));
    stack->addWidget(panelviewer);
    connect(panelviewer, SIGNAL(openUrlRequest(const QUrl &)), this, SLOT(handleOpenUrlRequest(const QUrl &)));

    // create the disk usage view

    diskusage = new DiskUsageViewer(stack);
    diskusage->setStatusLabel(dataLine, i18n("Disk Usage:"));
    diskusage->setProperty("KrusaderWidgetId", QVariant(DskUsage));
    stack->addWidget(diskusage);
    connect(diskusage, SIGNAL(openUrlRequest(const QUrl &)), this, SLOT(handleOpenUrlRequest(const QUrl &)));

    // -------- finish the layout (General one)
    layout->addWidget(stack, 1, 0, 1, 5);

    hide(); // for not to open the 3rd hand tool at start (selecting the last used tab)
    setCurrentPage(0);
}

PanelPopup::~PanelPopup() {}

void PanelPopup::saveSettings(KConfigGroup cfg) const
{
    if (currentPage() == Tree) {
        cfg.writeEntry("TreeBriefMode", tree->briefMode());
    }
}

void PanelPopup::restoreSettings(KConfigGroup cfg)
{
    tree->setBriefMode(cfg.readEntry("TreeBriefMode", true));
}

void PanelPopup::setCurrentPage(int id)
{
    QAbstractButton * curr = btns->button(id);
    if (curr) {
        curr->click();
    }
}

void PanelPopup::show()
{
    QWidget::show();
    if (_hidden)
        splitter->setSizes(splitterSizes);
    _hidden = false;
    tabSelected(currentPage());
}

void PanelPopup::hide()
{
    if (!_hidden)
        splitterSizes = splitter->sizes();
    QWidget::hide();
    _hidden = true;
    if (currentPage() == View) panelviewer->closeUrl();
    if (currentPage() == DskUsage) diskusage->closeUrl();
}

void PanelPopup::focusInEvent(QFocusEvent*)
{
    switch (currentPage()) {
    case Preview:
        if (!isHidden())
            viewer->setFocus();
        break;
    case View:
        if (!isHidden() && panelviewer->part() && panelviewer->part()->widget())
            panelviewer->part()->widget()->setFocus();
        break;
    case DskUsage:
        if (!isHidden() && diskusage->getWidget() && diskusage->getWidget()->currentWidget())
            diskusage->getWidget()->currentWidget()->setFocus();
        break;
    case Tree:
        if (!isHidden())
            tree->setFocus();
        break;
    }
}

void PanelPopup::handleOpenUrlRequest(const QUrl &url)
{
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForUrl(url);
    if (mime.isValid() && mime.name() == "inode/directory") ACTIVE_PANEL->func->openUrl(url);
}

void PanelPopup::tabSelected(int id)
{
    QUrl url;
    const vfile *vf = 0;
    if (ACTIVE_PANEL && ACTIVE_PANEL->func->files() && ACTIVE_PANEL->view)
        vf = ACTIVE_PANEL->func->files()->getVfile(ACTIVE_PANEL->view->getCurrentItem());
    if(vf)
        url = vf->vfile_getUrl();

    // if tab is tree, set something logical in the data line
    switch (id) {
    case Tree:
        stack->setCurrentWidget(tree);
        dataLine->setText(i18n("Tree:"));
        if (!isHidden())
            tree->setFocus();
        if (ACTIVE_PANEL)
            tree->setCurrentUrl(ACTIVE_PANEL->func->files()->currentDirectory());
        break;
    case Preview:
        stack->setCurrentWidget(viewer);
        dataLine->setText(i18n("Preview:"));
        update(vf);
        break;
    case View:
        stack->setCurrentWidget(panelviewer);
        dataLine->setText(i18n("View:"));
        update(vf);
        if (!isHidden() && panelviewer->part() && panelviewer->part()->widget())
            panelviewer->part()->widget()->setFocus();
        break;
    case DskUsage:
        stack->setCurrentWidget(diskusage);
        dataLine->setText(i18n("Disk Usage:"));
        update(vf);
        if (!isHidden() && diskusage->getWidget() && diskusage->getWidget()->currentWidget())
            diskusage->getWidget()->currentWidget()->setFocus();
        break;
    }
    if (id != View) panelviewer->closeUrl();
}

// decide which part to update, if at all
void PanelPopup::update(const vfile *vf)
{
    if (isHidden())
        return;

    QUrl url;
    if(vf)
       url = vf->vfile_getUrl();

    switch (currentPage()) {
    case Preview:
        viewer->showPreview(url);
        dataLine->setText(i18n("Preview: %1", url.fileName()));
        break;
    case View:
        if(vf && !vf->vfile_isDir() && vf->vfile_isReadable())
            panelviewer->openUrl(vf->vfile_getUrl());
        else
            panelviewer->closeUrl();
        dataLine->setText(i18n("View: %1", url.fileName()));
        break;
    case DskUsage: {
        if(vf && !vf->vfile_isDir())
            url = KIO::upUrl(url);
        dataLine->setText(i18n("Disk Usage: %1", url.fileName()));
        diskusage->openUrl(url);
    }
    break;
    case Tree:  // nothing to do
        break;
    }
}

void PanelPopup::onPanelPathChange(const QUrl &url)
{
    switch (currentPage()) {
    case Tree:
        if (url.isLocalFile()) {
            tree->setCurrentUrl(url); // synchronize panel path with tree path
        }
        break;
    }
}

// ------------------- tree

void PanelPopup::treeSelection()
{
    emit selection(tree->currentUrl());
    //emit hideMe();
}
