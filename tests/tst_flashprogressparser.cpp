#include "engine/FlashProgressParser.h"
#include <QtTest>

class TestFlashProgressParser : public QObject {
    Q_OBJECT

private slots:
    void anAnnouncedPlanShowsNothingYet() {
        FlashProgressParser parser;
        const auto event = parser.feed(R"({"event":"plan","op":"erase","total":1000})");
        QCOMPARE(event.kind, FlashHelperEvent::Kind::Ignored);
    }

    void erasePercentageGrowsWithTheFinishedBytes() {
        FlashProgressParser parser;
        parser.feed(R"({"event":"plan","op":"erase","total":1000})");
        parser.feed(R"({"event":"started","op":"erase"})");

        auto event = parser.feed(R"({"event":"progress","op":"erase","bytes":250})");
        QCOMPARE(event.kind, FlashHelperEvent::Kind::Progress);
        QCOMPARE(event.percent, 25);
        QCOMPARE(event.text, QString("Erasing 25%"));

        event = parser.feed(R"({"event":"progress","op":"erase","bytes":500})");
        QCOMPARE(event.percent, 75);
    }

    void thePercentageStaysBelowOneHundredUntilTheResult() {
        FlashProgressParser parser;
        parser.feed(R"({"event":"plan","op":"erase","total":1000})");
        const auto event = parser.feed(R"({"event":"progress","op":"erase","bytes":1000})");
        QCOMPARE(event.percent, 99);
    }

    void severalOperationsShareOnePercentage() {
        FlashProgressParser parser;
        parser.feed(R"({"event":"plan","op":"erase","total":500})");
        parser.feed(R"({"event":"plan","op":"program","total":300})");
        parser.feed(R"({"event":"plan","op":"verify","total":200})");

        auto event = parser.feed(R"({"event":"progress","op":"erase","bytes":500})");
        QCOMPARE(event.percent, 50);
        QCOMPARE(event.text, QString("Erasing 50%"));

        event = parser.feed(R"({"event":"progress","op":"program","bytes":300})");
        QCOMPARE(event.percent, 80);
        QCOMPARE(event.text, QString("Programming 80%"));

        event = parser.feed(R"({"event":"progress","op":"verify","bytes":100})");
        QCOMPARE(event.percent, 90);
        QCOMPARE(event.text, QString("Verifying 90%"));
    }

    void anOperationAnnouncedTwiceAddsUpItsTotals() {
        // One announcement per flash area.
        FlashProgressParser parser;
        parser.feed(R"({"event":"plan","op":"program","total":100})");
        parser.feed(R"({"event":"plan","op":"program","total":100})");
        const auto event = parser.feed(R"({"event":"progress","op":"program","bytes":50})");
        QCOMPARE(event.percent, 25);
    }

    void moreBytesThanAnnouncedAreNotCounted() {
        FlashProgressParser parser;
        parser.feed(R"({"event":"plan","op":"erase","total":100})");
        parser.feed(R"({"event":"plan","op":"program","total":100})");
        parser.feed(R"({"event":"progress","op":"erase","bytes":300})");
        const auto event = parser.feed(R"({"event":"progress","op":"program","bytes":0})");
        QCOMPARE(event.percent, 50);  // the erase counts as 100 of 200, not 300
    }

    void progressWithoutAnAnnouncedTotalShowsNothing() {
        FlashProgressParser parser;
        const auto event = parser.feed(R"({"event":"progress","op":"erase","bytes":100})");
        QCOMPARE(event.kind, FlashHelperEvent::Kind::Ignored);
    }

    void aMassEraseHasNoTotal() {
        FlashProgressParser parser;
        parser.feed(R"({"event":"plan","op":"erase","total":null})");
        const auto event = parser.feed(R"({"event":"started","op":"erase"})");
        QCOMPARE(event.kind, FlashHelperEvent::Kind::Ignored);
    }

    void readsTheResult() {
        FlashProgressParser parser;
        auto event = parser.feed(R"({"event":"result","ok":true})");
        QCOMPARE(event.kind, FlashHelperEvent::Kind::Result);
        QVERIFY(event.ok);

        event = parser.feed(R"({"event":"result","ok":false,"error":"cannot connect to X"})");
        QCOMPARE(event.kind, FlashHelperEvent::Kind::Result);
        QVERIFY(!event.ok);
        QCOMPARE(event.text, QString("cannot connect to X"));
    }

    void readsAMessageFromTheFlashAlgorithm() {
        FlashProgressParser parser;
        const auto event = parser.feed(R"({"event":"message","text":"hello"})");
        QCOMPARE(event.kind, FlashHelperEvent::Kind::Message);
        QCOMPARE(event.text, QString("hello"));
    }

    void ignoresLinesThatAreNotJson() {
        FlashProgressParser parser;
        QCOMPARE(parser.feed("thread 'main' panicked at src/main.rs").kind, FlashHelperEvent::Kind::Ignored);
        QCOMPARE(parser.feed("").kind, FlashHelperEvent::Kind::Ignored);
        QCOMPARE(parser.feed("[1,2,3]").kind, FlashHelperEvent::Kind::Ignored);
    }

    void resetForgetsThePlan() {
        FlashProgressParser parser;
        parser.feed(R"({"event":"plan","op":"erase","total":100})");
        parser.reset();
        const auto event = parser.feed(R"({"event":"progress","op":"erase","bytes":50})");
        QCOMPARE(event.kind, FlashHelperEvent::Kind::Ignored);
    }
};

QTEST_APPLESS_MAIN(TestFlashProgressParser)
#include "tst_flashprogressparser.moc"
