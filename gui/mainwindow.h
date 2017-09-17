/*
 * Main application window interface
 */
#pragma once

#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/sdr_thread.h"
#include "gui/freq_ctrl.h"

namespace Ui {
    class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void    runButtonClicked(bool);
    void    menuActivated(QAction *);

private:
    void    createButtons(void);

private:
    Ui::MainWindow   *ui;

    SdrThread        *sdr;

    // controls
    FreqCtrl         *fctl;

    // Buttons
    QToolButton      *ptt_button;
    QToolButton      *run_button;
    QToolButton      *cfg_button;
    QMenu            *cfg_menu;

    // layout containers
    QVBoxLayout      *win_layout;
    QHBoxLayout      *top_layout;
    QHBoxLayout      *main_layout;
};
