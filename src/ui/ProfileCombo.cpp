#include "ProfileCombo.h"
#include <QAbstractItemView>
#include <QHelpEvent>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QToolTip>

namespace {
constexpr int kNameRole = Qt::UserRole;
constexpr int kIsProfileRole = Qt::UserRole + 1;
constexpr int kButtonSize = 18;
constexpr int kButtonGap = 4;
constexpr int kButtonMargin = 6;

QRect deleteRect(const QRect& row) {
    return QRect(row.right() - kButtonMargin - kButtonSize, row.center().y() - kButtonSize / 2 + 1,
                 kButtonSize, kButtonSize);
}

QRect updateRect(const QRect& row) {
    return deleteRect(row).translated(-kButtonSize - kButtonGap, 0);
}

QRect renameRect(const QRect& row) {
    return updateRect(row).translated(-kButtonSize - kButtonGap, 0);
}

// Draws the item as the drop-down normally does (`base`) and adds the buttons on a hovered profile.
class ProfileItemDelegate : public QAbstractItemDelegate {
public:
    ProfileItemDelegate(const ProfileCombo* combo, QAbstractItemDelegate* base)
        : QAbstractItemDelegate(const_cast<ProfileCombo*>(combo)), m_combo(combo), m_base(base),
          m_unchanged(":/icons/profile/update-unchanged.svg"), m_changed(":/icons/profile/update-changed.svg"),
          m_saved(":/icons/profile/update-saved.svg"), m_rename(":/icons/profile/rename.svg"),
          m_delete(":/icons/profile/delete.svg") {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        m_base->paint(painter, option, index);
        if (!index.data(kIsProfileRole).toBool() || !(option.state & QStyle::State_MouseOver)) return;

        const QIcon* save = &m_changed;
        switch (m_combo->saveStateFor(index.data(kNameRole).toString())) {
        case ProfileCombo::SaveState::Unchanged: save = &m_unchanged; break;
        case ProfileCombo::SaveState::Saved: save = &m_saved; break;
        case ProfileCombo::SaveState::Changed: break;
        }
        // Rename and save only make sense for the selected profile.
        if (isCurrent(index)) {
            m_rename.paint(painter, renameRect(option.rect));
            save->paint(painter, updateRect(option.rect));
        }
        m_delete.paint(painter, deleteRect(option.rect));
    }

    bool isCurrent(const QModelIndex& index) const {
        return index.data(kNameRole).toString() == m_combo->currentProfile();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        return m_base->sizeHint(option, index);
    }

    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                   const QModelIndex& index) override {
        if (event->type() == QEvent::ToolTip && index.data(kIsProfileRole).toBool()) {
            QString text;
            if (isCurrent(index) && renameRect(option.rect).contains(event->pos())) {
                text = "Rename this profile";
            } else if (isCurrent(index) && updateRect(option.rect).contains(event->pos())) {
                switch (m_combo->saveStateFor(index.data(kNameRole).toString())) {
                case ProfileCombo::SaveState::Unchanged: text = "Nothing changed since it was loaded"; break;
                case ProfileCombo::SaveState::Changed: text = "Save the current settings to this profile"; break;
                case ProfileCombo::SaveState::Saved: text = "Saved"; break;
                }
            } else if (deleteRect(option.rect).contains(event->pos())) {
                text = "Delete this profile";
            }
            if (!text.isEmpty()) {
                QToolTip::showText(event->globalPos(), text, view);
                return true;
            }
        }
        return m_base->helpEvent(event, view, option, index);
    }

private:
    const ProfileCombo* m_combo;
    QAbstractItemDelegate* m_base;  // owned by the list view
    QIcon m_unchanged;
    QIcon m_changed;
    QIcon m_saved;
    QIcon m_rename;
    QIcon m_delete;
};
}

ProfileCombo::ProfileCombo(QWidget* parent)
    : QComboBox(parent)
{
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(12);
    setPlaceholderText("No profile");

    view()->setItemDelegate(new ProfileItemDelegate(this, view()->itemDelegate()));
    view()->setMouseTracking(true);
    // Installed after the drop-down's own filter, so it sees a click on a button first.
    view()->viewport()->installEventFilter(this);

    // Only a choice made by the user counts (activated, not currentIndexChanged).
    connect(this, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (!itemData(index, kIsProfileRole).toBool()) {
            emit saveRequested();
            setCurrentIndex(m_current.isEmpty() ? -1 : findData(m_current, kNameRole));
            return;
        }
        const QString name = itemData(index, kNameRole).toString();
        m_current = name;
        emit profileChosen(name);
    });

    setProfiles({}, QString());
}

void ProfileCombo::setProfiles(const QStringList& names, const QString& current) {
    const QSignalBlocker block(this);
    clear();
    addItem("Save current settings as profile...");
    for (const QString& name : names) {
        addItem(name);
        setItemData(count() - 1, name, kNameRole);
        setItemData(count() - 1, true, kIsProfileRole);
    }
    m_current = names.contains(current) ? current : QString();
    m_modified = false;
    setCurrentIndex(m_current.isEmpty() ? -1 : findData(m_current, kNameRole));
}

void ProfileCombo::selectProfile(const QString& name) {
    const QSignalBlocker block(this);
    for (int i = 0; i < count(); ++i) {
        if (itemData(i, kIsProfileRole).toBool()) setItemText(i, itemData(i, kNameRole).toString());
    }
    m_current = name;
    m_modified = false;
    setCurrentIndex(findData(name, kNameRole));
}

void ProfileCombo::setModified(bool modified) {
    m_modified = modified;
    const int index = currentIndex();
    if (index < 0 || !itemData(index, kIsProfileRole).toBool()) return;
    const QString name = itemData(index, kNameRole).toString();
    setItemText(index, modified ? name + " *" : name);
    setToolTip(modified ? "Changed since it was loaded. Open the list, hover the profile and press the save button to keep the changes." : QString());
}

void ProfileCombo::markSaved(const QString& name) {
    m_savedName = name;
    view()->viewport()->update();
}

ProfileCombo::SaveState ProfileCombo::saveStateFor(const QString& name) const {
    if (name == m_savedName) return SaveState::Saved;
    return m_modified ? SaveState::Changed : SaveState::Unchanged;
}

// Opens below the box (the default would put the selected row over it).
void ProfileCombo::showPopup() {
    m_savedName.clear();
    QComboBox::showPopup();
    if (QWidget* popup = view()->window()) {
        popup->move(mapToGlobal(QPoint(0, height())));
    }
}

bool ProfileCombo::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view()->viewport() && event->type() == QEvent::MouseButtonRelease) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const QModelIndex index = view()->indexAt(mouse->pos());
        if (index.isValid() && index.data(kIsProfileRole).toBool()) {
            const QRect row = view()->visualRect(index);
            const QString name = index.data(kNameRole).toString();
            const bool current = name == m_current;
            if (current && renameRect(row).contains(mouse->pos())) {
                hidePopup();
                emit renameRequested(name);
                return true;
            }
            if (current && updateRect(row).contains(mouse->pos())) {
                // The list stays open, so the button can turn green.
                emit updateRequested(name);
                return true;
            }
            if (deleteRect(row).contains(mouse->pos())) {
                hidePopup();
                emit deleteRequested(name);
                return true;
            }
        }
    }
    return QComboBox::eventFilter(watched, event);
}
