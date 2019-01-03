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

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QProcess>
#include <QTextCodec>
#include <QScrollBar>
#include <QFile>
#include <QTextStream>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->listWidgetKeys->setSelectionMode(QAbstractItemView::SingleSelection);
    QLabel* copyright_label = new QLabel(this);
    copyright_label->setText(QString("(c) 2019 - Mathieu Allory - Under MIT License - Build %1:%2").arg(__DATE__).arg(__TIME__));
    statusBar()->addPermanentWidget(copyright_label);

    refresh_gui_buttons();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_pushButtonGpgCheck_clicked()
{
    QString result = execute("gpg", QStringList() << "--version");
    QRegularExpression rx1("^(?<version>.*)\r.*");
    ui->lineEditGpgVersion->setText(rx1.match(result).captured("version"));
    QRegularExpression rx2(".*(Home: )(?<home>.*)\r.*");
    gpg_dir = rx2.match(result).captured("home");
    ui->lineEditGpgHome->setText(gpg_dir);

    refresh_gui_buttons();
}

QString MainWindow::execute(const QString& i_command, const QStringList& i_args)
{
    QProcess process;
    statusBar()->showMessage("Running " + i_command);
    process.start(i_command, i_args);
    // Wait until finished (10 sec)
    if(!process.waitForFinished(10000))
    {
        statusBar()->showMessage("Could not run " + i_command + ": timeout");
        return "";
    }
    statusBar()->clearMessage();

    QString read_data = QTextCodec::codecForMib(2252)->toUnicode(process.readAll());
    read_data += QTextCodec::codecForMib(2252)->toUnicode(process.readAllStandardError());
    log_text("[" + i_command + (i_args.empty() ? "" : " ") + i_args.join(" ") + "]\n", true);
    log_text(read_data);

    return read_data;
}

