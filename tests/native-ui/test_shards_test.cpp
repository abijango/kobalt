#include "qt_test_shards.h"

#include <QTest>

class Slots : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {}
    void cleanupTestCase() {}
    void init() {}
    void cleanup() {}
    void first_data() {}
    void first() {}
    void second() {}
    void third() {}
    void argument(int) {}
signals:
    void signal();
};

class ShardTest : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        qunsetenv("KOBALT_TEST_SHARD");
        qunsetenv("KOBALT_TEST_DATA_SHARD");
        qunsetenv("KOBALT_TEST_EXCLUDE");
    }
    void completeDisjointSelection() {
        Slots fixture;
        char executable[] = "test";
        char *argv[] = {executable};
        qputenv("KOBALT_TEST_SHARD", "0/2");
        QCOMPARE(kobalt::test::shardArguments(fixture, 1, argv), QStringList({"test", "first", "third"}));
        qputenv("KOBALT_TEST_SHARD", "1/2");
        QCOMPARE(kobalt::test::shardArguments(fixture, 1, argv), QStringList({"test", "second"}));
        qunsetenv("KOBALT_TEST_SHARD");
    }
    void explicitSelectionWithoutSharding() {
        Slots fixture;
        char executable[] = "test", selected[] = "first:row", option[] = "-v1";
        char *argv[] = {executable, selected, option};
        QCOMPARE(kobalt::test::shardArguments(fixture, 3, argv), QStringList({"test", "first:row", "-v1"}));
    }
    void dataRowsAndSeparateCases() {
        qputenv("KOBALT_TEST_DATA_SHARD", "1/3");
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(kobalt::test::shardContains(i, "KOBALT_TEST_DATA_SHARD"), i % 3 == 1);
        }
        Slots fixture;
        char executable[] = "test";
        char *argv[] = {executable};
        qputenv("KOBALT_TEST_SHARD", "0/1");
        qputenv("KOBALT_TEST_EXCLUDE", "second");
        QCOMPARE(kobalt::test::shardArguments(fixture, 1, argv), QStringList({"test", "first", "third"}));
    }
};
QTEST_GUILESS_MAIN(ShardTest)
#include "test_shards_test.moc"
