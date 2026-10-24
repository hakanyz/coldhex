#include "ProfileTransfer.h"
#include "SlotValidator.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {
const QString kFormat = QStringLiteral("coldhex-profiles");
constexpr int kMaxTextLength = 100;
constexpr int kMaxPathLength = 1024;
constexpr int kMaxSpeedKhz = 100000;

QString firmwareToText(FirmwareChoice c) {
    switch (c) {
    case FirmwareChoice::File1: return QStringLiteral("file1");
    case FirmwareChoice::Both: return QStringLiteral("both");
    case FirmwareChoice::File2: break;
    }
    return QStringLiteral("file2");
}

QString pathForExport(const QString& path, const QString& baseDir) {
    if (path.isEmpty()) return path;
    if (!baseDir.isEmpty()) {
        const QString relative = QDir(baseDir).relativeFilePath(path);
        if (!relative.startsWith(QLatin1String("..")) && !QDir::isAbsolutePath(relative)) {
            return QDir::fromNativeSeparators(relative);
        }
    }
    return QDir::fromNativeSeparators(path);
}

bool hasControlCharacters(const QString& text) {
    for (const QChar c : text) {
        if (c.unicode() < 0x20) return true;
    }
    return false;
}

// Reads one profile. Returns an empty string when it is fine, otherwise the reason it is refused.
QString readProfile(const QJsonObject& o, const QString& baseDir, Profile& p) {
    auto text = [&](const char* key, int maxLength, QString& out) -> QString {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined() || v.isNull()) { out.clear(); return QString(); }
        if (!v.isString()) return QStringLiteral("'%1' must be text").arg(QLatin1String(key));
        out = v.toString().trimmed();
        if (out.size() > maxLength) return QStringLiteral("'%1' is too long").arg(QLatin1String(key));
        if (hasControlCharacters(out)) return QStringLiteral("'%1' has control characters").arg(QLatin1String(key));
        return QString();
    };
    auto flag = [&](const char* key, bool& out) -> QString {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined() || v.isNull()) return QString();
        if (!v.isBool()) return QStringLiteral("'%1' must be true or false").arg(QLatin1String(key));
        out = v.toBool();
        return QString();
    };

    QString err;
    if (!(err = text("manufacturer", kMaxTextLength, p.manufacturer)).isEmpty()) return err;
    if (!(err = text("chip", kMaxTextLength, p.chip)).isEmpty()) return err;
    if (p.chip.isEmpty()) return QStringLiteral("no chip");

    QString iface;
    if (!(err = text("interface", kMaxTextLength, iface)).isEmpty()) return err;
    iface = iface.toUpper();
    if (iface.isEmpty()) iface = QStringLiteral("SWD");
    if (iface != QLatin1String("SWD") && iface != QLatin1String("JTAG")) return QStringLiteral("interface must be SWD or JTAG");
    p.interfaceName = iface;

    const QJsonValue speed = o.value(QStringLiteral("speedKhz"));
    if (!speed.isUndefined() && !speed.isNull()) {
        const double d = speed.toDouble(-1);
        if (!speed.isDouble() || d < 1 || d > kMaxSpeedKhz || d != static_cast<int>(d)) {
            return QStringLiteral("'speedKhz' must be a whole number between 1 and %1").arg(kMaxSpeedKhz);
        }
        p.speedKhz = static_cast<int>(d);
    }

    for (const char* key : {"slot1Address", "slot2Address"}) {
        QString address;
        if (!(err = text(key, kMaxTextLength, address)).isEmpty()) return err;
        if (!address.isEmpty()) {
            uint32_t value = 0;
            if (!SlotValidator::parseAddress(address, value)) return QStringLiteral("'%1' is not a hexadecimal address").arg(QLatin1String(key));
            address = QStringLiteral("0x") + QString::number(value, 16).rightJustified(8, QLatin1Char('0')).toUpper();
        }
        (QLatin1String(key) == QLatin1String("slot1Address") ? p.slot1Address : p.slot2Address) = address;
    }

    for (const char* key : {"slot1Path", "slot2Path"}) {
        QString path;
        if (!(err = text(key, kMaxPathLength, path)).isEmpty()) return err;
        if (!path.isEmpty()) {
            path = QDir::fromNativeSeparators(path);
            if (QDir::isRelativePath(path) && !baseDir.isEmpty()) path = QDir::cleanPath(baseDir + QLatin1Char('/') + path);
        }
        (QLatin1String(key) == QLatin1String("slot1Path") ? p.slot1Path : p.slot2Path) = path;
    }

    QString firmware;
    if (!(err = text("firmware", kMaxTextLength, firmware)).isEmpty()) return err;
    if (firmware.isEmpty() || firmware == QLatin1String("file2")) p.firmware = FirmwareChoice::File2;
    else if (firmware == QLatin1String("file1")) p.firmware = FirmwareChoice::File1;
    else if (firmware == QLatin1String("both")) p.firmware = FirmwareChoice::Both;
    else return QStringLiteral("'firmware' must be file1, file2 or both");

    if (!(err = flag("eraseBeforeFlash", p.eraseBeforeFlash)).isEmpty()) return err;
    if (!(err = flag("verifyAfterFlash", p.verifyAfterFlash)).isEmpty()) return err;
    if (!(err = flag("resetAfterFlash", p.resetAfterFlash)).isEmpty()) return err;
    if (!(err = flag("connectUnderReset", p.connectUnderReset)).isEmpty()) return err;
    return QString();
}
}