void MainWindow::log_text(const QString& i_text, bool i_new_paragraph)
{
    // Always move cursor at the end
    QTextCursor cursor_to_end(ui->textEditLogs->document());
    cursor_to_end.movePosition(QTextCursor::End);
    ui->textEditLogs->setTextCursor(cursor_to_end);

    // Add extra new line for a new paragraph
    if(i_new_paragraph && !ui->textEditLogs->document()->toPlainText().isEmpty())
    {        
        ui->textEditLogs->insertPlainText("\n");
    }

    // Append text
    ui->textEditLogs->insertPlainText(i_text);

    // Scroll down the text panel
    QScrollBar *sb = ui->textEditLogs->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::on_pushButtonAgentGetConfig_clicked()
{
    QStringList result = execute("gpgconf", QStringList() << "--list-options" << "gpg-agent").split("\r\n");
    bool putty_enabled = false;
    for (const QString& line: result)
    {
        if (line.startsWith("enable-putty-support"))
        {
            QStringList elt = line.split(":");
            if (elt.size() >=  10 && elt[9] == "1")
            {
                putty_enabled = true;
            }
        }
    }
    ui->lineEditPageantSupport->setText(putty_enabled ? "true" : "false");

    refresh_gui_buttons();
}

void MainWindow::on_pushButtonAgentRestart_clicked()
{
    execute("gpgconf", QStringList() << "--kill" << "gpg-agent");
    execute("gpgconf", QStringList() << "--launch" << "gpg-agent");
    log_text("WARNING: gpg-agent spawns processes to handle putty-like request - so do not forget to also restart your client !\n");
    refresh_gui_buttons();
}

void MainWindow::on_pushButtonClearLogs_clicked()
{
    this->ui->textEditLogs->clear();
}

void MainWindow::refresh_gui_buttons()
{
    // Most actions are possible only when the right version of GPG is available
    bool rightVer = (ui->lineEditGpgVersion->text() == "gpg (GnuPG) 2.2.4");
    ui->groupBoxGpgAgent->setEnabled(rightVer);
    ui->groupBoxKeys->setEnabled(rightVer);

    // Only not yet authorized keys can be added to ssh control
    bool can_authorize_key = false;
    if (ui->listWidgetKeys->currentItem())
    {
        QString key_hash = ui->listWidgetKeys->currentItem()->data(Qt::UserRole).toString();
        // Check whether that fingerprint is authorized
        for (key* k : keys)
        {
            if (k->hash == key_hash && k->sshcontrol == key::unauthorized)
            {
                can_authorize_key = true;
                break;
            }
        }
    }
    ui->pushButtonAuthorizeKey->setEnabled(can_authorize_key);

    if (ui->lineEditRawSshKey->text().isEmpty()) ui->lineEditRawSshKey->setEnabled(false);
    if (ui->lineEditStrippedSshKey->text().isEmpty()) ui->lineEditStrippedSshKey->setEnabled(false);

    ui->pushButtonAgentEnablePutty->setEnabled(ui->lineEditPageantSupport->text() == "false");
    // Copy buttons
    ui->pushButtonRawSshKeyCopy->setEnabled(ui->lineEditRawSshKey->isEnabled());
    ui->pushButtonStrippedSshKeyCopy->setEnabled(ui->lineEditStrippedSshKey->isEnabled());
}

void MainWindow::on_pushButtonClearFields_clicked()
{
    ui->lineEditGpgHome->clear();
    ui->lineEditGpgVersion->clear();
    ui->lineEditPageantSupport->clear();
    ui->lineEditRawSshKey->clear();
    ui->lineEditStrippedSshKey->clear();
    clear_keys();
    gpg_dir.clear();
    sshcontrol.clear();
    refresh_gui_buttons();
}

void MainWindow::on_pushButtonKeysQuery_clicked()
{
    clear_keys();
    QStringList result = execute("gpg", QStringList() << "--with-keygrip" << "--fingerprint" << "--fingerprint" << "-k").split("\r\n");

    // Small state machine to parse the list of keys
    bool in_key_group = false;
    bool in_sub = false;
    key* new_key = new key();
    for (QString line : result)
    {
        // Public key --> new group
        if (line.startsWith("pub"))
        {
            // entering new key group
            if (in_key_group || in_sub)
            {
                goto error;
            }
            else
            {
                in_key_group = true;
            }

            // Parse pub
            sub a_sub = parse_key_sub(line);
            new_key->subs.push_back(a_sub);
        }

        // Parse sub
        else if (line.startsWith("sub"))
        {
            // must be in a key group
            if (in_key_group == false)
            {
                goto error;
            }
            in_sub = true;

            // Parse pub
            sub a_sub = parse_key_sub(line);
            new_key->subs.push_back(a_sub);
        }

        // Parse uid
        else if (line.startsWith("uid"))
        {
            // must be in a key group
            // uid shall appear before subkeys
            if (in_key_group == false || in_sub)
            {
                goto error;
            }
            // "uid" must be followed by 10 blank spaces
            QRegularExpression rx_pub("^uid[ ]{10}\\[(?<exp>.*)\\] (?<name>.+) \\<(?<mail>.+)\\>");
            uid a_uid;
            a_uid.exp = rx_pub.match(line).captured("exp");
            a_uid.name = rx_pub.match(line).captured("name");
            a_uid.mail = rx_pub.match(line).captured("mail");

            new_key->uids.push_back(a_uid);
        }

        // Parse grip
        else if (line.contains("Keygrip"))
        {
            // must be in a key group
            if (in_key_group == false)
            {
                goto error;
            }
            // "uid" must be followed by 10 blank spaces
            QRegularExpression rx_pub("^[ ]{6}Keygrip = (?<grip>.*)");
            if (new_key->subs.empty())
            {
                goto error;
            }
            new_key->subs.last().grip = rx_pub.match(line).captured("grip");
        }

        // Parse hash/fingerprint (starts with 6 spaces)
        else if (line.startsWith("      ") && !line.contains("Keygrip"))
        {
            // must be in a key group
            if (in_key_group == false)
            {
                goto error;
            }
            // Keep the fingerprint, remove whitespaces
            QString fp = line.mid(6).remove(" ");
            // The principal fingerprint is also the hash for the whole key
            if (!in_sub)
            {
                new_key->hash = fp;
            }
            // Also add to the last sub parsed
            if (new_key->subs.empty())
            {
                goto error;
            }
            new_key->subs.last().fingerprint = fp;
        }

        // empty line --> end group
        else if (line.isEmpty() && in_key_group)
        {
            this->keys.push_back(new_key);
            new_key = new key();
            in_key_group = false;
            in_sub = false;
        }
    }
    // Might leak of one key(), doesn't really matter

    // Log results - in log window
    for (key* k : keys)
    {
        log_text(k->hash + "\n");
        for (sub s : k->subs)
        {
            log_text("- " + s.algo + (s.auth ? " AUTH " : " NO-AUTH ") + s.grip + " " + s.fingerprint + "\n");
        }
        for (uid u : k->uids)
        {
            log_text("- " + u.name + " " + u.mail + " " + u.exp + "\n");
        }
    }

    // Update widget content
    update_list_of_keys_from_struct();
    return;

error:
    log_text("ERROR: cannot parse gpg output\n");
    return;
}

void MainWindow::update_list_of_keys_from_struct()
{
    ui->listWidgetKeys->clear();
    // Log results - in log window + in the list widget
    for (key* k : keys)
    {
        QString key_digest = k->hash;
        QString key_auth_grip;
        bool can_auth = false;
        for (sub s : k->subs)
        {
            // Does this key has a subkey that can authenticate ?
            if (s.auth)
            {
                can_auth = true;
                key_auth_grip = s.grip;
            }
        }
        QStringList names;
        for (uid u : k->uids)
        {
            names.append(u.name);
        }
        if (!names.empty())
        {
            key_digest += " (" + names.join("|") + ")";
        }
        if (k->sshcontrol == key::unknown) key_digest += " [ssh: unknown]";
        else if (k->sshcontrol == key::authorized) key_digest += " [ssh: authorized]";
        else if (k->sshcontrol == key::unauthorized) key_digest += " [ssh: not authorized]";

        QListWidgetItem *newItem = new QListWidgetItem;
        newItem->setText(key_digest);
        newItem->setData(Qt::UserRole, k->hash);
        ui->listWidgetKeys->addItem(newItem);
    }

    refresh_gui_buttons();
}

sub MainWindow::parse_key_sub(const QString& i_line)
{
    QRegularExpression rx_pub("^(pub|sub)[ ]{3}(?<algo>.*) \\[(?<capa>[A-Z]+)\\]");
    sub a_sub;
    a_sub.algo = rx_pub.match(i_line).captured("algo");
    QString capa = rx_pub.match(i_line).captured("capa");
    if (capa.contains("A")) a_sub.auth = true;
    else a_sub.auth = false;

    return a_sub;
}

void MainWindow::on_listWidgetKeys_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous)

    QString fingerprint, fingerprint_auth, result;
    QStringList tok_key;

    if (current == nullptr)
    {
        ui->lineEditRawSshKey->clear();
        ui->lineEditStrippedSshKey->clear();
        goto go_out;
    }
    // Get key fingerprint and print it
    fingerprint = current->data(Qt::UserRole).toString();
    // The fingerprint we got is the one from the main, public key
    // not necessarily the one from the [A] key - so get the right one
    for (key* k : keys)
    {
        if (k->hash == fingerprint)
        {
            for(sub s: k->subs)
            {
                if (s.auth)
                {
                    fingerprint_auth = s.fingerprint;
                    break;
                }
            }
        }
    }

    if (fingerprint_auth.isEmpty())
    {
        QString err_msg = "Cannot find a suitable key for ssh (no subkey with auth capability found) !";
        ui->lineEditRawSshKey->setText(err_msg);
        ui->lineEditRawSshKey->setEnabled(false);
        ui->lineEditStrippedSshKey->setText(err_msg);
        ui->lineEditStrippedSshKey->setEnabled(false);
        goto go_out;
    }

    result = execute("gpg", QStringList() << "--export-ssh-key" << fingerprint_auth + "!");
    ui->lineEditRawSshKey->setText(result);
    ui->lineEditRawSshKey->setEnabled(true);
    tok_key = result.split(" ");
    if(tok_key.size() >= 3)
    {
        ui->lineEditStrippedSshKey->setText(tok_key[1]);
        ui->lineEditStrippedSshKey->setEnabled(true);
    }

