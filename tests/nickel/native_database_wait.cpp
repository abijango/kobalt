#include "platform/nickel/common/native_database_wait.h"
#include "check.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    auto writer = QSqlDatabase::addDatabase("QSQLITE", "writer");
    auto reader = QSqlDatabase::addDatabase("QSQLITE", "reader");
    writer.setDatabaseName(root.path() + "/progress.sqlite");
    reader.setDatabaseName(writer.databaseName());
    require(writer.open() && reader.open(), "Cannot open temporary database");
    {
        QSqlQuery lock(writer), save(reader);
        require(lock.exec("CREATE TABLE progress (position TEXT)"), "Cannot create progress row");
        require(lock.exec("INSERT INTO progress VALUES ('newer')"), "Cannot seed progress");
        require(lock.exec("BEGIN IMMEDIATE"), "Cannot hold competing database writer");
        require(save.exec("PRAGMA busy_timeout=5000"), "Cannot set existing wait");
        {
            kobalt::NativeDatabaseWait wait(reader, true);
            require(wait.ready(), "Cannot disable busy wait");
            QElapsedTimer elapsed;
            elapsed.start();
            require(!save.exec("BEGIN IMMEDIATE"), "Competing writer was ignored");
            require(elapsed.elapsed() < 1000, "Final save waited for a busy database");
        }
        require(save.exec("PRAGMA busy_timeout") && save.next(), "Cannot inspect restored wait");
        require(save.value(0).toInt() == 5000, "Nickel's wait was not restored");
        require(lock.exec("ROLLBACK"), "Cannot release competing writer");
        require(save.exec("SELECT position FROM progress") && save.next(), "Cannot read retained progress");
        require(save.value(0).toString() == "newer", "Competing progress was changed");
    }
    writer.close();
    reader.close();
    writer = reader = {};
    QSqlDatabase::removeDatabase("writer");
    QSqlDatabase::removeDatabase("reader");
}
