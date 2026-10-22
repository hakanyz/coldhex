#pragma once

#include <QComboBox>
#include <QStringList>

// A drop-down of named profiles. The first row saves the current settings as a new profile; the
// other rows are profiles. Hovering a profile shows a delete button on its right; the selected one
// also has rename and save (replace its contents with the current settings).
class ProfileCombo : public QComboBox {
    Q_OBJECT

public:
    // What the "replace with current settings" button of a profile shows.
    enum class SaveState {
        Unchanged,  // red: the settings equal what the profile holds
        Changed,    // blue: they differ, saving would change the profile
        Saved       // green: just saved; back to normal the next time the list opens
    };

    explicit ProfileCombo(QWidget* parent = nullptr);

    // Fills the list. `current` is shown as selected; empty (or unknown) shows "No profile".
    void setProfiles(const QStringList& names, const QString& current);

    // Selects another profile without rebuilding the list (the list may be open).
    void selectProfile(const QString& name);

    // Adds " *" to the shown profile's name while its settings were changed.
    void setModified(bool modified);

    // Shows the save button of `name` green until the list is opened again.
    void markSaved(const QString& name);

    // The profile shown as selected, empty when none. Only that one has the rename and save buttons.
    QString currentProfile() const { return m_current; }

    SaveState saveStateFor(const QString& name) const;

    void showPopup() override;

signals:
    void profileChosen(const QString& name);
    void saveRequested();
    void renameRequested(const QString& name);
    void updateRequested(const QString& name);
    void deleteRequested(const QString& name);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QString m_current;
    QString m_savedName;
    bool m_modified = false;
};
