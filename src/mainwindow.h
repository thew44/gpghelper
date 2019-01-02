/*
Copyright (c) 2019 - Mathieu ALLORY

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidgetItem>

namespace Ui {
class MainWindow;
}

struct uid
{
    QString exp;
    QString name;
    QString mail;
};

struct sub
{
    QString fingerprint;
    QString algo;
    bool auth;
    QString grip;

    sub()
    {
        auth = false;
    }
};

struct key
{
    QString hash;
    QList<uid> uids;
    QList<sub> subs;
    // authorized in sshcontrol ?
    enum sshcontrol_t
    {
        unknown,
        authorized,
        unauthorized
    };
    sshcontrol_t sshcontrol;

    key()
    {
        sshcontrol = unknown;
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void on_pushButtonGpgCheck_clicked();

    QString execute(const QString& i_command, const QStringList& i_args);

    void on_pushButtonAgentGetConfig_clicked();

    void on_pushButtonAgentRestart_clicked();

    void log_text(const QString& i_text, bool i_new_paragraph = false);

    void on_pushButtonClearLogs_clicked();

    void on_pushButtonClearFields_clicked();

    void on_pushButtonKeysQuery_clicked();

    void on_listWidgetKeys_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);   

    void on_pushButtonQuerySshControl_clicked();

    void on_pushButtonAuthorizeKey_clicked();

    void on_pushButtonRawSshKeyCopy_clicked();

    void on_pushButtonStrippedSshKeyCopy_clicked();

private:
    sub parse_key_sub(const QString& i_line);
    void clear_keys();
    void update_list_of_keys_from_struct();
    void refresh_gui_buttons();

private:
    Ui::MainWindow *ui;
    QList<key*> keys;
    QString gpg_dir;
    QStringList sshcontrol;
};

#endif // MAINWINDOW_H