go_out:
    refresh_gui_buttons();
}

void MainWindow::clear_keys()
{
    ui->listWidgetKeys->clear();
    for(key* k : keys)
    {
        delete k;
    }
    keys.clear();
}

void MainWindow::on_pushButtonQuerySshControl_clicked()
{
    if (gpg_dir.isEmpty()) return;
    sshcontrol.clear();
    // First, set all keys to unauthorized
    for (key* k : keys)
    {
        k->sshcontrol = key::unauthorized;
    }

    QFile ctrl_file(gpg_dir + "/sshcontrol");
    if (!ctrl_file.exists())
    {
        log_text("sshcontrol file does not yet exist (will be created)\n", true);
        goto go_out;
    }

    if (ctrl_file.open(QIODevice::ReadOnly))
    {
        log_text("Content of " + ctrl_file.fileName() + ":\n", true);
        QTextStream in(&ctrl_file);

        // Then loop on actual data and set to authorized those found in file
        while (!in.atEnd())
        {
           QString found_key = in.readLine();
           log_text(found_key + "\n");

           // Update status of known keys
           for (key* k : keys)
           {
               for (sub s: k->subs)
               {
                   if (s.grip == found_key)
                   {
                       k->sshcontrol = key::authorized;
                   }
               }
           }
        }
        ctrl_file.close();
    }
    else
    {
        log_text("ERROR: Cannot open " + ctrl_file.fileName() + "\n");
    }

go_out:
    update_list_of_keys_from_struct();
    refresh_gui_buttons();
}

