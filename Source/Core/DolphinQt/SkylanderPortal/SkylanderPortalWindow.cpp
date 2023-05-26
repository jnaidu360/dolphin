// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// DolphinQt code copied and modified for Dolphin from the RPCS3 Qt utility for Creating, Loading
// and Clearing Skylanders

#include "DolphinQt/SkylanderPortal/SkylanderPortalWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGroupBox>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVBoxLayout>

#include "Common/IOFile.h"

#include "Common/FileUtil.h"
#include "Core/Config/MainSettings.h"
#include "Core/IOS/USB/Emulated/Skylander.h"
#include "Core/System.h"
#include "DolphinQt/MainWindow.h"
#include "DolphinQt/RenderWidget.h"

#include "DolphinQt/QtUtils/DolphinFileDialog.h"
#include "DolphinQt/Resources.h"
#include "DolphinQt/Settings.h"

PortalButton::PortalButton(RenderWidget* rend, QWidget* pWindow, QWidget* parent) : QWidget(parent)
{
  SetRender(rend);
  portal_window = pWindow;

  setWindowTitle(tr("Portal Button"));
  setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
  setParent(0);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setAttribute(Qt::WA_TranslucentBackground, true);

  button = new QPushButton(tr("Portal of Power"), this);
  button->resize(100, 50);
  connect(button, &QAbstractButton::clicked, this, [this]() { OpenMenu(); });
  fade_out.callOnTimeout(this, &PortalButton::hide);

  move(100, 150);
}

PortalButton::~PortalButton() = default;

void PortalButton::SetEnabled(bool enable)
{
  enabled = enable;
  render->SetReportMouseMovement(enable);
  hide();
}

void PortalButton::OpenMenu()
{
  portal_window->show();
  portal_window->raise();
  portal_window->activateWindow();
}

void PortalButton::SetRender(RenderWidget* r)
{
  if (render != nullptr)
  {
    disconnect(render, &RenderWidget::MouseMoved, this, &PortalButton::Hovered);
  }
  render = r;
  connect(render, &RenderWidget::MouseMoved, this, &PortalButton::Hovered, Qt::DirectConnection);
}

void PortalButton::Hovered()
{
  if (enabled)
  {
    show();
    raise();
    fade_out.start(1000);
  }
}

SkylanderPortalWindow::SkylanderPortalWindow(RenderWidget* render, const MainWindow* main,
                                             QWidget* parent)
    : QWidget(parent)
{
  setWindowTitle(tr("Skylanders Manager"));
  setWindowIcon(Resources::GetAppIcon());
  setObjectName(QString::fromStdString("skylanders_manager"));
  setMinimumSize(QSize(550, 400));

  m_only_show_collection = new QCheckBox(tr("Only Show Files in Collection"));

  CreateMainWindow();

  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this,
          &SkylanderPortalWindow::OnEmulationStateChanged);

  installEventFilter(this);

  OnEmulationStateChanged(Core::GetState());

  open_portal_btn = new PortalButton(render, this);
  connect(main, &MainWindow::RenderInstanceChanged, open_portal_btn, &PortalButton::SetRender);

  sky_id = 0;
  sky_var = 0;

  connect(m_skylander_list, &QListWidget::itemSelectionChanged, this,
          &SkylanderPortalWindow::UpdateCurrentIDs);

  QDir skylanders_folder;
  // skylanders folder in user directory
  QString user_path =
      QString::fromStdString(File::GetUserPath(D_USER_IDX)) + QString::fromStdString("Skylanders");
  // first time initialize path in config
  if (Config::Get(Config::MAIN_SKYLANDERS_PATH) == "")
  {
    Config::SetBase(Config::MAIN_SKYLANDERS_PATH, user_path.toStdString());
    skylanders_folder = QDir(user_path);
  }
  else
  {
    skylanders_folder = QDir(QString::fromStdString(Config::Get(Config::MAIN_SKYLANDERS_PATH)));
  }
  // prompt response if path invalid
  if (!skylanders_folder.exists())
  {
    QMessageBox::StandardButton createFolderResponse;
    createFolderResponse =
        QMessageBox::question(this, tr("Create Skylander Folder"),
                              tr("Skylanders folder not found for this user. Create new folder?"),
                              QMessageBox::Yes | QMessageBox::No);
    if (createFolderResponse == QMessageBox::Yes)
    {
      skylanders_folder = QDir(user_path);
      Config::SetBase(Config::MAIN_SKYLANDERS_PATH, user_path.toStdString());
      skylanders_folder.mkdir(skylanders_folder.path());
    }
  }

  m_collection_path = QDir::toNativeSeparators(skylanders_folder.path()) + QDir::separator();
  m_last_skylander_path = m_collection_path;
  m_path_edit->setText(m_collection_path);
};

