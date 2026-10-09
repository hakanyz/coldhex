#include "HexMemoryModel.h"
#include <QFont>
#include <QColor>

HexMemoryModel::HexMemoryModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    // Initialize with default 256 bytes empty memory (0xFF)
    m_data = QByteArray(256, static_cast<char>(0xFF));
}

int HexMemoryModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    if (m_data.isEmpty()) return 16;
    return (m_data.size() + 15) / 16;
}

int HexMemoryModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    // 0: Address, 1..16: 00..0F, 17: ASCII
    return 18;
}

QVariant HexMemoryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return QVariant();

    int row = index.row();
    int col = index.column();

    if (role == Qt::DisplayRole) {
        if (col == 0) {
            uint32_t addr = m_baseAddress + (row * 16);
            return QString("0x%1").arg(addr, 8, 16, QLatin1Char('0')).toUpper();
        }

        if (col >= 1 && col <= 16) {
            int byteIndex = (row * 16) + (col - 1);
            if (byteIndex < m_data.size()) {
                uint8_t byteVal = static_cast<uint8_t>(m_data.at(byteIndex));
                return QString("%1").arg(byteVal, 2, 16, QLatin1Char('0')).toUpper();
            }
            return QString("--");
        }

        if (col == 17) {
            QString ascii;
            ascii.reserve(16);
            for (int i = 0; i < 16; ++i) {
                int byteIndex = (row * 16) + i;
                if (byteIndex < m_data.size()) {
                    uint8_t byteVal = static_cast<uint8_t>(m_data.at(byteIndex));
                    if (byteVal >= 32 && byteVal <= 126) {
                        ascii.append(QChar(byteVal));
                    } else {
                        ascii.append('.');
                    }
                } else {
                    ascii.append(' ');
                }
            }
            return ascii;
        }
    }

    if (role == Qt::TextAlignmentRole) {
        if (col == 0 || col == 17) {
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
        return static_cast<int>(Qt::AlignCenter);
    }

    return QVariant();
}

QVariant HexMemoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) return QVariant();

    if (orientation == Qt::Horizontal) {
        if (section == 0) return "Address";
        if (section >= 1 && section <= 16) {
            return QString("%1").arg(section - 1, 2, 16, QLatin1Char('0')).toUpper();
        }
        if (section == 17) return "ASCII";
    }

    return QVariant();
}

void HexMemoryModel::setMemoryData(uint32_t baseAddress, const QByteArray &data) {
    beginResetModel();
    m_baseAddress = baseAddress;
    m_data = data;
    endResetModel();
}

void HexMemoryModel::clear() {
    beginResetModel();
    m_baseAddress = 0x08000000;
    m_data = QByteArray(256, static_cast<char>(0xFF));
    endResetModel();
}