void MainWindow::on_pushButtonAuthorizeKey_clicked()
{
    if (ui->listWidgetKeys->currentItem())
    {
        // First try to open the file
        QFile ssh_control_file(gpg_dir + "/sshcontrol");
        ssh_control_file.open(QIODevice::Append | QIODevice::Text);
        if (!ssh_control_file.isOpen())
        {
            log_text("ERROR: Cannot open " + ssh_control_file.fileName() + " for modification\n");
            return;
        }

        QString key_hash = ui->listWidgetKeys->currentItem()->data(Qt::UserRole).toString();
        // Check whether that fingerprint is authorized
        for (key* k : keys)
        {
            if (k->hash == key_hash && k->sshcontrol == key::unauthorized)
            {
                // OK, we found the key, let's do this.
                // Get the grip of the first subkey that can authenticate
                for(sub s : k->subs)
                {
                    if (s.auth && !s.grip.isEmpty())
                    {
                        // gotcha !
                        log_text("Adding grip " + s.grip + " for fingerprint " + k->hash + " to ssh control file " + ssh_control_file.fileName() + "\n", true);
                        QTextStream out(&ssh_control_file);
                        out << s.grip << endl;
                        ssh_control_file.close();
                        // Refresh screen
                        on_pushButtonQuerySshControl_clicked();
                        return;
                    }
                }
                // If we are still here, it means we found no suitable key
                log_text("No suitable key (allowing authentication) found for fingerprint " + k->hash + " \n", true);
            }
        }
        ssh_control_file.close();
    }
}

void MainWindow::on_pushButtonRawSshKeyCopy_clicked()
{
    ui->lineEditRawSshKey->selectAll();
    ui->lineEditRawSshKey->copy();
}

void MainWindow::on_pushButtonStrippedSshKeyCopy_clicked()
{
    ui->lineEditStrippedSshKey->selectAll();
    ui->lineEditStrippedSshKey->copy();
}

void MainWindow::on_pushButtonAgentEnablePutty_clicked()
{
    // Append enable-putty-support at the end of gpg-agent.conf
    // First try to open the file
    QFile gpg_conf_file(gpg_dir + "/gpg-agent.conf");
    gpg_conf_file.open(QIODevice::Append | QIODevice::Text);
    if (!gpg_conf_file.isOpen())
    {
        log_text("ERROR: Cannot open " + gpg_conf_file.fileName() + " for modification\n");
        return;
    }

    log_text("Adding 'enable-putty-support' to gpg-agent configuration file " + gpg_conf_file.fileName() + "\n", true);
    QTextStream out(&gpg_conf_file);
    out << "enable-putty-support" << endl;
    gpg_conf_file.close();

    // Reload configuration
    this->on_pushButtonAgentGetConfig_clicked();
    // Restart agent
    this->on_pushButtonAgentRestart_clicked();
}
