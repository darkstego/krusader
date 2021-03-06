/***************************************************************************
                            panelfunc.cpp
                         -------------------
copyright            : (C) 2000 by Shie Erlich & Rafi Yanai
e-mail               : krusader@users.sourceforge.net
web site             : http://krusader.sourceforge.net
---------------------------------------------------------------------------
Description
***************************************************************************

A

 db   dD d8888b. db    db .d8888.  .d8b.  d8888b. d88888b d8888b.
 88 ,8P' 88  `8D 88    88 88'  YP d8' `8b 88  `8D 88'     88  `8D
 88,8P   88oobY' 88    88 `8bo.   88ooo88 88   88 88ooooo 88oobY'
 88`8b   88`8b   88    88   `Y8b. 88~~~88 88   88 88~~~~~ 88`8b
 88 `88. 88 `88. 88b  d88 db   8D 88   88 88  .8D 88.     88 `88.
 YP   YD 88   YD ~Y8888P' `8888Y' YP   YP Y8888D' Y88888P 88   YD

                                                 S o u r c e    F i l e

***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include "panelfunc.h"

// QtCore
#include <QEventLoop>
#include <QList>
#include <QMimeData>
#include <QDir>
#include <QTemporaryFile>
#include <QUrl>
// QtGui
#include <QClipboard>
#include <QDrag>
// QtWidgets
#include <QApplication>
#include <QInputDialog>

#include <KArchive/KTar>
#include <KConfigCore/KDesktopFile>
#include <KCoreAddons/KJobTrackerInterface>
#include <KCoreAddons/KProcess>
#include <KCoreAddons/KShell>
#include <KCoreAddons/KUrlMimeData>
#include <KI18n/KLocalizedString>
#include <KIO/DesktopExecParser>
#include <KIO/JobUiDelegate>
#include <KIOCore/KProtocolInfo>
#include <KIOWidgets/KOpenWithDialog>
#include <KIOWidgets/KPropertiesDialog>
#include <KIOWidgets/KRun>
#include <KService/KMimeTypeTrader>
#include <KWidgetsAddons/KCursor>
#include <KWidgetsAddons/KMessageBox>
#include <KWidgetsAddons/KToggleAction>

#include "dirhistoryqueue.h"
#include "krcalcspacedialog.h"
#include "listpanel.h"
#include "krerrordisplay.h"
#include "listpanelactions.h"
#include "../krglobal.h"
#include "../krslots.h"
#include "../kractions.h"
#include "../defaults.h"
#include "../abstractpanelmanager.h"
#include "../krservices.h"
#include "../Archive/krarchandler.h"
#include "../Archive/packjob.h"
#include "../VFS/vfile.h"
#include "../VFS/virt_vfs.h"
#include "../VFS/krpermhandler.h"
#include "../VFS/krvfshandler.h"
#include "../Dialogs/packgui.h"
#include "../Dialogs/krdialogs.h"
#include "../Dialogs/krpleasewait.h"
#include "../Dialogs/krspwidgets.h"
#include "../Dialogs/checksumdlg.h"
#include "../KViewer/krviewer.h"
#include "../GUI/syncbrowsebutton.h"
#include "../MountMan/kmountman.h"
#include "../JobMan/jobman.h"

QPointer<ListPanelFunc> ListPanelFunc::copyToClipboardOrigin;

ListPanelFunc::ListPanelFunc(ListPanel *parent) : QObject(parent),
        panel(parent), vfsP(0), urlManuallyEntered(false), _refreshing(false), _ignoreVFSErrors(false)
{
    history = new DirHistoryQueue(panel);
    delayTimer.setSingleShot(true);
    connect(&delayTimer, SIGNAL(timeout()), this, SLOT(doRefresh()));
}

ListPanelFunc::~ListPanelFunc()
{
    if (vfsP) {
        vfsP->deleteLater();
    }
    delete history;
}

void ListPanelFunc::navigatorUrlChanged(const QUrl &url)
{
    if (_refreshing)
        return;

    if (!ListPanel::isNavigatorEditModeSet()) {
        panel->urlNavigator->setUrlEditable(false);
    }

    openUrl(KrServices::escapeFileUrl(url), QString(), true);
}

bool ListPanelFunc::isSyncing(const QUrl &url)
{
    if(otherFunc()->otherFunc() == this &&
       panel->otherPanel()->gui->syncBrowseButton->state() == SYNCBROWSE_CD &&
       !otherFunc()->syncURL.isEmpty() &&
       otherFunc()->syncURL == url)
        return true;

    return false;
}

void ListPanelFunc::openFileNameInternal(const QString &name, bool externallyExecutable)
{
    if (name == "..") {
        dirUp();
        return ;
    }

    vfile *vf = files()->getVfile(name);
    if (vf == 0)
        return ;

    QUrl url = files()->getUrl(name);

    if (vf->vfile_isDir()) {
        panel->view->setNameToMakeCurrent(QString());
        openUrl(url);
        return;
    }

    QString mime = vf->vfile_getMime();

    QUrl arcPath = browsableArchivePath(name);
    if (!arcPath.isEmpty()) {
        bool browseAsDirectory = !externallyExecutable
                || (KConfigGroup(krConfig, "Archives").readEntry("ArchivesAsDirectories", _ArchivesAsDirectories)
                && (KRarcHandler::arcSupported(mime) || KrServices::isoSupported(mime)));
        if (browseAsDirectory) {
            openUrl(arcPath);
            return;
        }
    }

    if (externallyExecutable) {
        if (KRun::isExecutableFile(url, mime)) {
            runCommand(KShell::quoteArg(url.path()));
            return;
        }

        KService::Ptr service = KMimeTypeTrader::self()->preferredService(mime);
        if(service) {
            runService(*service, QList<QUrl>() << url);
            return;
        }

        displayOpenWithDialog(QList<QUrl>() << url);
    }
}

#if 0
//FIXME: see if this is still needed
void ListPanelFunc::popErronousUrl()
{
    QUrl current = urlStack.last();
    while (urlStack.count() != 0) {
        QUrl url = urlStack.takeLast();
        if (!current.equals(url)) {
            immediateOpenUrl(url, true);
            return;
        }
    }
    immediateOpenUrl(QUrl::fromLocalFile(ROOT_DIR), true);
}
#endif
QUrl ListPanelFunc::cleanPath(const QUrl &urlIn)
{
    QUrl url = urlIn;
    url.setPath(QDir::cleanPath(url.path()));
    if (!url.isValid() || url.isRelative()) {
        if (url.url() == "~")
            url = QUrl::fromLocalFile(QDir::homePath());
        else if (!url.url().startsWith('/')) {
            // possible relative URL - translate to full URL
            url = files()->currentDirectory();
            url.setPath(url.path() + '/' + urlIn.path());
        }
    }
    url.setPath(QDir::cleanPath(url.path()));
    return url;
}

void ListPanelFunc::openUrl(const QUrl &url, const QString& nameToMakeCurrent,
                            bool manuallyEntered)
{
    if (panel->syncBrowseButton->state() == SYNCBROWSE_CD) {
        //do sync-browse stuff....
        if(syncURL.isEmpty())
            syncURL = panel->otherPanel()->virtualPath();

        QString relative = QDir(panel->virtualPath().path() + '/').relativeFilePath(url.path());
        syncURL.setPath(QDir::cleanPath(syncURL.path() + '/' + relative));
        panel->otherPanel()->gui->setLocked(false);
        otherFunc()->openUrlInternal(syncURL, nameToMakeCurrent, false, false, false);
    }
    openUrlInternal(url, nameToMakeCurrent, false, false, manuallyEntered);
}

void ListPanelFunc::immediateOpenUrl(const QUrl &url, bool disableLock)
{
    openUrlInternal(url, QString(), true, disableLock, false);
}

void ListPanelFunc::openUrlInternal(const QUrl &url, const QString& nameToMakeCurrent,
                                    bool immediately, bool disableLock, bool manuallyEntered)
{
    QUrl cleanUrl = cleanPath(url);

    if (!disableLock && panel->isLocked() &&
            !files()->currentDirectory().matches(cleanUrl, QUrl::StripTrailingSlash)) {
        panel->_manager->newTab(url);
        urlManuallyEntered = false;
        return;
    }

    urlManuallyEntered = manuallyEntered;

    history->add(cleanUrl, nameToMakeCurrent);

    if(immediately)
        doRefresh();
    else
        refresh();
}

void ListPanelFunc::refresh()
{
    panel->inlineRefreshCancel();
    delayTimer.start(0); // to avoid qApp->processEvents() deadlock situaltion
}

void ListPanelFunc::doRefresh()
{
    _refreshing = true;
    delayTimer.stop();

    QUrl url = history->currentUrl();

    if(!url.isValid()) {
        //FIXME go back in history here ?
        panel->slotStartUpdate(true);  // refresh the panel
        urlManuallyEntered = false;
        return ;
    }

    panel->inlineRefreshCancel();

    // if we are not refreshing to current URL
    bool isEqualUrl = files()->currentDirectory().matches(url, QUrl::StripTrailingSlash);

    if (!isEqualUrl) {
        panel->setCursor(Qt::WaitCursor);
        panel->view->clearSavedSelection();
    }

    if(panel->vfsError)
        panel->vfsError->hide();

    bool refreshFailed = false;
    while (true) {
        QUrl url = history->currentUrl();

        isEqualUrl = files()->currentDirectory().matches(url, QUrl::StripTrailingSlash);

        // may get a new vfs for this url
        vfs* vfs = KrVfsHandler::instance().getVfs(url, files());
        vfs->setParentWindow(krMainWindow);
        connect(vfs, &vfs::aboutToOpenDir, &krMtMan, &KMountMan::autoMount, Qt::DirectConnection);
        if (vfs != vfsP) {
            panel->view->setFiles(0);

            // disconnect older signals
            disconnect(vfsP, 0, panel, 0);

            vfsP->deleteLater();
            vfsP = vfs; // v != 0 so this is safe
        } else {
            if (vfsP->isRefreshing()) {
                delayTimer.start(100); /* if vfs is busy try refreshing later */
                return;
            }
        }
        // (re)connect vfs signals
        disconnect(files(), 0, panel, 0);
        connect(files(), SIGNAL(refreshDone(bool)), panel, SLOT(slotStartUpdate(bool)));
        connect(files(), &vfs::filesystemInfoChanged, panel, &ListPanel::updateFilesystemStats);
        connect(files(), SIGNAL(refreshJobStarted(KIO::Job*)),
                panel, SLOT(slotJobStarted(KIO::Job*)));
        connect(files(), SIGNAL(error(QString)),
                panel, SLOT(slotVfsError(QString)));

        panel->view->setFiles(files());

        if(!history->currentItem().isEmpty() && isEqualUrl) {
            // if the url we're refreshing into is the current one, then the
            // partial refresh will not generate the needed signals to actually allow the
            // view to use nameToMakeCurrent. do it here instead (patch by Thomas Jarosch)
            panel->view->setCurrentItem(history->currentItem());
            panel->view->makeItemVisible(panel->view->getCurrentKrViewItem());
        }
        panel->view->setNameToMakeCurrent(history->currentItem());

        int savedHistoryState = history->state();

        // NOTE: this is blocking. Returns false on error or interruption (cancel requested or panel
        // was deleted)
        const bool refreshed = vfsP->refresh(url);
        if (refreshed) {
            // update the history and address bar, as the actual url might differ from the one requested
            history->setCurrentUrl(vfsP->currentDirectory());
            panel->urlNavigator->setLocationUrl(vfsP->currentDirectory());
            break; // we have a valid refreshed URL now
        }
        if (!panel || !panel->view)
            // this panel was deleted while refreshing
            return;

        refreshFailed = true;

        panel->view->setNameToMakeCurrent(QString());

        if(history->state() != savedHistoryState) // don't go back if the history was touched
            break;
        if(!history->goBack()) {
            // put the root dir to the beginning of history, if it's not there yet
            if (!url.matches(QUrl::fromLocalFile(ROOT_DIR), QUrl::StripTrailingSlash))
                history->pushBackRoot();
            else
                break;
        }
        _ignoreVFSErrors = true;
    }
    _ignoreVFSErrors = false;
    panel->view->setNameToMakeCurrent(QString());

    panel->setCursor(Qt::ArrowCursor);

    // on local file system change the working directory
    if (files()->isLocal())
        QDir::setCurrent(KrServices::urlToLocalPath(files()->currentDirectory()));

    // see if the open url operation failed, and if so,
    // put the attempted url in the navigator bar and let the user change it
    if (refreshFailed) {
        if(isSyncing(url))
            panel->otherPanel()->gui->syncBrowseButton->setChecked(false);
        else if(urlManuallyEntered) {
            panel->urlNavigator->setLocationUrl(url);
            if(panel == ACTIVE_PANEL)
                panel->editLocation();
        }
    }

    if(otherFunc()->otherFunc() == this)  // not true if our tab is not active
        otherFunc()->syncURL = QUrl();

    urlManuallyEntered = false;

    refreshActions();
    _refreshing = false;
}

