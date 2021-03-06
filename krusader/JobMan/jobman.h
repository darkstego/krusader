/*****************************************************************************
 * Copyright (C) 2010 Jan Lepper <dehtris@yahoo.de>                          *
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

#ifndef JOBMAN_H
#define JOBMAN_H

// QtCore
#include <QAction>
// QtWidgets
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>

#include <KCoreAddons/KJob>
#include <KWidgetsAddons/KToolBarPopupAction>

class KrJob;


/**
 * @brief The job manager provides a progress dialog and control over (KIO) file operation jobs.
 *
 * Job manager does not have a window (or dialog). All functions are provided via toolbar actions.
 * Icon, text and tooltip are already set, shortcuts are missing.
 *
 * If Job manager queue mode is activated only the first job is started. If more jobs are incoming
 * via manageJob() they are not started. If the running job finishes the next job in line is
 * started.
 *
 * Note that the desktop system (e.g. KDE Plasma Shell) may also has control over the jobs.
 *
 * Reference: plasma-workspace/kuiserver/progresslistdelegate.h
 *
 * If a job still exists Krusader does not exit on quit() until the job is finished. If the job is
 * paused this takes forever. Call waitForJobs() before exit to prevent this.
 *
 * About undoing jobs: If jobs are recorded (all KrJobs are, some in VFS) we can undo them with
 * FileUndoManager (which is a singleton) here.
 * It would be great if each job in the job list could be undone invividually but FileUndoManager
 * is currently (KF5.27) only able to undo the last recorded job.
 */
class JobMan : public QObject
{
    Q_OBJECT

public:
    explicit JobMan(QObject *parent = 0);
    /** Toolbar action icon for pausing/starting all jobs with drop down menu showing all jobs.*/
    QAction *controlAction() const { return _controlAction; }
    /** Toolbar action progress bar showing the average job progress percentage of all jobs.*/
    QAction *progressAction() const { return _progressAction; }
    /** Toolbar action combo box for changing the .*/
    QAction *modeAction() const { return _modeAction; }
    QAction *undoAction() const { return _undoAction; }

    /** Wait for all jobs to terminate (blocking!).
     *
     * Returns true immediately if there are no jobs and user input is not required. Otherwise a
     * modal UI dialog is shown and the user can abort all jobs or cancel the dialog.
     *
     * @param waitForUserInput if true dialog is only closed after user interaction (button click)
     * @return true if no jobs are running (anymore) and user wants to quit. Else false
     */
    bool waitForJobs(bool waitForUserInput);

    /* Curent info about _queueMode state */
    bool isQueueModeEnabled() { return _queueMode; };

public slots:
    /** Display, monitor and give user ability to control a job.
     *
     * If reverseQueueMode is false, job is queued or run in parallel accordingly
     * to job manager mode. When reverseQueueMode is true, opposite manager mode is chosen.
     *
     * When startPaused is true, job is never started immediately. Instead, it is waiting
     * to be manually unpaused. Or in case of enabled queueMode it is started automatically
     * when other jobs are finished.
     */
    void manageJob(KrJob *krJob, bool reverseQueueMode = false, bool startPaused = false);

protected slots:
    void slotKJobStarted(KJob *krJob);
    void slotControlActionTriggered();
    void slotPercent(KJob *, unsigned long);
    void slotDescription(KJob*,const QString &description, const QPair<QString,QString> &field1,
                         const QPair<QString,QString> &field2);
    void slotTerminated(KrJob *krJob);
    void slotUpdateControlAction();
    void slotUndoTextChange(const QString &text);
    void slotUpdateMessageBox();

private:
    void updateUI();
    bool jobsAreRunning();

    QList<KrJob *> _jobs; // all jobs not terminated (finished or canceled) yet
    bool _queueMode;

    KToolBarPopupAction *_controlAction;
    QProgressBar *_progressBar;
    QAction *_progressAction;
    QAction *_modeAction;
    QAction *_undoAction;

    QMessageBox *_messageBox;
    bool _autoCloseMessageBox;

    static const QString sDefaultToolTip;
};

#endif // JOBMAN_H