SkylanderPortalWindow::~SkylanderPortalWindow() = default;

// window
void SkylanderPortalWindow::CreateMainWindow()
{
  auto* main_layout = new QVBoxLayout();

  auto* select_layout = new QHBoxLayout;

  select_layout->addWidget(CreatePortalGroup());
  select_layout->addWidget(CreateSearchGroup());

  main_layout->addLayout(select_layout);

  QBoxLayout* command_layout = new QHBoxLayout;
  command_layout->setAlignment(Qt::AlignCenter);
  auto* create_btn = new QPushButton(tr("Customize"));
  auto* load_file_btn = new QPushButton(tr("Load File"));
  auto* clear_btn = new QPushButton(tr("Clear Slot"));
  auto* load_btn = new QPushButton(tr("Load Slot"));
  connect(create_btn, &QAbstractButton::clicked, this,
          &SkylanderPortalWindow::CreateSkylanderAdvanced);
  connect(clear_btn, &QAbstractButton::clicked, this, [this]() { ClearSlot(GetCurrentSlot()); });
  connect(load_btn, &QAbstractButton::clicked, this, &SkylanderPortalWindow::LoadSelected);
  connect(load_file_btn, &QAbstractButton::clicked, this, &SkylanderPortalWindow::LoadFromFile);
  command_layout->addWidget(create_btn);
  command_layout->addWidget(load_file_btn);
  command_layout->addWidget(clear_btn);
  command_layout->addWidget(load_btn);
  m_command_buttons = new QGroupBox;
  m_command_buttons->setLayout(command_layout);
  main_layout->addWidget(m_command_buttons);

  setLayout(main_layout);

  RefreshList();
  m_skylander_list->setCurrentItem(m_skylander_list->item(0), QItemSelectionModel::Select);
  UpdateSlotNames();
}

