/*****************************************************************************
 * Copyright (C) 2016 Krusader Krew                                          *
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

#include "jobman.h"

// QtCore
#include <QUrl>
// QtWidgets
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <KConfigCore/KSharedConfig>
#include <KI18n/KLocalizedString>
#include <KIOWidgets/KIO/FileUndoManager>

#include "krjob.h"
#include "../krglobal.h"

/** The menu action entry for a job in the popup menu.*/
class JobMenuAction : public QWidgetAction
{
    Q_OBJECT

public:
    JobMenuAction(KrJob *krJob, QObject *parent) : QWidgetAction(parent), _krJob(krJob)
    {
        QWidget *container = new QWidget();
        QGridLayout *layout = new QGridLayout(container);
        _description = new QLabel(krJob->description());
        _progressBar = new QProgressBar();
        layout->addWidget(_description, 0, 0, 1, 3);
        layout->addWidget(_progressBar, 1, 0);

        _pauseResumeButton = new QPushButton();
        updatePauseResumeButton();
        connect(_pauseResumeButton, &QPushButton::clicked, this,
              &JobMenuAction::slotPauseResumeButtonClicked);
        layout->addWidget(_pauseResumeButton, 1, 1);

        _cancelButton = new QPushButton();
        _cancelButton->setIcon(QIcon::fromTheme("remove"));
        _cancelButton->setToolTip(i18n("Cancel Job"));
        connect(_cancelButton, &QPushButton::clicked,
                this, &JobMenuAction::slotCancelButtonClicked);
        layout->addWidget(_cancelButton, 1, 2);

        setDefaultWidget(container);

        connect(krJob, &KrJob::started, this, [=](KJob *job) {
            connect(job, &KJob::description, this, &JobMenuAction::slotDescription);
            connect(job, SIGNAL(percent(KJob *, unsigned long)), this,
                    SLOT(slotPercent(KJob *, unsigned long)));
            connect(job, &KJob::suspended, this, &JobMenuAction::updatePauseResumeButton);
            connect(job, &KJob::resumed, this, &JobMenuAction::updatePauseResumeButton);
            connect(job, &KJob::result, this, &JobMenuAction::slotResult);
            connect(job, &KJob::warning, this, [](KJob *, const QString &plain, const QString &) {
                krOut << "unexpected job warning: " << plain;
            });

            updatePauseResumeButton();
        });
        connect(krJob, &KrJob::terminated, this, &JobMenuAction::slotTerminated);
    }

protected slots:
    void slotDescription(KJob *, const QString &description,
                       const QPair<QString, QString> &field1,
                       const QPair<QString, QString> &field2)
    {
        const QPair<QString, QString> textField = !field2.first.isEmpty() ? field2 : field1;
        QString text = description;
        if (!textField.first.isEmpty()) {
            text += QString(" - %1: %2").arg(textField.first, textField.second);
        }
        _description->setText(text);

        if (!field2.first.isEmpty() && !field1.first.isEmpty()) {
            // NOTE: tooltips for QAction items in menu are not shown
            _progressBar->setToolTip(QString("%1: %2").arg(field1.first, field1.second));
        }
    }

    void slotPercent(KJob *, unsigned long percent) { _progressBar->setValue(percent); }

    void updatePauseResumeButton()
    {
        _pauseResumeButton->setIcon(QIcon::fromTheme(
            _krJob->isRunning() ? "media-playback-pause" :
            _krJob->isManuallyPaused() ? "media-playback-start" : "chronometer-start"));
        _pauseResumeButton->setToolTip(_krJob->isRunning() ? i18n("Pause Job") :
                                       _krJob->isManuallyPaused() ? i18n("Resume Job") :
                                                                    i18n("Start Job"));
    }

    void slotResult(KJob *job)
    {
        // NOTE: _job may already set to 0 now
        if(!job->error()) {
            // percent signal is not reliable, set manually
            _progressBar->setValue(100);
        }
    }

    void slotTerminated()
    {
        _pauseResumeButton->setEnabled(false);
        _cancelButton->setIcon(QIcon::fromTheme("edit-clear"));
        _cancelButton->setToolTip(i18n("Clear"));

        _krJob = 0;
    }

    void slotPauseResumeButtonClicked()
    {
        if (!_krJob)
          return;

        if (_krJob->isRunning())
            _krJob->pause();
        else
            _krJob->start();
    }

