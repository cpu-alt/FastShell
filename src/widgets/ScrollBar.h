// Copyright 2020 WindTerm, 2026 FastShell contributors. Apache-2.0.
//
// QScrollBar with a 64-bit range, for scrollback buffers larger than INT_MAX
// lines. The underlying QScrollBar works on a scaled-down int range.
#pragma once

#include <QScrollBar>

class ScrollBar : public QScrollBar {
    Q_OBJECT

public:
    explicit ScrollBar(Qt::Orientation orientation, QWidget *parent = nullptr);

    qint64 maximum() const { return m_maximum; }
    qint64 minimum() const { return m_minimum; }
    qint64 value() const { return m_value; }

    bool setMaximum(qint64 maximum);
    bool setMinimum(qint64 minimum);
    bool setPage(int page);
    bool setRange(qint64 minimum, qint64 maximum);
    bool setSingleStep(int singleStep);
    bool setValue(qint64 value);

Q_SIGNALS:
    void pageChanged(int page);
    void rangeChanged(qint64 minimum, qint64 maximum);
    void singleStepChanged(int singleStep);
    void valueChanged(qint64 value, qint64 oldValue);

private:
    bool scaled() const { return m_maximum - m_minimum > INT_MAX; }
    int toSlider(qint64 value) const;
    qint64 fromSlider(int position) const;
    void updateValue(qint64 value);

    qint64 m_maximum = 0;
    qint64 m_minimum = 0;
    qint64 m_value = 0;
};