QGroupBox* SkylanderPortalWindow::CreatePortalGroup()
{
  auto* slot_group = new QGroupBox();
  auto* slot_layout = new QVBoxLayout();

  auto* checkbox_group = new QGroupBox();
  auto* checkbox_layout = new QVBoxLayout();
  m_enabled_checkbox = new QCheckBox(tr("Emulate Skylander Portal"), this);
  m_enabled_checkbox->setChecked(Config::Get(Config::MAIN_EMULATE_SKYLANDER_PORTAL));
  m_emulating = Config::Get(Config::MAIN_EMULATE_SKYLANDER_PORTAL);
  m_show_button_ingame_checkbox = new QCheckBox(tr("Show Portal Button In-Game"), this);
#ifdef Q_OS_DARWIN
  m_show_button_ingame_checkbox->setEnabled(false);
#endif
  connect(m_enabled_checkbox, &QCheckBox::toggled, [&](bool checked) { EmulatePortal(checked); });
  connect(m_show_button_ingame_checkbox, &QCheckBox::toggled,
          [&](bool checked) { open_portal_btn->SetEnabled(checked); });
  checkbox_layout->addWidget(m_enabled_checkbox);
  checkbox_layout->addWidget(m_show_button_ingame_checkbox);
  checkbox_group->setLayout(checkbox_layout);
  slot_layout->addWidget(checkbox_group);

  auto add_line = [](QVBoxLayout* vbox) {
    auto* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    vbox->addWidget(line);
  };

  m_group_skylanders = new QGroupBox(tr("Portal Slots:"));
  auto* vbox_group = new QVBoxLayout();
  auto* scroll_area = new QScrollArea();

  for (auto i = 0; i < MAX_SKYLANDERS; i++)
  {
    if (i != 0)
    {
      add_line(vbox_group);
    }

    auto* hbox_skylander = new QHBoxLayout();
    auto* label_skyname = new QLabel(QString(tr("Skylander %1")).arg(i + 1));
    m_edit_skylanders[i] = new QLineEdit();
    m_edit_skylanders[i]->setEnabled(false);

    QRadioButton* button = new QRadioButton;
    m_slot_radios[i] = button;
    button->setProperty("id", i);
    hbox_skylander->addWidget(button);
    hbox_skylander->addWidget(label_skyname);
    hbox_skylander->addWidget(m_edit_skylanders[i]);

    vbox_group->addLayout(hbox_skylander);
  }
  m_slot_radios[0]->setChecked(true);

  m_group_skylanders->setLayout(vbox_group);
  scroll_area->setWidget(m_group_skylanders);
  scroll_area->setWidgetResizable(true);
  m_group_skylanders->setVisible(Config::Get(Config::MAIN_EMULATE_SKYLANDER_PORTAL));
  slot_layout->addWidget(scroll_area);

  slot_group->setLayout(slot_layout);
  slot_group->setMaximumWidth(350);

  return slot_group;
}