void ListPanelFunc::redirectLink()
{
    if (!files()->isLocal()) {
        KMessageBox::sorry(krMainWindow, i18n("You can edit links only on local file systems"));
        return ;
    }

    vfile *vf = files()->getVfile(panel->getCurrentName());
    if (!vf)
        return ;

    QString file = vf->vfile_getUrl().path();
    QString currentLink = vf->vfile_getSymDest();
    if (currentLink.isEmpty()) {
        KMessageBox::sorry(krMainWindow, i18n("The current file is not a link, so it cannot be redirected."));
        return ;
    }

    // ask the user for a new destination
    bool ok = false;
    QString newLink =
        QInputDialog::getText(krMainWindow, i18n("Link Redirection"), i18n("Please enter the new link destination:"), QLineEdit::Normal, currentLink, &ok);

    // if the user canceled - quit
    if (!ok || newLink == currentLink)
        return ;
    // delete the current link
    if (unlink(file.toLocal8Bit()) == -1) {
        KMessageBox::sorry(krMainWindow, i18n("Cannot remove old link: %1", file));
        return ;
    }
    // try to create a new symlink
    if (symlink(newLink.toLocal8Bit(), file.toLocal8Bit()) == -1) {
        KMessageBox:: /* --=={ Patch by Heiner <h.eichmann@gmx.de> }==-- */sorry(krMainWindow, i18n("Failed to create a new link: %1", file));
        return ;
    }
}