    void slotCancelButtonClicked()
    {
        if (_krJob) {
          _krJob->cancel();
        } else {
          deleteLater();
        }
    }

private:
    KrJob *_krJob;

    QLabel *_description;
    QProgressBar *_progressBar;
    QPushButton *_pauseResumeButton;
    QPushButton *_cancelButton;
};

#include "jobman.moc" // required for class definitions with Q_OBJECT macro in implementation files


const QString JobMan::sDefaultToolTip = i18n("No jobs");

JobMan::JobMan(QObject *parent) : QObject(parent), _messageBox(0)
{
    // job control action
    _controlAction = new KToolBarPopupAction(QIcon::fromTheme("media-playback-pause"),
                                          i18n("Play/Pause &Job"), this);
    _controlAction->setEnabled(false);
    connect(_controlAction, &QAction::triggered, this, &JobMan::slotControlActionTriggered);

    QMenu *menu = new QMenu(krMainWindow);
    menu->setMinimumWidth(300);
    // make scrollable if menu is too long
    menu->setStyleSheet("QMenu { menu-scrollable: 1; }");
    _controlAction->setMenu(menu);

    // progress bar action
    _progressBar = new QProgressBar();
    _progressBar->setToolTip(sDefaultToolTip);
    _progressBar->setEnabled(false);
    // listen to clicks on progress bar
    _progressBar->installEventFilter(this);

    QWidgetAction *progressAction = new QWidgetAction(krMainWindow);
    progressAction->setText(i18n("Job Progress Bar"));
    progressAction->setDefaultWidget(_progressBar);
    _progressAction = progressAction;

    // job queue mode action
    KConfigGroup cfg(krConfig, "JobManager");
    _queueMode = cfg.readEntry("Queue Mode", false);
    _modeAction = new QAction(QIcon::fromTheme("media-playlist-repeat"), i18n("Job Queue Mode"),
                              krMainWindow);
    _modeAction->setToolTip(i18n("Run only one job in parallel"));
    _modeAction->setCheckable(true);
    _modeAction->setChecked(_queueMode);
    connect(_modeAction, &QAction::toggled, [=](bool checked) mutable {
        _queueMode = checked;
        cfg.writeEntry("Queue Mode", _queueMode);
    });

    // undo action
    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    undoManager->uiInterface()->setParentWidget(krMainWindow);

    _undoAction = new QAction(QIcon::fromTheme("edit-undo"), i18n("Undo Last Job"), krMainWindow);
    _undoAction->setEnabled(false);
    connect(_undoAction, &QAction::triggered, undoManager, &KIO::FileUndoManager::undo);
    connect(undoManager, static_cast<void(KIO::FileUndoManager::*)(bool)>(&KIO::FileUndoManager::undoAvailable),
            _undoAction, &QAction::setEnabled);
    connect(undoManager, &KIO::FileUndoManager::undoTextChanged, this, &JobMan::slotUndoTextChange);
}

bool JobMan::waitForJobs(bool waitForUserInput)
{
    if (_jobs.isEmpty() && !waitForUserInput)
        return true;

    // attempt to get all job threads does not work
    //QList<QThread *> threads = krMainWindow->findChildren<QThread *>();

    _autoCloseMessageBox = !waitForUserInput;

    _messageBox = new QMessageBox(krMainWindow);
    _messageBox->setWindowTitle(i18n("Warning"));
    _messageBox->setIconPixmap(QIcon::fromTheme("dialog-warning")
                             .pixmap(QMessageBox::standardIcon(QMessageBox::Information).size()));
    _messageBox->setText(i18n("Are you sure you want to quit?"));
    _messageBox->addButton(QMessageBox::Abort);
    _messageBox->addButton(QMessageBox::Cancel);
    _messageBox->setDefaultButton(QMessageBox::Cancel);
    for (KrJob *job: _jobs)
        connect(job, &KrJob::terminated, this, &JobMan::slotUpdateMessageBox);
    slotUpdateMessageBox();

    int result = _messageBox->exec(); // blocking
    _messageBox->deleteLater();
    _messageBox = 0;

    // accepted -> cancel all jobs
    if (result == QMessageBox::Abort) {
        for (KrJob *job: _jobs) {
            job->cancel();
        }
        return true;
    }
    // else:
    return false;
}