QGroupBox* SkylanderPortalWindow::CreateSearchGroup()
{
  m_skylander_list = new QListWidget;

  m_skylander_list->setMinimumWidth(200);
  connect(m_skylander_list, &QListWidget::itemDoubleClicked, this,
          &SkylanderPortalWindow::LoadSelected);

  auto* main_group = new QGroupBox();
  auto* main_layout = new QVBoxLayout();

  auto* header_group = new QGroupBox();
  auto* header_layout = new QHBoxLayout();

  header_layout->addWidget(new QLabel(tr("Skylander Collection Path:")));
  m_path_edit = new QLineEdit;
  header_layout->addWidget(m_path_edit);
  m_path_select = new QPushButton(tr("Choose"));
  connect(m_path_edit, &QLineEdit::editingFinished, this,
          &SkylanderPortalWindow::OnCollectionPathChanged);
  connect(m_path_select, &QAbstractButton::clicked, this,
          &SkylanderPortalWindow::SelectCollectionPath);
  header_layout->addWidget(m_path_select);

  header_group->setLayout(header_layout);
  main_layout->addWidget(header_group);

  auto* search_bar_layout = new QHBoxLayout;
  m_sky_search = new QLineEdit;
  m_sky_search->setClearButtonEnabled(true);
  connect(m_sky_search, &QLineEdit::textChanged, this, &SkylanderPortalWindow::RefreshList);
  search_bar_layout->addWidget(new QLabel(tr("Search:")));
  search_bar_layout->addWidget(m_sky_search);
  main_layout->addLayout(search_bar_layout);

  auto* search_group = new QGroupBox();
  auto* search_layout = new QHBoxLayout();

  auto* search_filters_group = new QGroupBox();
  auto* search_filters_layout = new QVBoxLayout();

  auto* search_checkbox_group = new QGroupBox(tr("Game"));
  auto* search_checkbox_layout = new QVBoxLayout();

  for (int i = 0; i < 5; i++)
  {
    QCheckBox* checkbox = new QCheckBox(this);
    checkbox->setChecked(true);
    connect(checkbox, &QCheckBox::toggled, this, &SkylanderPortalWindow::RefreshList);
    m_game_filters[i] = checkbox;
    search_checkbox_layout->addWidget(checkbox);
  }
  m_game_filters[IOS::HLE::USB::SPYROS_ADV]->setText(tr("Spyro's Adventure"));
  m_game_filters[IOS::HLE::USB::GIANTS]->setText(tr("Giants"));
  m_game_filters[IOS::HLE::USB::SWAP_FORCE]->setText(tr("Swap Force"));
  m_game_filters[IOS::HLE::USB::TRAP_TEAM]->setText(tr("Trap Team"));
  m_game_filters[IOS::HLE::USB::SUPERCHARGERS]->setText(tr("Superchargers"));
  search_checkbox_group->setLayout(search_checkbox_layout);
  search_filters_layout->addWidget(search_checkbox_group);

  auto* search_radio_group = new QGroupBox(tr("Element"));
  auto* search_radio_layout = new QHBoxLayout();

  auto* radio_layout_left = new QVBoxLayout();
  for (int i = 0; i < 10; i += 2)
  {
    QRadioButton* radio = new QRadioButton(this);
    radio->setProperty("id", i);
    connect(radio, &QRadioButton::toggled, this, &SkylanderPortalWindow::RefreshList);
    m_element_filter[i] = radio;
    radio_layout_left->addWidget(radio);
  }
  search_radio_layout->addLayout(radio_layout_left);

  auto* radio_layout_right = new QVBoxLayout();
  for (int i = 1; i < 10; i += 2)
  {
    QRadioButton* radio = new QRadioButton(this);
    radio->setProperty("id", i);
    connect(radio, &QRadioButton::toggled, this, &SkylanderPortalWindow::RefreshList);
    m_element_filter[i] = radio;
    radio_layout_right->addWidget(radio);
  }
  search_radio_layout->addLayout(radio_layout_right);

  m_element_filter[0]->setText(tr("All"));
  m_element_filter[0]->setChecked(true);
  m_element_filter[1]->setText(tr("Magic"));
  m_element_filter[2]->setText(tr("Water"));
  m_element_filter[3]->setText(tr("Tech"));
  m_element_filter[4]->setText(tr("Fire"));
  m_element_filter[5]->setText(tr("Earth"));
  m_element_filter[6]->setText(tr("Life"));
  m_element_filter[7]->setText(tr("Air"));
  m_element_filter[8]->setText(tr("Undead"));
  m_element_filter[9]->setText(tr("Other"));

  search_radio_group->setLayout(search_radio_layout);
  search_filters_layout->addWidget(search_radio_group);

  auto* other_box = new QGroupBox(tr("Other"));
  auto* other_layout = new QVBoxLayout;
  connect(m_only_show_collection, &QCheckBox::toggled, this, &SkylanderPortalWindow::RefreshList);
  other_layout->addWidget(m_only_show_collection);
  other_box->setLayout(other_layout);
  search_filters_layout->addWidget(other_box);

  search_filters_layout->addStretch(50);

  search_filters_group->setLayout(search_filters_layout);
  search_layout->addWidget(search_filters_group);

  search_layout->addWidget(m_skylander_list);

  search_group->setLayout(search_layout);
  main_layout->addWidget(search_group);

  main_group->setLayout(main_layout);

  return main_group;
}

bool SkylanderPortalWindow::eventFilter(QObject* object, QEvent* event)
{
  // Close when escape is pressed
  if (event->type() == QEvent::KeyPress)
  {
    if (static_cast<QKeyEvent*>(event)->matches(QKeySequence::Cancel))
      hide();
  }

  return false;
}

void SkylanderPortalWindow::closeEvent(QCloseEvent* event)
{
  hide();
}

// user interface
void SkylanderPortalWindow::EmulatePortal(bool emulate)
{
  Config::SetBaseOrCurrent(Config::MAIN_EMULATE_SKYLANDER_PORTAL, emulate);
  m_group_skylanders->setVisible(emulate);
  m_command_buttons->setVisible(emulate);
  m_emulating = emulate;
}

void SkylanderPortalWindow::SelectCollectionPath()
{
  QString dir = QDir::toNativeSeparators(DolphinFileDialog::getExistingDirectory(
      this, tr("Select Skylander Collection"), m_collection_path));
  if (!dir.isEmpty())
  {
    dir += QDir::separator();
    m_path_edit->setText(dir);
    m_collection_path = dir;
  }
  Config::SetBase(Config::MAIN_SKYLANDERS_PATH, dir.toStdString());

  if (m_only_show_collection->isChecked())
    RefreshList();
}