void ListPanelFunc::krlink(bool sym)
{
    if (!files()->isLocal()) {
        KMessageBox::sorry(krMainWindow, i18n("You can create links only on local file systems"));
        return;
    }

    QString name = panel->getCurrentName();

    // ask the new link name..
    bool ok = false;
    QString linkName =
        QInputDialog::getText(krMainWindow, i18n("New Link"),
                              i18n("Create a new link to: %1", name), QLineEdit::Normal, name, &ok);

    // if the user canceled - quit
    if (!ok || linkName == name)
        return;

    // if the name is already taken - quit
    if (files()->getVfile(linkName) != 0) {
        KMessageBox::sorry(krMainWindow, i18n("A folder or a file with this name already exists."));
        return;
    }

    // make link name and target absolute path
    if (linkName.left(1) != "/")
        linkName = files()->currentDirectory().path() + '/' + linkName;
    name = files()->getUrl(name).path();

    if (sym) {
        if (symlink(name.toLocal8Bit(), linkName.toLocal8Bit()) == -1)
            KMessageBox::sorry(krMainWindow,
                               i18n("Failed to create a new symlink '%1' to: '%2'", linkName, name));
    } else {
        if (link(name.toLocal8Bit(), linkName.toLocal8Bit()) == -1)
            KMessageBox::sorry(krMainWindow,
                               i18n("Failed to create a new link '%1' to '%2'", linkName, name));
    }
}

void ListPanelFunc::view()
{
    QString fileName = panel->getCurrentName();
    if (fileName.isNull())
        return ;

    // if we're trying to view a directory, just exit
    vfile * vf = files()->getVfile(fileName);
    if (!vf || vf->vfile_isDir())
        return ;
    if (!vf->vfile_isReadable()) {
        KMessageBox::sorry(0, i18n("No permissions to view this file."));
        return ;
    }
    // call KViewer.
    KrViewer::view(files()->getUrl(fileName));
    // nothing more to it!
}

