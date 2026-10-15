#pragma once

#include <QApplication>
#include <QEvent>
#include <QPointer>
#include <QWidget>

namespace kobalt::nickel {
// Nickel owns the dialog and its actions. Restore its parent before the
// reader closes so destroying the reader cannot destroy Nickel's dialog.
class UsbDialogOverlay : public QObject {
public:
    UsbDialogOverlay() {
        qApp->installEventFilter(this);
    }
    ~UsbDialogOverlay() override {
        restore();
    }
    bool active() const {
        return !dialog_.isNull();
    }
    void present(QWidget *dialog, QWidget *reader) {
        if (!dialog || !reader || active()) {
            return;
        }
        dialog_ = dialog;
        reader_ = reader;
        parent_ = dialog->parentWidget();
        flags_ = dialog->windowFlags();
        geometry_ = dialog->geometry();
        const auto position = reader->mapFromGlobal(dialog->mapToGlobal(QPoint()));
        moving_ = true;
        // A child of Nickel's main window cannot paint above the separate
        // reader window. Keep the same dialog and its native callbacks.
        dialog->setParent(reader, Qt::Widget);
        dialog->move(position);
        dialog->show();
        dialog->raise();
        moving_ = false;
    }
    void restore() {
        if (!dialog_) {
            return;
        }
        moving_ = true;
        auto dialog = dialog_;
        dialog_.clear();
        dialog->hide();
        dialog->setParent(parent_, flags_);
        dialog->setGeometry(geometry_);
        parent_.clear();
        reader_.clear();
        moving_ = false;
    }

private:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (!moving_ && event->type() == QEvent::Hide && (object == dialog_ || object == reader_)) {
            restore();
        }
        return false;
    }
    QPointer<QWidget> dialog_, reader_, parent_;
    Qt::WindowFlags flags_;
    QRect geometry_;
    bool moving_ = false;
};
}
