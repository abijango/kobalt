#pragma once
#include <QWidget>
#include "metrics.h"

class QPainter;

namespace kobalt::ui {
using Edges = QFlags<Qt::Edge>;
// These components use public Qt Widgets APIs shared by Qt 5 and Qt 6.
// Their parent owns the widgets. Reader actions and firmware hooks stay outside.
void paintEdges(QPainter &painter, const QRect &rect, Edges edges, const QColor &ink,
                int width = metrics::control::border);
void paintButtonShape(QPainter &painter, const QRect &rect, const QColor &paper, const QColor &fill,
                      const QColor &border);
// The reader may be embedded in a desktop or fixture window with its own theme.
bool darkTheme(const QWidget *widget);
// Light dividers between rows and grey secondary text follow the reader theme.
QColor dividerColor(const QWidget *widget);
QColor noteColor(const QWidget *widget);
// The status rule and the heavy lower rule of a view that uses the header geometry.
void paintHeaderRules(QPainter &painter, const QWidget *view);
void readerStyle(QWidget *widget, const QString &source);
}