void ListPanelFunc::viewDlg()
{
    // ask the user for a url to view
    QUrl dest = KChooseDir::getFile(i18n("Enter a URL to view:"), panel->virtualPath(), panel->virtualPath());
    if (dest.isEmpty())
        return ;   // the user canceled

    KrViewer::view(dest);   // view the file
}

void ListPanelFunc::terminal()
{
    SLOTS->runTerminal(panel->realPath());
}

void ListPanelFunc::edit()
{
    KFileItem tmp;

    if (fileToCreate.isEmpty()) {
        QString name = panel->getCurrentName();
        if (name.isNull())
            return;
        fileToCreate = files()->getUrl(name);
    }

    tmp = KFileItem(fileToCreate);

    if (tmp.isDir()) {
        KMessageBox::sorry(krMainWindow, i18n("You cannot edit a folder"));
        fileToCreate = QUrl();
        return ;
    }

    if (!tmp.isReadable()) {
        KMessageBox::sorry(0, i18n("No permissions to edit this file."));
        fileToCreate = QUrl();
        return;
    }

    KrViewer::edit(fileToCreate);
    fileToCreate = QUrl();
}

void ListPanelFunc::editNew()
{
    if(!fileToCreate.isEmpty())
        return;

    // ask the user for the filename to edit
    fileToCreate = KChooseDir::getFile(i18n("Enter the filename to edit:"), panel->virtualPath(), panel->virtualPath());
    if(fileToCreate.isEmpty())
        return ;   // the user canceled

    // if the file exists, edit it instead of creating a new one
    QFile f(fileToCreate.toLocalFile());

    if(f.exists()) {
        edit();
    } else {
        QTemporaryFile *tempFile = new QTemporaryFile;
        tempFile->open();

        KIO::CopyJob *job = KIO::copy(QUrl::fromLocalFile(tempFile->fileName()), fileToCreate);
        job->setUiDelegate(0);
        job->setDefaultPermissions(true);
        connect(job, SIGNAL(result(KJob*)), SLOT(slotFileCreated(KJob*)));
        connect(job, SIGNAL(result(KJob*)), tempFile, SLOT(deleteLater()));
    }
}

void ListPanelFunc::slotFileCreated(KJob *job)
{
    if(!job->error() || job->error() == KIO::ERR_FILE_ALREADY_EXIST) {
        KrViewer::edit(fileToCreate);

        if(KIO::upUrl(fileToCreate).matches(panel->virtualPath(), QUrl::StripTrailingSlash))
            refresh();
        else if(KIO::upUrl(fileToCreate).matches(panel->otherPanel()->virtualPath(), QUrl::StripTrailingSlash))
            otherFunc()->refresh();
    }
    else
        KMessageBox::sorry(krMainWindow, job->errorString());

    fileToCreate = QUrl();
}

void ListPanelFunc::moveFilesByQueue()
{
    moveFiles(!krJobMan->isQueueModeEnabled());
}

void ListPanelFunc::copyFilesByQueue()
{
    copyFiles(!krJobMan->isQueueModeEnabled());
}

void ListPanelFunc::copyFiles(bool reverseQueueMode, bool move)
{
    const QStringList fileNames = panel->getSelectedNames();
    if (fileNames.isEmpty())
        return ;  // safety

    QUrl destination = panel->otherPanel()->virtualPath();
    bool startPaused = false;

    KConfigGroup group(krConfig, "Advanced");

    bool showDialog = move ? group.readEntry("Confirm Move", _ConfirmMove) :
                             group.readEntry("Confirm Copy", _ConfirmCopy);
    if (showDialog) {
        QString operationText;
        if (move) {
            operationText = fileNames.count() == 1
                                ? i18n("Move %1 to:", fileNames.first())
                                : i18np("Move %1 file to:", "Move %1 files to:", fileNames.count());
        } else {
            operationText = fileNames.count() == 1
                                ? i18n("Copy %1 to:", fileNames.first())
                                : i18np("Copy %1 file to:", "Copy %1 files to:", fileNames.count());
        }

        // ask the user for the copy/move dest
        KChooseDir::ChooseResult result = KChooseDir::getCopyDir(operationText, destination, panel->virtualPath());
        destination = result.url;
        if (destination.isEmpty())
            return ; // the user canceled

        reverseQueueMode = result.reverseQueueMode;
        startPaused = result.startPaused;
    }

    const QList<QUrl> fileUrls = files()->getUrls(fileNames);

    if (move) {
        // after the delete return the cursor to the first unmarked file above the current item
        panel->prepareToDelete();
    }

    // make sure the user does not overwrite multiple files by mistake
    if (fileNames.count() > 1) {
        destination = vfs::ensureTrailingSlash(destination);
    }

    KIO::CopyJob::CopyMode mode = move ? KIO::CopyJob::Move : KIO::CopyJob::Copy;
    KrVfsHandler::instance().startCopyFiles(fileUrls, destination, mode, true, reverseQueueMode, startPaused);

    if(KConfigGroup(krConfig, "Look&Feel").readEntry("UnselectBeforeOperation", _UnselectBeforeOperation)) {
        panel->view->saveSelection();
        panel->view->unselectAll();
    }
}

// called from SLOTS to begin the renaming process
void ListPanelFunc::rename()
{
    panel->view->renameCurrentItem();
}

