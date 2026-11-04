// Copyright 2020 WindTerm, 2026 FastShell contributors. Apache-2.0.
//
// QSplitter whose side panes can be collapsed and restored to their previous
// ratio, by code or by double-clicking the handle.
#pragma once

#include <QSplitter>

class Splitter : public QSplitter {
    Q_OBJECT

public:
    explicit Splitter(QWidget *parent = nullptr);
    explicit Splitter(Qt::Orientation orientation, QWidget *parent = nullptr);

    void showIndex(int index);
    void hideIndex(int index);
    bool isIndexVisible(int index) const;
    void setIndexVisible(int index, bool visible);
    void setMainIndex(int mainIndex) { m_mainIndex = mainIndex; }

Q_SIGNALS:
    void showIndexRequested(int index);
    void hideIndexRequested(int index);

protected:
    QSplitterHandle *createHandle() override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    int m_mainIndex = 0;
};