void SkylanderPortalWindow::LoadSelected()
{
  if (!m_emulating)
    return;

  u8 slot = GetCurrentSlot();

  QDir collection = QDir(m_collection_path);
  QString skyName;
  QString file_path;
  if (m_only_show_collection->isChecked())
  {
    if (m_skylander_list->currentItem() == nullptr)
      return;
    file_path = m_collection_path + m_skylander_list->currentItem()->text() + tr(".sky");
  }
  else
  {
    file_path = GetFilePath(sky_id, sky_var);
  }

  if (file_path.isEmpty())
  {
    QMessageBox::StandardButton createFileResponse;
    createFileResponse =
        QMessageBox::question(this, tr("Create Skylander File"),
                              tr("Skylander not found in this collection. Create new file?"),
                              QMessageBox::Yes | QMessageBox::No);

    if (createFileResponse == QMessageBox::Yes)
    {
      QString predef_name = m_collection_path;
      const auto found_sky = IOS::HLE::USB::list_skylanders.find(std::make_pair(sky_id, sky_var));
      if (found_sky != IOS::HLE::USB::list_skylanders.end())
      {
        predef_name += QString::fromStdString(std::string(found_sky->second.name) + ".sky");
      }
      else
      {
        QString str = tr("Unknown(%1 %2).sky");
        predef_name += str.arg(sky_id, sky_var);
      }

      CreateSkyfile(predef_name, true);
    }
  }
  else
  {
    m_last_skylander_path = QFileInfo(file_path).absolutePath() + tr("/");

    LoadSkyfilePath(slot, file_path);
  }
}

void SkylanderPortalWindow::LoadFromFile()
{
  u8 slot = GetCurrentSlot();
  const QString file_path =
      DolphinFileDialog::getOpenFileName(this, tr("Select Skylander File"), m_last_skylander_path,
                                         tr("Skylander (*.sky);;All Files (*)"));
  ;
  if (file_path.isEmpty())
  {
    return;
  }
  m_last_skylander_path =
      QDir::toNativeSeparators(QFileInfo(file_path).absolutePath()) + QDir::separator();

  LoadSkyfilePath(slot, file_path);
}

void SkylanderPortalWindow::CreateSkylanderAdvanced()
{
  QDialog* createWindow = new QDialog;

  auto* layout = new QVBoxLayout;

  auto* hbox_idvar = new QHBoxLayout();
  auto* label_id = new QLabel(tr("ID:"));
  auto* label_var = new QLabel(tr("Variant:"));
  auto* edit_id = new QLineEdit(tr("0"));
  auto* edit_var = new QLineEdit(tr("0"));
  auto* rxv =
      new QRegularExpressionValidator(QRegularExpression(QString::fromStdString("\\d*")), this);
  edit_id->setValidator(rxv);
  edit_var->setValidator(rxv);
  hbox_idvar->addWidget(label_id);
  hbox_idvar->addWidget(edit_id);
  hbox_idvar->addWidget(label_var);
  hbox_idvar->addWidget(edit_var);
  layout->addLayout(hbox_idvar);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
  layout->addWidget(buttons);

  createWindow->setLayout(layout);

  connect(buttons, &QDialogButtonBox::accepted, this, [=, this]() {
    bool ok_id = false, ok_var = false;
    sky_id = edit_id->text().toUShort(&ok_id);
    if (!ok_id)
    {
      QMessageBox::warning(this, tr("Error converting value"), tr("ID entered is invalid!"),
                           QMessageBox::Ok);
      return;
    }
    sky_var = edit_var->text().toUShort(&ok_var);
    if (!ok_var)
    {
      QMessageBox::warning(this, tr("Error converting value"), tr("Variant entered is invalid!"),
                           QMessageBox::Ok);
      return;
    }

    QString predef_name = m_last_skylander_path;
    const auto found_sky = IOS::HLE::USB::list_skylanders.find(std::make_pair(sky_id, sky_var));
    if (found_sky != IOS::HLE::USB::list_skylanders.end())
    {
      predef_name += QString::fromStdString(std::string(found_sky->second.name) + ".sky");
    }
    else
    {
      QString str = tr("Unknown(%1 %2).sky");
      predef_name += str.arg(sky_id, sky_var);
    }

    QString file_path = DolphinFileDialog::getSaveFileName(
        this, tr("Create Skylander File"), predef_name, tr("Skylander (*.sky);;All Files (*)"));
    if (file_path.isEmpty())
    {
      return;
    }

    CreateSkyfile(file_path, true);
    createWindow->accept();
  });

  connect(buttons, &QDialogButtonBox::rejected, createWindow, &QDialog::reject);

  createWindow->show();
  createWindow->raise();
}