// called by signal itemRenamed() from the view to complete the renaming process
void ListPanelFunc::rename(const QString &oldname, const QString &newname)
{
    if (oldname == newname)
        return ; // do nothing

    // set current after rename
    panel->view->setNameToMakeCurrent(newname);

    // as always - the vfs do the job
    files()->rename(oldname, newname);
}

void ListPanelFunc::mkdir()
{
    // ask the new dir name..
    // suggested name is the complete name for the directories
    // while filenames are suggested without their extension
    QString suggestedName = panel->getCurrentName();
    if (!suggestedName.isEmpty() && !files()->getVfile(suggestedName)->vfile_isDir())
        suggestedName = QFileInfo(suggestedName).completeBaseName();

    QString dirName = QInputDialog::getText(krMainWindow, i18n("New folder"), i18n("Folder's name:"), QLineEdit::Normal, suggestedName);

    // if the user canceled - quit
    if (dirName.isEmpty())
        return ;

    QStringList dirTree = dirName.split('/');

    for (QStringList::Iterator it = dirTree.begin(); it != dirTree.end(); ++it) {
        if (*it == ".")
            continue;
        if (*it == "..") {
            immediateOpenUrl(QUrl::fromUserInput(*it, QString(), QUrl::AssumeLocalFile));
            continue;
        }
        // check if the name is already taken
        if (files()->getVfile(*it)) {
            // if it is the last dir to be created - quit
            if (*it == dirTree.last()) {
                KMessageBox::sorry(krMainWindow, i18n("A folder or a file with this name already exists."));
                return ;
            }
            // else go into this dir
            else {
                immediateOpenUrl(QUrl::fromUserInput(*it, QString(), QUrl::AssumeLocalFile));
                continue;
            }
        }

        panel->view->setNameToMakeCurrent(*it);
        // as always - the vfs does the job
        files()->mkDir(*it);
        if (dirTree.count() > 1)
            immediateOpenUrl(QUrl::fromUserInput(*it, QString(), QUrl::AssumeLocalFile));
    } // for
}

void ListPanelFunc::deleteFiles(bool reallyDelete)
{
    if (files()->type() == vfs::VFS_VIRT && files()->isRoot()) {
        // only virtual deletion possible
        removeVirtualFiles();
        return;
    }

    // first get the selected file names list
    QStringList fileNames = panel->getSelectedNames();
    if (fileNames.isEmpty())
        return;

    const KConfigGroup generalGroup(krConfig, "General");
    bool moveToTrash = !reallyDelete && generalGroup.readEntry("Move To Trash", _MoveToTrash);
    // make sure this is possible
    moveToTrash = moveToTrash && files()->canMoveToTrash(fileNames);

    // now ask the user if he/she is sure:
    const KConfigGroup advancedGroup(krConfig, "Advanced");
    if (advancedGroup.readEntry("Confirm Delete", _ConfirmDelete)) {
        QString s; // text
        KGuiItem b; // continue button

        if (moveToTrash) {
            s = i18np("Do you really want to move this item to the trash?",
                      "Do you really want to move these %1 items to the trash?", fileNames.count());
            b = KGuiItem(i18n("&Trash"));
        } else if (files()->type() == vfs::VFS_VIRT) {
            s = i18np("<qt>Do you really want to delete this item <b>physically</b> (not just "
                      "removing it from the virtual items)?</qt>",
                      "<qt>Do you really want to delete these %1 items <b>physically</b> (not just "
                      "removing them from the virtual items)?</qt>",
                      fileNames.count());
            b = KStandardGuiItem::del();
        } else {
            s = i18np("Do you really want to delete this item?",
                      "Do you really want to delete these %1 items?", fileNames.count());
            b = KStandardGuiItem::del();
        }

        // show message
        // note: i'm using continue and not yes/no because the yes/no has cancel as default button
        if (KMessageBox::warningContinueCancelList(krMainWindow, s, fileNames, i18n("Warning"),
                                                   b) != KMessageBox::Continue)
            return;
    }

    // we want to warn the user about non empty dir
    bool emptyDirVerify = advancedGroup.readEntry("Confirm Unempty Dir", _ConfirmUnemptyDir);
    // TODO only local fs supported
    emptyDirVerify &= files()->isLocal();

    if (emptyDirVerify) {
        for (const QString fileName: fileNames) {
            vfile *vfile = files()->getVfile(fileName);
            if (vfile && !vfile->vfile_isSymLink() && vfile->vfile_isDir()) {
                // read local dir...
                const QDir dir(vfile->vfile_getUrl().path());
                if (dir.entryList(QDir::TypeMask | QDir::System | QDir::Hidden).count() > 2) {
                    // ...is not empty, ask user
                    const KMessageBox::ButtonCode result = KMessageBox::warningYesNoCancel(
                        krMainWindow,
                        i18n("<qt><p>Folder <b>%1</b> is not empty.</p><p>Skip this one "
                             "or delete all?</p></qt>",
                             fileName),
                        QString(), KGuiItem(i18n("&Skip")), KGuiItem(i18n("&Delete All")));
                    if (result == KMessageBox::Yes) {
                        fileNames.removeAll(fileName); // skip
                    } else if (result == KMessageBox::No) {
                        break; // accept all remaining
                    } else {
                        return; // cancel
                    }
                }
            }
        }
    }

    if (fileNames.count() == 0)
        return; // nothing to delete

    // after the delete return the cursor to the first unmarked
    // file above the current item;
    panel->prepareToDelete();

    // let the vfs do the job...
    files()->deleteFiles(fileNames, moveToTrash);
}

