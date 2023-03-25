// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <optional>

#include <QBrush>
#include <QString>
#include <QTimer>
#include <QWidget>

#include "Core/Core.h"
#include "Core/IOS/USB/Emulated/Skylander.h"

class QCheckBox;
class QGroupBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QListWidget;

struct Skylander
{
  u8 portal_slot;
  u16 sky_id;
  u16 sky_var;
};

class SkylanderPortalWindow : public QWidget
{
  Q_OBJECT
public:
  explicit SkylanderPortalWindow(QWidget* parent = nullptr);
  ~SkylanderPortalWindow() override;

protected:
  std::array<QLineEdit*, MAX_SKYLANDERS> m_edit_skylanders;
  std::array<std::optional<Skylander>, MAX_SKYLANDERS> m_sky_slots;

private:
  // window
  void CreateMainWindow();
  QGroupBox* CreatePortalGroup();
  QGroupBox* CreateSearchGroup();
  void closeEvent(QCloseEvent* bar) override;
  bool eventFilter(QObject* object, QEvent* event) final override;

  // user interface
  void EmulatePortal(bool emulate);
  void SelectCollectionPath();
  void LoadSelected();
  void LoadFromFile();
  void ClearSlot(u8 slot);
  void CreateSkylanderAdvanced();

  // behind the scenes
  void OnEmulationStateChanged(Core::State state);
  void OnCollectionPathChanged();
  void RefreshList();
  void UpdateCurrentIDs();
  void CreateSkyfile(const QString& path, bool loadAfter);
  void LoadSkyfilePath(u8 slot, const QString& path);
  void UpdateSlotNames();

  // helpers
  bool PassesFilter(QString name, u16 id, u16 var);
  QString GetFilePath(u16 id, u16 var);
  u8 GetCurrentSlot();
  int GetElementRadio();
  QBrush GetBaseColor(std::pair<const u16, const u16> ids);

  bool m_emulating;
  QCheckBox* m_enabled_checkbox;
  QGroupBox* m_group_skylanders;
  QGroupBox* m_command_buttons;
  QRadioButton* m_slot_radios[16];

  // Qt is not guaranteed to keep track of file paths using native file pickers, so we use this
  // variable to ensure we open at the most recent Skylander file location
  QString m_last_skylander_path;

  QString m_collection_path;
  QLineEdit* m_path_edit;
  QPushButton* m_path_select;

  QCheckBox* m_game_filters[5];
  QRadioButton* m_element_filter[10];
  QCheckBox* m_only_show_collection;
  QLineEdit* m_sky_search;
  QListWidget* m_skylander_list;
  u16 sky_id;
  u16 sky_var;
};