void SkylanderPortalWindow::ClearSlot(u8 slot)
{
  auto& system = Core::System::GetInstance();
  if (auto slot_infos = m_sky_slots[slot])
  {
    if (!system.GetSkylanderPortal().RemoveSkylander(slot_infos->portal_slot))
    {
      QMessageBox::warning(this, tr("Failed to clear Skylander!"),
                           tr("Failed to clear the Skylander from slot(%1)!\n").arg(slot),
                           QMessageBox::Ok);
      return;
    }
    m_sky_slots[slot].reset();
    if (m_only_show_collection->isChecked())
      RefreshList();
    UpdateSlotNames();
  }
}

// behind the scenes
void SkylanderPortalWindow::OnCollectionPathChanged()
{
  m_collection_path = m_path_edit->text();
  Config::SetBase(Config::MAIN_SKYLANDERS_PATH, m_path_edit->text().toStdString());
  RefreshList();
}

void SkylanderPortalWindow::OnEmulationStateChanged(Core::State state)
{
  const bool running = state != Core::State::Uninitialized;

  m_enabled_checkbox->setEnabled(!running);
}

void SkylanderPortalWindow::UpdateCurrentIDs()
{
  const u32 sky_info = m_skylander_list->currentItem()->data(1).toUInt();
  if (sky_info != 0xFFFFFFFF)
  {
    sky_id = sky_info >> 16;
    sky_var = sky_info & 0xFFFF;
  }
}

void SkylanderPortalWindow::RefreshList()
{
  int row = m_skylander_list->currentRow();
  m_skylander_list->clear();
  if (m_only_show_collection->isChecked())
  {
    QDir collection = QDir(m_collection_path);
    auto& system = Core::System::GetInstance();
    for (auto file : collection.entryInfoList(QStringList(tr("*.sky"))))
    {
      File::IOFile sky_file(file.filePath().toStdString(), "r+b");
      if (!sky_file)
      {
        continue;
      }
      std::array<u8, 0x40 * 0x10> file_data;
      if (!sky_file.ReadBytes(file_data.data(), file_data.size()))
      {
        continue;
      }
      auto ids = system.GetSkylanderPortal().CalculateIDs(file_data);
      if (PassesFilter(file.baseName(), ids.first, ids.second))
      {
        const uint qvar = (ids.first << 16) | ids.second;
        QListWidgetItem* skylander = new QListWidgetItem(file.baseName());
        skylander->setBackground(GetBaseColor(ids));
        skylander->setForeground(QBrush(QColor(0, 0, 0, 255)));
        skylander->setData(1, qvar);
        m_skylander_list->addItem(skylander);
      }
    }
  }
  else
  {
    for (const auto& entry : IOS::HLE::USB::list_skylanders)
    {
      int id = entry.first.first;
      int var = entry.first.second;
      if (PassesFilter(tr(entry.second.name), id, var))
      {
        const uint qvar = (entry.first.first << 16) | entry.first.second;
        QListWidgetItem* skylander = new QListWidgetItem(tr(entry.second.name));
        ;
        skylander->setBackground(GetBaseColor(entry.first));
        skylander->setForeground(QBrush(QColor(0, 0, 0, 255)));
        skylander->setData(1, qvar);
        m_skylander_list->addItem(skylander);
      }
    }
  }
  if (m_skylander_list->count() >= row)
  {
    m_skylander_list->setCurrentItem(m_skylander_list->item(row), QItemSelectionModel::Select);
  }
  else if (m_skylander_list->count() > 0)
  {
    m_skylander_list->setCurrentItem(m_skylander_list->item(m_skylander_list->count() - 1),
                                     QItemSelectionModel::Select);
  }
}