void JobMan::manageJob(KrJob *job, bool reverseQueueMode, bool startPaused)
{

    JobMenuAction *menuAction = new JobMenuAction(job, _controlAction);
    connect(menuAction, &QObject::destroyed, this, &JobMan::slotUpdateControlAction);
    _controlAction->menu()->addAction(menuAction);
    slotUpdateControlAction();

    connect(job, &KrJob::started, this, &JobMan::slotKJobStarted);
    connect(job, &KrJob::terminated, this, &JobMan::slotTerminated);

    bool isQueueMode = _queueMode != reverseQueueMode;

    if (!startPaused && (!jobsAreRunning() || !isQueueMode)) {
        job->start();
    }

    _jobs.append(job);

    updateUI();
}

// #### protected slots

void JobMan::slotKJobStarted(KJob *job)
{
    // KJob has two percent() functions
    connect(job, SIGNAL(percent(KJob *, unsigned long)), this,
            SLOT(slotPercent(KJob *, unsigned long)));
    connect(job, &KJob::description, this, &JobMan::slotDescription);
    connect(job, &KJob::suspended, this, &JobMan::updateUI);
    connect(job, &KJob::resumed, this, &JobMan::updateUI);
}

void JobMan::slotControlActionTriggered()
{
    if (_jobs.isEmpty()) {
        _controlAction->menu()->clear();
        _controlAction->setEnabled(false);
        return;
    }

    const bool anyRunning = jobsAreRunning();
    if (!anyRunning && _queueMode) {
        _jobs.first()->start();
    } else {
        for (KrJob *job : _jobs) {
            if (anyRunning)
                job->pause();
            else
                job->start();
        }
    }
}

void JobMan::slotPercent(KJob *, unsigned long)
{
    updateUI();
}

void JobMan::slotDescription(KJob*,const QString &description, const QPair<QString,QString> &field1,
                     const QPair<QString,QString> &field2)
{
    // TODO cache all descriptions
    if (_jobs.length() > 1)
        return;

    _progressBar->setToolTip(
        QString("%1\n%2: %3\n%4: %5")
            .arg(description, field1.first, field1.second, field2.first, field2.second));
}

void JobMan::slotTerminated(KrJob *krJob)
{
    _jobs.removeAll(krJob);

    if (!_jobs.isEmpty() && !jobsAreRunning()) {
        foreach (KrJob *job, _jobs) {
            if (!job->isManuallyPaused()) {
                // start next job
                job->start();
                break;
            }
        }
    }

    updateUI();
}

void JobMan::slotUpdateControlAction()
{
    _controlAction->setEnabled(!_controlAction->menu()->isEmpty());
}

void JobMan::slotUndoTextChange(const QString &text)
{
    _undoAction->setToolTip(KIO::FileUndoManager::self()->undoAvailable() ? text :
                                                                            i18n("Undo Last Job"));
}

void JobMan::slotUpdateMessageBox()
{
    if (!_messageBox)
        return;

    if (_jobs.isEmpty() && _autoCloseMessageBox) {
        _messageBox->done(QMessageBox::Abort);
        return;
    }

    if (_jobs.isEmpty()) {
        _messageBox->setInformativeText("");
        _messageBox->setButtonText(QMessageBox::Abort, "Quit");
        return;
    }

    _messageBox->setInformativeText(i18np("There is one job operation left.",
                                          "There are %1 job operations left.", _jobs.length()));
    _messageBox->setButtonText(QMessageBox::Abort, "Abort Jobs and Quit");
}

// #### private

void JobMan::updateUI()
{
    int totalPercent = 0;
    for (KrJob *job: _jobs) {
        totalPercent += job->percent();
    }
    const bool hasJobs = !_jobs.isEmpty();
    _progressBar->setEnabled(hasJobs);
    if (hasJobs) {
        _progressBar->setValue(totalPercent / _jobs.length());
    } else {
        _progressBar->reset();
    }
    if (!hasJobs)
        _progressBar->setToolTip(i18n("No Jobs"));
    if (_jobs.length() > 1)
        _progressBar->setToolTip(i18np("%1 Job", "%1 Jobs", _jobs.length()));

    const bool running = jobsAreRunning();
    _controlAction->setIcon(QIcon::fromTheme(
        !hasJobs ? "edit-clear" : running ? "media-playback-pause" : "media-playback-start"));
    _controlAction->setToolTip(!hasJobs ? i18n("Clear Job List") : running ?
                                          i18n("Pause All Jobs") :
                                          i18n("Resume Job List"));
}

bool JobMan::jobsAreRunning()
{
    for (KrJob *job: _jobs)
        if (job->isRunning())
            return true;

    return false;
}
