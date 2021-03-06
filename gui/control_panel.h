/*
 * Main control panel
 */
#pragma once

#include <QTime>
#include <QWidget>

#include "app/app_config.h"
#include "nanosdr/common/datatypes.h"
#include "nanosdr/common/sdr_data.h"

namespace Ui {
    class ControlPanel;
}

class ControlPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ControlPanel(QWidget *parent = nullptr);
    ~ControlPanel();

    void    readSettings(const app_config_t &conf);
    void    saveSettings(app_config_t &conf);

    void    addRxControls(QWidget *controls);
    void    addSignalData(double rms);

signals:
    void    rxGainModeChanged(int mode);
    void    rxGainChanged(int gain);
    void    demodChanged(sdr_demod_t new_demod);
    void    filterChanged(real_t low_cut, real_t high_cut);
    void    cwOffsetChanged(real_t offset);

private slots:
    void    on_rxButton_clicked(bool);
    void    on_txButton_clicked(bool);
    void    on_dispButton_clicked(bool);
    void    on_recButton_clicked(bool);

    void    on_amButton_clicked(bool);
    void    on_ssbButton_clicked(bool);
    void    on_cwButton_clicked(bool);
    void    on_fmButton_clicked(bool);

private:
    void    initModeSettings(void); // FIXME: Replace with a readSettings()
    void    updateMode(quint8);

private:
    Ui::ControlPanel   *ui;

    quint8              current_mode; // CP_MODE_...
    quint8              last_ssb_mode;

    // Mode definitions and settings
    struct  mode_setting {
        sdr_demod_t    demod;
        real_t         filter_lo;
        real_t         filter_hi;
        real_t         cw_offset;
    };

    struct mode_setting   *mode_settings; // latest settings for each mode

    // signal statistics
    struct {
        QTime          timer;
        double         min;
        double         max;
        double         rms;
        quint32        num;
        bool           reset;
    } stats;

};
