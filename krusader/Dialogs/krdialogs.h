/*****************************************************************************
 * Copyright (C) 2000 Shie Erlich <erlich@users.sourceforge.net>             *
 * Copyright (C) 2000 Rafi Yanai <yanai@users.sourceforge.net>               *
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

#ifndef KRDIALOGS_H
#define KRDIALOGS_H

// QtCore
#include <QDateTime>
#include <QUrl>
// QtWidgets
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
// QtGui
#include <QPixmap>

#include <KWidgetsAddons/KAnimatedButton>
#include <KIOWidgets/KFile>
#include <KIOWidgets/KUrlRequesterDialog>
#include <KWidgetsAddons/KDatePicker>

/** \class KChooseDir
 * Used for asking the user for a folder.
 * example:
 * \code
 * QUrl u = KChooseDir::getDir("target folder", "/suggested/path", ACTIVE_PANEL->virtualPath());
 * if (u.isEmpty()) {
 *   // user canceled (either by pressing cancel, or esc
 * } else {
 *   // do you thing here: you've got a safe url to use
 * }
 * \endcode
 */
class KChooseDir
{
public:
    struct ChooseResult {
        QUrl url;
        bool reverseQueueMode;
        bool startPaused;
        bool preserveAttrs; // NOTE: field never read
        QUrl baseURL;       // NOTE: field never read
    };

    /**
     * \param text - description of the info requested from the user
     * \param url - a suggested url to appear in the box as a default choice
     * \param cwd - a path which is the current working directory (usually ACTIVE_PANEL->virtualPath()).
     *              this is used for completion of partial urls
     */
    static QUrl getFile(const QString &text, const QUrl &url, const QUrl &cwd);
    static QUrl getDir(const QString &text, const QUrl &url, const QUrl &cwd);
    static ChooseResult getCopyDir(const QString &text, const QUrl &url, const QUrl &cwd,
                                   bool preserveAttrs = false, const QUrl &baseURL = QUrl());

  private:
    static QUrl get(const QString &text, const QUrl &url, const QUrl &cwd, KFile::Modes mode);
};

class KUrlRequesterDlgForCopy : public QDialog
{
    Q_OBJECT
public:
    KUrlRequesterDlgForCopy(const QUrl& url, const QString& text, bool presAttrs,
                            QWidget *parent, bool modal = true, QUrl baseURL = QUrl());

    QUrl selectedURL() const;
    QUrl baseURL() const;
    bool preserveAttrs();
    bool isReverseQueueMode() { return reverseQueueMode; };
    bool isStartPaused() { return pauseBox->isChecked(); };
    bool copyDirStructure();
    void hidePreserveAttrs() {
//         preserveAttrsCB->hide();
    }

    KUrlRequester *urlRequester();

protected:
    virtual void keyPressEvent(QKeyEvent *e) Q_DECL_OVERRIDE;


private slots:
    void slotReverseQueueMode();
    void slotTextChanged(const QString &);
    void slotDirStructCBChanged();
private:
    KUrlRequester *urlRequester_;
    QComboBox *baseUrlCombo;
//     QCheckBox *preserveAttrsCB;
    QCheckBox *copyDirStructureCB;
    QCheckBox *pauseBox;
    QPushButton *okButton;
    bool reverseQueueMode = false;
};

class KRGetDate : public QDialog
{
    Q_OBJECT
public:
    KRGetDate(QDate date = QDate::currentDate(), QWidget *parent = 0);
    QDate getDate();

private slots:
    void setDate(QDate);

private:
    KDatePicker *dateWidget;
    QDate chosenDate, originalDate;
};

#endif
