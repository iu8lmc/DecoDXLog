// DecoDXLog — ranking DX spiegabile, senza effetti collaterali radio.
#pragma once

#include <QDateTime>
#include <QList>
#include <QStringList>

namespace decolog::core::dxassistant {

struct Candidate {
    QString call;
    QString band;
    QString mode;
    QDateTime spottedAt;
    int status{0};
    int propagationQuality{-1}; // -1 = previsione assente, 0..3
    bool hasPropagation{false};
    QString propagationEngine;
    bool hasHistory{false};
    bool worked{false};
    bool confirmed{false};
    bool radioAvailable{true};
    bool antennaAvailable{true};
    QString radio;
    QString antenna;
    bool observed{true}; // false means forecast/inferred, never treated as a fact
    bool historical{false};
    QString awardAccount;
    QString source;
    QString spotKey;
    double frequencyKhz{0.0};
};

struct Preferences {
    int newEntityWeight{40};
    int newBandWeight{25};
    int newModeWeight{15};
    int newReferenceWeight{20};
    int propagationWeight{20};
    int freshnessMinutes{30};
    bool rejectStale{false};
    double stalePenalty{0.5};
    QString awardAccount;
    bool prioritizeWorkedUnconfirmed{true};
    bool requireFeasible{false};
    QStringList availableBands;
    QStringList availableModes;
    QStringList availableRadios;
    QStringList availableAntennas;
};

struct Recommendation {
    Candidate candidate;
    double score{0.0};
    QStringList reasons;
    QStringList limitations;
    QString evidence; // observed, historical, or forecast/inferred
};

// Classifica esclusivamente candidati forniti dal chiamante. Non esegue CAT,
// non modifica il log e non trasmette.
QList<Recommendation> rank(const QList<Candidate>& candidates,
                           const Preferences& preferences = {},
                           const QDateTime& now = QDateTime::currentDateTimeUtc());

} // namespace decolog::core::dxassistant