void ListPanelFunc::removeVirtualFiles()
{
    if (files()->type() != vfs::VFS_VIRT) {
        krOut << "filesystem not virtual";
        return;
    }

    const QStringList fileNames = panel->getSelectedNames();
    if (fileNames.isEmpty())
        return;

    const QString text =
        i18np("Do you really want to delete this virtual item (physical files stay untouched)?",
              "Do you really want to delete these %1 virtual items (physical files stay "
              "untouched)?",
              fileNames.count());
    if (KMessageBox::warningContinueCancelList(krMainWindow, text, fileNames, i18n("Warning"),
                                               KStandardGuiItem::remove()) != KMessageBox::Continue)
        return;

    virt_vfs *vfs = static_cast<virt_vfs*>(files());
    vfs->remove(fileNames);
}

void ListPanelFunc::goInside(const QString& name)
{
    openFileNameInternal(name, false);
}

void ListPanelFunc::runCommand(QString cmd)
{
    krOut << "Run command: " << cmd;
    QString workdir = panel->virtualPath().isLocalFile() ?
            panel->virtualPath().path() : QDir::homePath();
    if(!KRun::runCommand(cmd, krMainWindow, workdir))
        KMessageBox::error(0, i18n("Could not start %1", cmd));
}

void ListPanelFunc::runService(const KService &service, QList<QUrl> urls)
{
    krOut << "Run service: " << service.name();
    KIO::DesktopExecParser parser(service, urls);
    QStringList args = parser.resultingArguments();
    if (!args.isEmpty())
        runCommand(KShell::joinArgs(args));
    else
        KMessageBox::error(0, i18n("%1 cannot open %2", service.name(), KrServices::toStringList(urls).join(", ")));
}

void ListPanelFunc::displayOpenWithDialog(QList<QUrl> urls)
{
    KRun::displayOpenWithDialog(urls, krMainWindow);
}

QUrl ListPanelFunc::browsableArchivePath(const QString &filename)
{
    vfile *vf = files()->getVfile(filename);
    QUrl url = files()->getUrl(filename);
    QString mime = vf->vfile_getMime();

    if(url.isLocalFile()) {
        QString protocol = KrServices::registeredProtocol(mime);
        if(!protocol.isEmpty()) {
            url.setScheme(protocol);
            return url;
        }
    }
    return QUrl();
}

// this is done when you double click on a file
void ListPanelFunc::execute(const QString& name)
{
    openFileNameInternal(name, true);
}

void ListPanelFunc::pack()
{
    const QStringList fileNames = panel->getSelectedNames();
    if (fileNames.isEmpty())
        return ;  // safety

    if (fileNames.count() == 0)
        return ; // nothing to pack

    // choose the default name
    QString defaultName = panel->virtualPath().fileName();
    if (defaultName.isEmpty())
        defaultName = "pack";
    if (fileNames.count() == 1)
        defaultName = fileNames.first();
    // ask the user for archive name and packer
    new PackGUI(defaultName, panel->otherPanel()->virtualPath().toDisplayString(QUrl::PreferLocalFile | QUrl::StripTrailingSlash),
                fileNames.count(), fileNames.first());
    if (PackGUI::type.isEmpty()) {
        return ; // the user canceled
    }

    // check for partial URLs
    if (!PackGUI::destination.contains(":/") && !PackGUI::destination.startsWith('/')) {
        PackGUI::destination = panel->virtualPath().toDisplayString() + '/' + PackGUI::destination;
    }

    QString destDir = PackGUI::destination;
    if (!destDir.endsWith('/'))
        destDir += '/';

    bool packToOtherPanel = (destDir == vfs::ensureTrailingSlash(panel->otherPanel()->virtualPath()).toDisplayString(QUrl::PreferLocalFile));

    QUrl destURL = QUrl::fromUserInput(destDir + PackGUI::filename + '.' + PackGUI::type, QString(), QUrl::AssumeLocalFile);
    if (destURL.isLocalFile() && QFile::exists(destURL.path())) {
        QString msg = i18n("<qt><p>The archive <b>%1.%2</b> already exists. Do you want to overwrite it?</p><p>All data in the previous archive will be lost.</p></qt>", PackGUI::filename, PackGUI::type);
        if (PackGUI::type == "zip") {
            msg = i18n("<qt><p>The archive <b>%1.%2</b> already exists. Do you want to overwrite it?</p><p>Zip will replace identically named entries in the zip archive or add entries for new names.</p></qt>", PackGUI::filename, PackGUI::type);
        }
        if (KMessageBox::warningContinueCancel(krMainWindow, msg, QString(), KStandardGuiItem::overwrite())
                == KMessageBox::Cancel)
            return ; // stop operation
    } else if (destURL.scheme() == QStringLiteral("virt")) {
        KMessageBox::error(krMainWindow, i18n("Cannot pack files onto a virtual destination."));
        return;
    }

    PackJob * job = PackJob::createPacker(files()->currentDirectory(), destURL, fileNames, PackGUI::type, PackGUI::extraProps);
    job->setUiDelegate(new KIO::JobUiDelegate());
    KIO::getJobTracker()->registerJob(job);
    job->ui()->setAutoErrorHandlingEnabled(true);

    if (packToOtherPanel)
        connect(job, SIGNAL(result(KJob*)), panel->otherPanel()->func, SLOT(refresh()));

}