void SkylanderPortalWindow::CreateSkyfile(const QString& path, bool loadAfter)
{
  auto& system = Core::System::GetInstance();

  if (!system.GetSkylanderPortal().CreateSkylander(path.toStdString(), sky_id, sky_var))
  {
    QMessageBox::warning(this, tr("Failed to create Skylander file!"),
                         tr("Failed to create Skylander file:\n%1").arg(path), QMessageBox::Ok);
    return;
  }
  m_last_skylander_path = QFileInfo(path).absolutePath() + QString::fromStdString("/");

  if (loadAfter)
    LoadSkyfilePath(GetCurrentSlot(), path);
}

void SkylanderPortalWindow::LoadSkyfilePath(u8 slot, const QString& path)
{
  File::IOFile sky_file(path.toStdString(), "r+b");
  if (!sky_file)
  {
    QMessageBox::warning(
        this, tr("Failed to open the Skylander file!"),
        tr("Failed to open the Skylander file(%1)!\nFile may already be in use on the portal.")
            .arg(path),
        QMessageBox::Ok);
    return;
  }
  std::array<u8, 0x40 * 0x10> file_data;
  if (!sky_file.ReadBytes(file_data.data(), file_data.size()))
  {
    QMessageBox::warning(
        this, tr("Failed to read the Skylander file!"),
        tr("Failed to read the Skylander file(%1)!\nFile was too small.").arg(path),
        QMessageBox::Ok);
    return;
  }

  ClearSlot(slot);

  auto& system = Core::System::GetInstance();
  std::pair<u16, u16> id_var = system.GetSkylanderPortal().CalculateIDs(file_data);
  u8 portal_slot = system.GetSkylanderPortal().LoadSkylander(file_data.data(), std::move(sky_file));
  if (portal_slot == 0xFF)
  {
    QMessageBox::warning(this, tr("Failed to load the Skylander file!"),
                         tr("Failed to load the Skylander file(%1)!\n").arg(path), QMessageBox::Ok);
    return;
  }
  m_sky_slots[slot] = {portal_slot, id_var.first, id_var.second};
  RefreshList();
  UpdateSlotNames();
}

void SkylanderPortalWindow::UpdateSlotNames()
{
  for (auto i = 0; i < MAX_SKYLANDERS; i++)
  {
    QString display_string;
    if (auto sd = m_sky_slots[i])
    {
      auto found_sky = IOS::HLE::USB::list_skylanders.find(std::make_pair(sd->sky_id, sd->sky_var));
      if (found_sky != IOS::HLE::USB::list_skylanders.end())
      {
        display_string = QString::fromStdString(found_sky->second.name);
      }
      else
      {
        display_string = QString(tr("Unknown (Id:%1 Var:%2)")).arg(sd->sky_id).arg(sd->sky_var);
      }
    }
    else
    {
      display_string = tr("None");
    }

    m_edit_skylanders[i]->setText(display_string);
  }
}