QByteArray ProfileTransfer::toJson(const QList<NamedProfile>& profiles, const QString& appVersion,
                                   bool includePaths, const QString& baseDir) {
    QJsonArray array;
    for (const NamedProfile& n : profiles) {
        const Profile& p = n.profile;
        QJsonObject o;
        o["name"] = n.name;
        o["manufacturer"] = p.manufacturer;
        o["chip"] = p.chip;
        o["interface"] = p.interfaceName;
        o["speedKhz"] = p.speedKhz;
        o["slot1Path"] = includePaths ? pathForExport(p.slot1Path, baseDir) : QString();
        o["slot2Path"] = includePaths ? pathForExport(p.slot2Path, baseDir) : QString();
        o["slot1Address"] = p.slot1Address;
        o["slot2Address"] = p.slot2Address;
        o["firmware"] = firmwareToText(p.firmware);
        o["eraseBeforeFlash"] = p.eraseBeforeFlash;
        o["verifyAfterFlash"] = p.verifyAfterFlash;
        o["resetAfterFlash"] = p.resetAfterFlash;
        o["connectUnderReset"] = p.connectUnderReset;
        array.append(o);
    }

    QJsonObject root;
    root["format"] = kFormat;
    root["version"] = kFormatVersion;
    root["app"] = appVersion;
    root["profiles"] = array;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

ImportResult ProfileTransfer::fromJson(const QByteArray& data, const QString& baseDir) {
    ImportResult result;
    if (data.size() > kMaxFileBytes) {
        result.error = QStringLiteral("The file is too large for a profile file.");
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = QStringLiteral("This is not a valid profile file (it is not readable JSON).");
        return result;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != kFormat) {
        result.error = QStringLiteral("This is not a ColdHex profile file.");
        return result;
    }
    const int version = root.value(QStringLiteral("version")).toInt(0);
    if (version < 1) {
        result.error = QStringLiteral("This profile file has no valid format version.");
        return result;
    }
    if (version > kFormatVersion) {
        result.error = QStringLiteral("This file was made by a newer ColdHex (file format %1). Update ColdHex to read it.").arg(version);
        return result;
    }
    if (!root.value(QStringLiteral("profiles")).isArray()) {
        result.error = QStringLiteral("The file has no list of profiles.");
        return result;
    }

    const QJsonArray array = root.value(QStringLiteral("profiles")).toArray();
    if (array.isEmpty()) {
        result.error = QStringLiteral("The file contains no profiles.");
        return result;
    }

    QSet<QString> seen;
    int index = 0;
    for (const QJsonValue& value : array) {
        ++index;
        if (result.profiles.size() >= kMaxProfiles) {
            result.skipped << QStringLiteral("Only the first %1 profiles are read.").arg(kMaxProfiles);
            break;
        }
        if (!value.isObject()) {
            result.skipped << QStringLiteral("Entry %1: not a profile").arg(index);
            continue;
        }
        const QJsonObject o = value.toObject();
        const QJsonValue nameValue = o.value(QStringLiteral("name"));
        const QString name = nameValue.toString().trimmed();
        if (!nameValue.isString() || !ProfileStore::isValidName(name) || name.size() > kMaxTextLength || hasControlCharacters(name)) {
            result.skipped << QStringLiteral("Entry %1: the name is missing or not allowed").arg(index);
            continue;
        }
        if (seen.contains(name.toLower())) {
            result.skipped << QStringLiteral("'%1': the name appears more than once, only the first is used").arg(name);
            continue;
        }

        Profile profile;
        const QString problem = readProfile(o, baseDir, profile);
        if (!problem.isEmpty()) {
            result.skipped << QStringLiteral("'%1': %2").arg(name, problem);
            continue;
        }
        seen.insert(name.toLower());
        result.profiles.append({name, profile});
    }

    if (result.profiles.isEmpty()) {
        result.error = QStringLiteral("None of the profiles in the file can be used.");
    }
    return result;
}

QString ProfileTransfer::uniqueName(const QString& base, const QStringList& taken) {
    auto isTaken = [&](const QString& candidate) {
        for (const QString& t : taken) {
            if (t.compare(candidate, Qt::CaseInsensitive) == 0) return true;
        }
        return false;
    };
    if (!isTaken(base)) return base;
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(n);
        if (!isTaken(candidate)) return candidate;
    }
}
