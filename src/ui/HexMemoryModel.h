#pragma once

#include <QAbstractTableModel>
#include <QByteArray>
#include <cstdint>

class HexMemoryModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit HexMemoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setMemoryData(uint32_t baseAddress, const QByteArray &data);
    void clear();

    uint32_t baseAddress() const { return m_baseAddress; }
    int dataSize() const { return m_data.size(); }

private:
    uint32_t m_baseAddress = 0x08000000;
    QByteArray m_data;
};