void ListPanelFunc::testArchive()
{
    const QStringList fileNames = panel->getSelectedNames();
    if (fileNames.isEmpty())
        return ;  // safety

    TestArchiveJob * job = TestArchiveJob::testArchives(files()->currentDirectory(), fileNames);
    job->setUiDelegate(new KIO::JobUiDelegate());
    KIO::getJobTracker()->registerJob(job);
    job->ui()->setAutoErrorHandlingEnabled(true);
}

void ListPanelFunc::unpack()
{
    const QStringList fileNames = panel->getSelectedNames();
    if (fileNames.isEmpty())
        return ;  // safety

    QString s;
    if (fileNames.count() == 1)
        s = i18n("Unpack %1 to:", fileNames[0]);
    else
        s = i18np("Unpack %1 file to:", "Unpack %1 files to:", fileNames.count());

    // ask the user for the copy dest
    QUrl dest = KChooseDir::getDir(s, panel->otherPanel()->virtualPath(), panel->virtualPath());
    if (dest.isEmpty()) return ;   // the user canceled

    bool packToOtherPanel = (dest.matches(panel->otherPanel()->virtualPath(), QUrl::StripTrailingSlash));

    UnpackJob * job = UnpackJob::createUnpacker(files()->currentDirectory(), dest, fileNames);
    job->setUiDelegate(new KIO::JobUiDelegate());
    KIO::getJobTracker()->registerJob(job);
    job->ui()->setAutoErrorHandlingEnabled(true);

    if (packToOtherPanel)
        connect(job, SIGNAL(result(KJob*)), panel->otherPanel()->func, SLOT(refresh()));

}

// a small ugly function, used to prevent duplication of EVERY line of
// code (maybe except 3) from createChecksum and matchChecksum
static void checksum_wrapper(ListPanel *panel, QStringList& args, bool &folders)
{
    KrViewItemList items;
    panel->view->getSelectedKrViewItems(&items);
    if (items.isEmpty()) return ;   // nothing to do
    // determine if we need recursive mode (md5deep)
    folders = false;
    for (KrViewItemList::Iterator it = items.begin(); it != items.end(); ++it) {
        if (panel->func->getVFile(*it)->vfile_isDir()) {
            folders = true;
            args << (*it)->name();
        } else args << (*it)->name();
    }
}

void ListPanelFunc::createChecksum()
{
    QStringList args;
    bool folders;
    checksum_wrapper(panel, args, folders);
    CreateChecksumDlg dlg(args, folders, panel->realPath());
}

void ListPanelFunc::matchChecksum()
{
    QStringList args;
    bool folders;
    checksum_wrapper(panel, args, folders);
    QList<vfile *> checksumFiles =
        files()->searchVfiles(KRQuery(MatchChecksumDlg::checksumTypesFilter));
    MatchChecksumDlg dlg(args, folders, panel->realPath(),
        (checksumFiles.size() == 1
             ? checksumFiles[0]->vfile_getUrl().toDisplayString(QUrl::PreferLocalFile)
             : QString()));
}

void ListPanelFunc::calcSpace(KrViewItem *item)
{
    QStringList items;
    if (item) {
        items << item->name();
    } else {
        panel->view->getSelectedItems(&items);
        if (items.isEmpty()) {
            panel->view->selectAllIncludingDirs();
            panel->view->getSelectedItems(&items);
            if (items.isEmpty())
                return ; // nothing to do
        }
    }
    QPointer<KrCalcSpaceDialog> calc = new KrCalcSpaceDialog(krMainWindow, panel, items, item != 0);
    calc->exec();
    panel->slotUpdateTotals();

    delete calc;
}

void ListPanelFunc::FTPDisconnect()
{
    // you can disconnect only if connected!
    if (files()->isRemote()) {
        panel->_actions->actFTPDisconnect->setEnabled(false);
        panel->view->setNameToMakeCurrent(QString());
        openUrl(QUrl::fromLocalFile(panel->realPath())); // open the last local URL
    }
}

void ListPanelFunc::newFTPconnection()
{
    QUrl url = KRSpWidgets::newFTP();
    // if the user canceled - quit
    if (url.isEmpty())
        return ;

    panel->_actions->actFTPDisconnect->setEnabled(true);
    openUrl(url);
}

void ListPanelFunc::properties()
{
    const QStringList names = panel->getSelectedNames();
    if (names.isEmpty())
        return ;  // no names...
    KFileItemList fi;

    for (int i = 0 ; i < names.count() ; ++i) {
        vfile* vf = files()->getVfile(names[i]);
        if (!vf)
            continue;
        QUrl url = files()->getUrl(names[i]);
        fi.push_back(KFileItem(vf->vfile_getEntry(), url));
    }

    if (fi.isEmpty())
        return ;

    // Show the properties dialog
    KPropertiesDialog *dlg = new KPropertiesDialog(fi, krMainWindow);
    connect(dlg, SIGNAL(applied()), SLOT(refresh()));
    dlg->show();
}