// helpers
bool SkylanderPortalWindow::PassesFilter(QString name, u16 id, u16 var)
{
  const auto skypair = IOS::HLE::USB::list_skylanders.find(std::make_pair(id, var));
  IOS::HLE::USB::SkyData character;
  if (skypair == IOS::HLE::USB::list_skylanders.end())
  {
    return false;
  }
  else
  {
    character = skypair->second;
  }

  bool pass = false;
  if (m_game_filters[IOS::HLE::USB::SPYROS_ADV]->isChecked())
  {
    if (character.game == IOS::HLE::USB::SPYROS_ADV)
      pass = true;
  }
  if (m_game_filters[IOS::HLE::USB::GIANTS]->isChecked())
  {
    if (character.game == IOS::HLE::USB::GIANTS)
      pass = true;
  }
  if (m_game_filters[IOS::HLE::USB::SWAP_FORCE]->isChecked())
  {
    if (character.game == IOS::HLE::USB::SWAP_FORCE)
      pass = true;
  }
  if (m_game_filters[IOS::HLE::USB::TRAP_TEAM]->isChecked())
  {
    if (character.game == IOS::HLE::USB::TRAP_TEAM)
      pass = true;
  }
  if (m_game_filters[IOS::HLE::USB::SUPERCHARGERS]->isChecked())
  {
    if (character.game == IOS::HLE::USB::SUPERCHARGERS)
      pass = true;
  }
  if (!pass)
    return false;

  if (!name.contains(m_sky_search->text(), Qt::CaseInsensitive))
    return false;

  switch (GetElementRadio())
  {
  case 1:
    if (character.element != IOS::HLE::USB::MAGIC)
      return false;
    break;
  case 2:
    if (character.element != IOS::HLE::USB::WATER)
      return false;
    break;
  case 3:
    if (character.element != IOS::HLE::USB::TECH)
      return false;
    break;
  case 4:
    if (character.element != IOS::HLE::USB::FIRE)
      return false;
    break;
  case 5:
    if (character.element != IOS::HLE::USB::EARTH)
      return false;
    break;
  case 6:
    if (character.element != IOS::HLE::USB::LIFE)
      return false;
    break;
  case 7:
    if (character.element != IOS::HLE::USB::AIR)
      return false;
    break;
  case 8:
    if (character.element != IOS::HLE::USB::UNDEAD)
      return false;
    break;
  case 9:
    if (character.element != IOS::HLE::USB::E_OTHER)
      return false;
    break;
  }

  return true;
}

QString SkylanderPortalWindow::GetFilePath(u16 id, u16 var)
{
  QDir collection = QDir(m_collection_path);
  auto& system = Core::System::GetInstance();
  QString file_path;
  for (auto file : collection.entryInfoList(QStringList(tr("*.sky"))))
  {
    File::IOFile sky_file(file.filePath().toStdString(), "r+b");
    if (!sky_file)
    {
      continue;
    }
    std::array<u8, 0x40 * 0x10> file_data;
    if (!sky_file.ReadBytes(file_data.data(), file_data.size()))
    {
      continue;
    }
    auto ids = system.GetSkylanderPortal().CalculateIDs(file_data);
    if (ids.first == id && ids.second == var)
    {
      return file.filePath();
    }
  }
  return file_path;
}

u8 SkylanderPortalWindow::GetCurrentSlot()
{
  for (auto radio : m_slot_radios)
  {
    if (radio->isChecked())
    {
      return radio->property("id").toInt();
    }
  }
  return 0;
}

int SkylanderPortalWindow::GetElementRadio()
{
  for (auto radio : m_element_filter)
  {
    if (radio->isChecked())
    {
      return radio->property("id").toInt();
    }
  }
  return -1;
}

QBrush SkylanderPortalWindow::GetBaseColor(std::pair<const u16, const u16> ids)
{
  switch (IOS::HLE::USB::list_skylanders.at(ids).game)
  {
  case IOS::HLE::USB::SPYROS_ADV:
    return QBrush(QColor(240, 255, 240, 255));
  case IOS::HLE::USB::GIANTS:
    return QBrush(QColor(255, 240, 215, 255));
  case IOS::HLE::USB::SWAP_FORCE:
    return QBrush(QColor(240, 245, 255, 255));
  case IOS::HLE::USB::TRAP_TEAM:
    return QBrush(QColor(255, 240, 240, 255));
  case IOS::HLE::USB::SUPERCHARGERS:
    return QBrush(QColor(247, 228, 215, 255));
  default:
    return QBrush(QColor(255, 255, 255, 255));
  }
}
