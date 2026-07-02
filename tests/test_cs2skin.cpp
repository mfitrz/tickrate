#include <QtTest>
#include "network/OpenSkinClient.h"
#include "model/CS2Types.h"

class TestCS2Skin : public QObject
{
    Q_OBJECT

private slots:
    // stripWear
    void stripWear_factoryNew();
    void stripWear_minimalWear();
    void stripWear_fieldTested();
    void stripWear_wellWorn();
    void stripWear_battleScarred();
    void stripWear_noSuffix_unchanged();
    void stripWear_partialSuffix_unchanged();
    void stripWear_emptyString();
    void stripWear_multiWordBaseName();

    // wearIndex
    void wearIndex_factoryNew_isZero();
    void wearIndex_minimalWear_isOne();
    void wearIndex_fieldTested_isTwo();
    void wearIndex_wellWorn_isThree();
    void wearIndex_battleScarred_isFour();
    void wearIndex_noSuffix_isMinusOne();
    void wearIndex_emptyString_isMinusOne();

    // wear constants
    void wearCount_isFive();
    void wearShort_matchesExpected();
};

// ── stripWear ─────────────────────────────────────────────────────────────────

void TestCS2Skin::stripWear_factoryNew()
{
    QCOMPARE(OpenSkinClient::stripWear("AK-47 | Redline (Factory New)"),
             QString("AK-47 | Redline"));
}

void TestCS2Skin::stripWear_minimalWear()
{
    QCOMPARE(OpenSkinClient::stripWear("AWP | Dragon Lore (Minimal Wear)"),
             QString("AWP | Dragon Lore"));
}

void TestCS2Skin::stripWear_fieldTested()
{
    QCOMPARE(OpenSkinClient::stripWear("M4A4 | Howl (Field-Tested)"),
             QString("M4A4 | Howl"));
}

void TestCS2Skin::stripWear_wellWorn()
{
    QCOMPARE(OpenSkinClient::stripWear("Glock-18 | Fade (Well-Worn)"),
             QString("Glock-18 | Fade"));
}

void TestCS2Skin::stripWear_battleScarred()
{
    QCOMPARE(OpenSkinClient::stripWear("USP-S | Kill Confirmed (Battle-Scarred)"),
             QString("USP-S | Kill Confirmed"));
}

void TestCS2Skin::stripWear_noSuffix_unchanged()
{
    QCOMPARE(OpenSkinClient::stripWear("AK-47 | Redline"),
             QString("AK-47 | Redline"));
}

void TestCS2Skin::stripWear_partialSuffix_unchanged()
{
    // Contains a wear word but not as a proper trailing suffix
    QCOMPARE(OpenSkinClient::stripWear("Factory New Skin"),
             QString("Factory New Skin"));
}

void TestCS2Skin::stripWear_emptyString()
{
    QCOMPARE(OpenSkinClient::stripWear(""), QString(""));
}

void TestCS2Skin::stripWear_multiWordBaseName()
{
    QCOMPARE(OpenSkinClient::stripWear("Desert Eagle | Blaze (Factory New)"),
             QString("Desert Eagle | Blaze"));
}

// ── wearIndex ────────────────────────────────────────────────────────────────

void TestCS2Skin::wearIndex_factoryNew_isZero()
{
    QCOMPARE(OpenSkinClient::wearIndex("AK-47 | Redline (Factory New)"), 0);
}

void TestCS2Skin::wearIndex_minimalWear_isOne()
{
    QCOMPARE(OpenSkinClient::wearIndex("AWP | Dragon Lore (Minimal Wear)"), 1);
}

void TestCS2Skin::wearIndex_fieldTested_isTwo()
{
    QCOMPARE(OpenSkinClient::wearIndex("M4A4 | Howl (Field-Tested)"), 2);
}

void TestCS2Skin::wearIndex_wellWorn_isThree()
{
    QCOMPARE(OpenSkinClient::wearIndex("Glock-18 | Fade (Well-Worn)"), 3);
}

void TestCS2Skin::wearIndex_battleScarred_isFour()
{
    QCOMPARE(OpenSkinClient::wearIndex("USP-S | Kill Confirmed (Battle-Scarred)"), 4);
}

void TestCS2Skin::wearIndex_noSuffix_isMinusOne()
{
    QCOMPARE(OpenSkinClient::wearIndex("AK-47 | Redline"), -1);
}

void TestCS2Skin::wearIndex_emptyString_isMinusOne()
{
    QCOMPARE(OpenSkinClient::wearIndex(""), -1);
}

// ── wear constants ────────────────────────────────────────────────────────────

void TestCS2Skin::wearCount_isFive()
{
    QCOMPARE(kWearCount, 5);
}

void TestCS2Skin::wearShort_matchesExpected()
{
    QCOMPARE(QString(kWearShort[0]), QString("FN"));
    QCOMPARE(QString(kWearShort[1]), QString("MW"));
    QCOMPARE(QString(kWearShort[2]), QString("FT"));
    QCOMPARE(QString(kWearShort[3]), QString("WW"));
    QCOMPARE(QString(kWearShort[4]), QString("BS"));
}

QTEST_MAIN(TestCS2Skin)
#include "test_cs2skin.moc"