void ListPanelFunc::refreshActions()
{
    panel->updateButtons();

    if(ACTIVE_PANEL != panel)
        return;

    QString protocol = files()->currentDirectory().scheme();
    krRemoteEncoding->setEnabled(protocol == "ftp" || protocol == "sftp" || protocol == "fish" || protocol == "krarc");
    //krMultiRename->setEnabled( vfsType == vfs::VFS_NORMAL );  // batch rename
    //krProperties ->setEnabled( vfsType == vfs::VFS_NORMAL || vfsType == vfs::VFS_FTP ); // file properties

    /*
      krUnpack->setEnabled(true);                            // unpack archive
      krTest->setEnabled(true);                              // test archive
      krSelect->setEnabled(true);                            // select a group by filter
      krSelectAll->setEnabled(true);                         // select all files
      krUnselect->setEnabled(true);                          // unselect by filter
      krUnselectAll->setEnabled( true);                      // remove all selections
      krInvert->setEnabled(true);                            // invert the selection
      krFTPConnect->setEnabled(true);                        // connect to an ftp
      krFTPNew->setEnabled(true);                            // create a new connection
      krAllFiles->setEnabled(true);                          // show all files in list
      krCustomFiles->setEnabled(true);                       // show a custom set of files
      krRoot->setEnabled(true);                              // go all the way up
          krExecFiles->setEnabled(true);                         // show only executables
    */

    panel->_actions->setViewActions[panel->panelType]->setChecked(true);
    panel->_actions->actFTPDisconnect->setEnabled(files()->isRemote()); // allow disconnecting a network session
    panel->_actions->actCreateChecksum->setEnabled(files()->isLocal());
    panel->_actions->actDirUp->setEnabled(!files()->isRoot());
    panel->_actions->actRoot->setEnabled(!panel->virtualPath().matches(QUrl::fromLocalFile(ROOT_DIR),
                                                                       QUrl::StripTrailingSlash));
    panel->_actions->actHome->setEnabled(!atHome());
    panel->_actions->actHistoryBackward->setEnabled(history->canGoBack());
    panel->_actions->actHistoryForward->setEnabled(history->canGoForward());
    panel->view->op()->emitRefreshActions();
}

vfs* ListPanelFunc::files()
{
    if (!vfsP)
        vfsP = KrVfsHandler::instance().getVfs(QUrl::fromLocalFile("/"));
    return vfsP;
}

void ListPanelFunc::clipboardChanged(QClipboard::Mode mode)
{
    if (mode == QClipboard::Clipboard && this == copyToClipboardOrigin) {
        disconnect(QApplication::clipboard(), 0, this, 0);
        copyToClipboardOrigin = 0;
    }
}

void ListPanelFunc::copyToClipboard(bool move)
{
    const QStringList fileNames = panel->getSelectedNames();
    if (fileNames.isEmpty())
        return ;  // safety

    QList<QUrl> fileUrls = files()->getUrls(fileNames);
    QMimeData *mimeData = new QMimeData;
    mimeData->setData("application/x-kde-cutselection", move ? "1" : "0");
    mimeData->setUrls(fileUrls);

    if (copyToClipboardOrigin)
        disconnect(QApplication::clipboard(), 0, copyToClipboardOrigin, 0);
    copyToClipboardOrigin = this;

    QApplication::clipboard()->setMimeData(mimeData, QClipboard::Clipboard);

    connect(QApplication::clipboard(), SIGNAL(changed(QClipboard::Mode)), this, SLOT(clipboardChanged(QClipboard::Mode)));
}

void ListPanelFunc::pasteFromClipboard()
{
    QClipboard * cb = QApplication::clipboard();

    ListPanelFunc *origin = 0;

    if (copyToClipboardOrigin) {
        disconnect(QApplication::clipboard(), 0, copyToClipboardOrigin, 0);
        origin = copyToClipboardOrigin;
        copyToClipboardOrigin = 0;
    }

    bool move = false;
    const QMimeData *data = cb->mimeData();
    if (data->hasFormat("application/x-kde-cutselection")) {
        QByteArray a = data->data("application/x-kde-cutselection");
        if (!a.isEmpty())
            move = (a.at(0) == '1'); // true if 1
    }

    QList<QUrl> urls = data->urls();
    if (urls.isEmpty())
        return ;

    if(origin && KConfigGroup(krConfig, "Look&Feel").readEntry("UnselectBeforeOperation", _UnselectBeforeOperation)) {
        origin->panel->view->saveSelection();
        for(KrViewItem *item = origin->panel->view->getFirst(); item != 0; item = origin->panel->view->getNext(item)) {
            if (urls.contains(item->getVfile()->vfile_getUrl()))
                item->setSelected(false);
        }
    }

    files()->addFiles(urls, move ? KIO::CopyJob::Move : KIO::CopyJob::Copy);
}

ListPanelFunc* ListPanelFunc::otherFunc()
{
    return panel->otherPanel()->func;
}

void ListPanelFunc::historyGotoPos(int pos)
{
    if(history->gotoPos(pos))
        refresh();
}

void ListPanelFunc::historyBackward()
{
    if(history->goBack())
        refresh();
}

void ListPanelFunc::historyForward()
{
    if(history->goForward())
        refresh();
}

void ListPanelFunc::dirUp()
{
    openUrl(KIO::upUrl(files()->currentDirectory()), files()->currentDirectory().fileName());
}

void ListPanelFunc::home()
{
    openUrl(QUrl::fromLocalFile(QDir::homePath()));
}

void ListPanelFunc::root()
{
    openUrl(QUrl::fromLocalFile(ROOT_DIR));
}

void ListPanelFunc::cdToOtherPanel()
{
    openUrl(panel->otherPanel()->virtualPath());
}

void ListPanelFunc::syncOtherPanel()
{
    otherFunc()->openUrl(panel->virtualPath());
}

bool ListPanelFunc::atHome()
{
    return QUrl::fromLocalFile(QDir::homePath()).matches(panel->virtualPath(), QUrl::StripTrailingSlash);
}

