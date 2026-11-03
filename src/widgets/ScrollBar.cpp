// Copyright 2020 WindTerm, 2026 WindTerm NG contributors. Apache-2.0.
//
// Fix from the original: the int <-> 64-bit mapping ignored the minimum, so a
// non-zero minimum gave wrong positions.
#include "widgets/ScrollBar.h"

#include <QSignalBlocker>
#include <climits>

ScrollBar::ScrollBar(Qt::Orientation orientation, QWidget *parent) : QScrollBar(orientation, parent) {
    setContextMenuPolicy(Qt::PreventContextMenu);
    m_minimum = QScrollBar::minimum();
    m_maximum = QScrollBar::maximum();
    m_value = QScrollBar::value();

    connect(this, &QScrollBar::actionTriggered, this, [this](int action) {
        switch (action) {
        case SliderMove: updateValue(fromSlider(sliderPosition())); break;
        case SliderPageStepAdd: updateValue(m_value + pageStep()); break;
        case SliderPageStepSub: updateValue(m_value - pageStep()); break;
        case SliderSingleStepAdd: updateValue(m_value + singleStep()); break;
        case SliderSingleStepSub: updateValue(m_value - singleStep()); break;
        case SliderToMaximum: updateValue(m_maximum); break;
        case SliderToMinimum: updateValue(m_minimum); break;
        default: break;
        }
        setSliderPosition(toSlider(m_value));
    });
}

int ScrollBar::toSlider(qint64 value) const {
    if (!scaled()) {
        return int(value);
    }
    const double ratio = double(value - m_minimum) / double(m_maximum - m_minimum);
    return int(ratio * INT_MAX);
}

qint64 ScrollBar::fromSlider(int position) const {
    if (!scaled()) {
        return position;
    }
    return m_minimum + qint64(double(position) / INT_MAX * double(m_maximum - m_minimum));
}

void ScrollBar::updateValue(qint64 value) {
    value = qBound(m_minimum, value, m_maximum);
    if (value != m_value) {
        const qint64 old = std::exchange(m_value, value);
        Q_EMIT valueChanged(m_value, old);
    }
}

bool ScrollBar::setMaximum(qint64 maximum) {
    return setRange(std::min(m_minimum, maximum), maximum);
}

bool ScrollBar::setMinimum(qint64 minimum) {
    return setRange(minimum, std::max(minimum, m_maximum));
}

bool ScrollBar::setPage(int page) {
    if (page > 0 && page != pageStep()) {
        setPageStep(page);
        Q_EMIT pageChanged(page);
        return true;
    }
    return false;
}

bool ScrollBar::setRange(qint64 minimum, qint64 maximum) {
    if (minimum > maximum || (minimum == m_minimum && maximum == m_maximum)) {
        return false;
    }
    m_minimum = minimum;
    m_maximum = maximum;
    {
        QSignalBlocker blocker(this);
        if (scaled()) {
            QScrollBar::setRange(0, INT_MAX);
        } else {
            QScrollBar::setRange(int(minimum), int(maximum));
        }
    }
    Q_EMIT rangeChanged(minimum, maximum);
    setValue(m_value);
    return true;
}

bool ScrollBar::setSingleStep(int step) {
    if (step > 0 && step != singleStep()) {
        QScrollBar::setSingleStep(step);
        Q_EMIT singleStepChanged(step);
        return true;
    }
    return false;
}

bool ScrollBar::setValue(qint64 value) {
    value = qBound(m_minimum, value, m_maximum);
    {
        QSignalBlocker blocker(this);
        QScrollBar::setValue(toSlider(value));
    }
    if (value != m_value) {
        const qint64 old = std::exchange(m_value, value);
        Q_EMIT valueChanged(m_value, old);
        return true;
    }
    return false;
}
