#include "AboutDialog.hpp"
#include "FileTreeModel.hpp"
#include "GuiLogController.hpp"
#include "GuiTaskRunner.hpp"
#include "Logo.hpp"
#include "MainWindow.hpp"
#include "TrackerEditDialog.hpp"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QModelIndex>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QWidget>
#include <QtTest>
#include <stdexcept>
#include <torrentutils/core/application.hpp>
#include <torrentutils/core/version.hpp>
#include <utility>
#include <vector>

namespace {

using namespace torrentutils::core;

FileEntry make_file(std::vector<std::string> segments, const std::uint64_t size,
                    const FileAttributes attributes = {})
{
    auto path = LogicalPath::from_segments(std::move(segments));
    Q_ASSERT(path);
    auto file = FileEntry::create(std::move(path).value(), size, attributes);
    Q_ASSERT(file);
    return std::move(file).value();
}

QModelIndex child_named(const FileTreeModel& model, const QModelIndex& parent, const QString& name)
{
    for (int row = 0; row < model.rowCount(parent); ++row)
    {
        const auto candidate = model.index(row, 0, parent);
        if (candidate.data().toString() == name)
        {
            return candidate;
        }
    }
    return {};
}

bool send_drop(QWidget* target, const QMimeData& mime_data)
{
    QDragEnterEvent enter(QPoint(4, 4), Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
    static_cast<void>(QApplication::sendEvent(target, &enter));
    if (!enter.isAccepted())
        return false;

    QDropEvent event(QPointF(4.0, 4.0), Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
    static_cast<void>(QApplication::sendEvent(target, &event));
    return event.isAccepted();
}

#ifdef Q_OS_WIN
constexpr auto kUserConfigEnvironment = "APPDATA";
#else
constexpr auto kUserConfigEnvironment = "XDG_CONFIG_HOME";
#endif

class EnvironmentVariableGuard final
{
  public:
    explicit EnvironmentVariableGuard(const char* name)
        : name_(name), value_(qgetenv(name)), was_set_(qEnvironmentVariableIsSet(name))
    {
    }

    ~EnvironmentVariableGuard()
    {
        if (was_set_)
        {
            qputenv(name_.constData(), value_);
        }
        else
        {
            qunsetenv(name_.constData());
        }
    }

    void set(const QByteArray& value)
    {
        qputenv(name_.constData(), value);
    }

  private:
    QByteArray name_;
    QByteArray value_;
    bool was_set_;
};

class CurrentDirectoryGuard final
{
  public:
    explicit CurrentDirectoryGuard(const QString& path)
        : original_(QDir::currentPath()), changed_(QDir::setCurrent(path))
    {
    }

    ~CurrentDirectoryGuard()
    {
        if (changed_)
        {
            static_cast<void>(QDir::setCurrent(original_));
        }
    }

    [[nodiscard]] bool changed() const noexcept
    {
        return changed_;
    }

  private:
    QString original_;
    bool changed_;
};

} // namespace

class GuiLogoTest final : public QObject
{
    Q_OBJECT
  public:
    static void initMain();

  private slots:
    void loggerKeepsStructuredUnicodeDiagnostics()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        GuiLogController logger;
        torrentutils::frontend::GuiPreferences preferences;
        preferences.logging_enabled = true;
        preferences.log_level = torrentutils::frontend::GuiLogLevel::Debug;
        preferences.log_path =
            QDir(directory.path()).filePath(QStringLiteral("session.log")).toUtf8().toStdString();
        logger.configure(preferences, {});
        logger.log_event(torrentutils::core::LogLevel::Warning, "gui", "verify", "failure",
                         {{"path", "D:/测试/file.bin"}});
        const auto context = logger.diagnostic_context();
        QVERIFY(context.find("level=WARNING") != std::string::npos);
        QVERIFY(context.find("D:/测试/file.bin") != std::string::npos);
    }

    void loggerCorrelatesOperationLifecycle()
    {
        GuiLogController logger;
        const auto first = logger.begin_operation("gui", "verify", {{"path", "D:/data"}});
        const auto second = logger.begin_operation("gui", "inspect");
        logger.finish_operation(first, torrentutils::core::LogLevel::Info, "gui", "verify",
                                "finish", {{"outcome", "verified"}});
        logger.finish_operation(second, torrentutils::core::LogLevel::Warning, "gui", "inspect",
                                "skip", {{"reason", "cancelled"}});
        QCOMPARE(QString::fromStdString(first), QStringLiteral("op-000001"));
        QCOMPARE(QString::fromStdString(second), QStringLiteral("op-000002"));
        const auto context = logger.diagnostic_context();
        QVERIFY(context.find("operation_id=\"op-000001\"") != std::string::npos);
        QVERIFY(context.find("operation_id=\"op-000002\"") != std::string::npos);
        QVERIFY(context.find("duration_ms=") != std::string::npos);
    }

    void applicationIconContainsLogo()
    {
        QVERIFY(!torrentcraft::gui::application_icon().isNull());
        QVERIFY(!torrentcraft::gui::render_logo(64).isNull());
    }

    void fileTreeIconsAreRenderable()
    {
        const QIcon fallback_file(QStringLiteral(":/torrentcraft/icons/file.svg"));
        const QIcon fallback_folder(QStringLiteral(":/torrentcraft/icons/folder.svg"));
        QVERIFY(!fallback_file.pixmap(16, 16).isNull());
        QVERIFY(!fallback_folder.pixmap(16, 16).isNull());

        FileTreeModel model(nullptr, FileTreeModel::Mode::Inspect);
        model.set_files({make_file({"folder", "payload.bin"}, 1)});
        model.fetchMore({});

        const auto folder = child_named(model, {}, QStringLiteral("folder"));
        QVERIFY(folder.isValid());
        const auto folder_icon = folder.data(Qt::DecorationRole).value<QIcon>();
        QVERIFY(!folder_icon.pixmap(16, 16).isNull());

        model.fetchMore(folder);
        const auto file = child_named(model, folder, QStringLiteral("payload.bin"));
        QVERIFY(file.isValid());
        const auto file_icon = file.data(Qt::DecorationRole).value<QIcon>();
        QVERIFY(!file_icon.pixmap(16, 16).isNull());
    }
    void aboutDialogShowsLogo()
    {
        AboutDialog dialog;
        const auto* logo = dialog.findChild<QLabel*>("logo");
        if (logo == nullptr)
        {
            QFAIL("About dialog logo label was not found.");
        }
        QVERIFY(!logo->pixmap().isNull());
    }
    void aboutDialogShowsCoreVersion()
    {
        AboutDialog dialog;
        const auto* version_label = dialog.findChild<QLabel*>("version");
        QVERIFY(version_label != nullptr);
        const auto core_version = torrentutils::core::version();
        QCOMPARE(version_label->text(),
                 QStringLiteral("Version ") +
                     QString::fromUtf8(core_version.data(), static_cast<int>(core_version.size())));
    }

    void aboutDialogCloseButtonCloses()
    {
        AboutDialog dialog;
        dialog.show();
        auto* close = dialog.findChild<QPushButton*>("btnClose");
        QVERIFY(close != nullptr);
        QTest::mouseClick(close, Qt::LeftButton);
        QVERIFY(!dialog.isVisible());
    }

    void aboutDialogCanViewSelectedLicense()
    {
        AboutDialog dialog;
        dialog.show();
        auto* tree = dialog.findChild<QTreeWidget*>("treeLicenses");
        auto* view = dialog.findChild<QPushButton*>("btnViewLicense");
        QVERIFY(tree != nullptr);
        QVERIFY(view != nullptr);
        QVERIFY(!view->isEnabled());
        QVERIFY(tree->topLevelItemCount() > 0);
        tree->setCurrentItem(tree->topLevelItem(0));
        QVERIFY(view->isEnabled());

        bool opened = false;
        bool closed_by_button = false;
        QTimer::singleShot(0, [&] {
            for (auto* browser : dialog.findChildren<QTextBrowser*>())
            {
                opened = !browser->toPlainText().isEmpty();
            }
            for (auto* child : dialog.findChildren<QDialog*>())
            {
                if (child != &dialog)
                {
                    auto* buttons = child->findChild<QDialogButtonBox*>("buttonBox");
                    if (buttons != nullptr && buttons->button(QDialogButtonBox::Close) != nullptr)
                    {
                        buttons->button(QDialogButtonBox::Close)->click();
                        closed_by_button = true;
                    }
                    else
                    {
                        child->accept();
                    }
                }
            }
        });
        QTest::mouseClick(view, Qt::LeftButton);
        QVERIFY(opened);
        QVERIFY(closed_by_button);
    }
    void aboutComponentsShowBuildVersions()
    {
        AboutDialog dialog;
        auto* tree = dialog.findChild<QTreeWidget*>("treeComponents");
        QVERIFY(tree != nullptr);
        QCOMPARE(tree->topLevelItemCount(), 7);
        for (int row = 0; row < tree->topLevelItemCount(); ++row)
        {
            const auto version = tree->topLevelItem(row)->text(1);
            QVERIFY(!version.isEmpty());
            QVERIFY(version != QStringLiteral("6.x (planned)"));
            QVERIFY(version != QStringLiteral("vcpkg baseline"));
            QVERIFY(version != QStringLiteral("transitive"));
        }
    }

    void trackerFileListShowsFilenameSizeAndStatus()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile torrent(directory.filePath(QStringLiteral("sample.torrent")));
        QVERIFY(torrent.open(QIODevice::WriteOnly));
        QCOMPARE(torrent.write("test"), qint64(4));
        torrent.close();

        MainWindow window;
        auto* source = window.findChild<QLineEdit*>("editTrackerSourcePath");
        auto* reload = window.findChild<QPushButton*>("btnTrackerReloadFolder");
        auto* table = window.findChild<QTableView*>("tblTrackerTorrents");
        QVERIFY(source != nullptr);
        QVERIFY(reload != nullptr);
        QVERIFY(table != nullptr);
        source->setText(directory.path());
        reload->click();

        auto* model = table->model();
        QVERIFY(model != nullptr);
        QCOMPARE(model->columnCount(), 3);
        QCOMPARE(model->headerData(0, Qt::Horizontal).toString(), QStringLiteral("Filename"));
        QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QStringLiteral("Size"));
        QCOMPARE(model->headerData(2, Qt::Horizontal).toString(), QStringLiteral("Status"));
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("sample.torrent"));
        QCOMPARE(model->index(0, 1).data().toString(), QStringLiteral("4 B"));
        QCOMPARE(model->index(0, 2).data().toString(), QStringLiteral("Ready"));
        QCOMPARE(QDir::fromNativeSeparators(model->index(0, 0).data(Qt::UserRole).toString()),
                 QDir::fromNativeSeparators(QFileInfo(torrent).absoluteFilePath()));
    }

    void verifyContentDirectoryShowsDragAndDropHint()
    {
        MainWindow window;
        const auto* hint = window.findChild<QLabel*>("lblVerifyContentDropArea");
        QVERIFY(hint != nullptr);
        QCOMPARE(hint->text(), QStringLiteral("[Drag and drop area]"));
    }

    void pathDropsDecodeLocalUrlsAndRejectOtherData()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto first_path = directory.filePath(QStringLiteral("payload with space"));
        const auto second_path = directory.filePath(QStringLiteral("ignored"));
        QVERIFY(QDir().mkpath(first_path));
        QVERIFY(QDir().mkpath(second_path));

        MainWindow window;
        auto* input = window.findChild<QLineEdit*>("editCreateInputPath");
        auto* group = window.findChild<QWidget*>("grpCreateInput");
        QVERIFY(input != nullptr);
        QVERIFY(group != nullptr);

        QMimeData local;
        local.setUrls({QUrl::fromLocalFile(first_path), QUrl::fromLocalFile(second_path)});
        QVERIFY(send_drop(input, local));
        QCOMPARE(QDir::fromNativeSeparators(input->text()), QDir::fromNativeSeparators(first_path));

        auto localhost_url = QUrl::fromLocalFile(second_path);
        localhost_url.setHost(QStringLiteral("localhost"));
        QMimeData localhost;
        localhost.setUrls({localhost_url});
        QVERIFY(send_drop(group, localhost));
        QCOMPARE(QDir::fromNativeSeparators(input->text()),
                 QDir::fromNativeSeparators(second_path));

        input->setText(QStringLiteral("unchanged"));
        QUrl remote_url;
        remote_url.setScheme(QStringLiteral("file"));
        remote_url.setHost(QStringLiteral("fileserver"));
        remote_url.setPath(QStringLiteral("/share/payload.torrent"));
        QMimeData remote;
        remote.setUrls({remote_url});
        QVERIFY(!send_drop(input, remote));
        QCOMPARE(input->text(), QStringLiteral("unchanged"));

        QMimeData web;
        web.setUrls({QUrl(QStringLiteral("https://example.invalid/payload.torrent"))});
        QVERIFY(!send_drop(input, web));
        QCOMPARE(input->text(), QStringLiteral("unchanged"));

        QMimeData plain_text;
        plain_text.setText(first_path);
        QVERIFY(!send_drop(input, plain_text));
        QCOMPARE(input->text(), QStringLiteral("unchanged"));
    }

    void trackerTablesKeepFullTrackerUrlsAndScrollHorizontally()
    {
        MainWindow window;
        const char* names[] = {"tblCreateTrackers", "tblModifyTrackers", "tblTrackerTiers",
                               "tblInspectTrackers"};
        for (const auto* name : names)
        {
            auto* table = window.findChild<QTableView*>(name);
            QVERIFY2(table != nullptr, name);
            const auto* header = table->horizontalHeader();
            QCOMPARE(header->sectionResizeMode(0), QHeaderView::Fixed);
            QCOMPARE(header->sectionSize(0), 48);
            QCOMPARE(header->sectionResizeMode(1), QHeaderView::ResizeToContents);
            QVERIFY(header->stretchLastSection());
            QCOMPARE(table->textElideMode(), Qt::ElideNone);
            QCOMPARE(table->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
        }
    }

    void advancedMemoryControlsExposeAlignedDefaults()
    {
        MainWindow window;
        const auto* hash_memory = window.findChild<QSpinBox*>("spinAdvancedVerifyMemory");
        const auto* working_set = window.findChild<QSpinBox*>("spinAdvancedMemoryWorkingSetLimit");
        QVERIFY(hash_memory != nullptr);
        QVERIFY(working_set != nullptr);
        QCOMPARE(hash_memory->value(), 32);
        QCOMPARE(hash_memory->suffix(), QStringLiteral(" MiB"));
        QCOMPARE(working_set->value(), 512);
        QCOMPARE(working_set->suffix(), QStringLiteral(" MiB"));
    }

    void advancedDisplayControlsExposeAvailableOptions()
    {
        MainWindow window;
        auto* style = window.findChild<QComboBox*>("cmbAdvancedStyle");
        auto* font = window.findChild<QComboBox*>("cmbAdvancedFont");
        auto* style_label = window.findChild<QLabel*>("lblAdvancedStyle");
        auto* font_label = window.findChild<QLabel*>("lblAdvancedFont");
        QVERIFY(style != nullptr);
        QVERIFY(font != nullptr);
        QVERIFY(style_label != nullptr);
        QVERIFY(font_label != nullptr);
        QVERIFY(style->count() >= 1);
        const auto system_fonts = QFontDatabase::families();
        QVERIFY2(!system_fonts.isEmpty(), "Qt must discover at least one system font");
        QVERIFY(font->count() > 1);
        QCOMPARE(style->itemData(0).toString(), QString());
        QCOMPARE(font->itemData(0).toString(), QString());
        QVERIFY(style->findData(QStringLiteral("Fusion")) >= 0);
        QVERIFY(!style_label->text().isEmpty());
        QVERIFY(!font_label->text().isEmpty());
    }

    void guiLoadsCanonicalUserConfigWhenProjectConfigIsAbsent()
    {
        QTemporaryDir user_config_directory;
        QTemporaryDir working_directory;
        QVERIFY(user_config_directory.isValid());
        QVERIFY(working_directory.isValid());
        EnvironmentVariableGuard user_config_environment(kUserConfigEnvironment);
        user_config_environment.set(user_config_directory.path().toUtf8());
        CurrentDirectoryGuard current_directory(working_directory.path());
        QVERIFY(current_directory.changed());

        const auto config_path = QDir(user_config_directory.path())
                                     .filePath(QStringLiteral("torrentcraft/torrentcraft.json"));
        QVERIFY(QDir().mkpath(QFileInfo(config_path).absolutePath()));
        QFile config(config_path);
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(config.write(R"({
            "schema": "torrentcraft.config/v1",
            "gui": {"language": "zh_CN"}
        })") > 0);
        config.close();

        MainWindow window;
        auto* config_path_widget =
            window.findChild<QLineEdit*>(QStringLiteral("editAdvancedConfigPath"));
        auto* chinese = window.findChild<QAction*>(QStringLiteral("actionLanguageChinese"));
        QVERIFY(config_path_widget != nullptr);
        QVERIFY(chinese != nullptr);
        QCOMPARE(config_path_widget->text(), QDir::toNativeSeparators(config_path));
        QVERIFY(chinese->isChecked());
    }

    void guiCreatesCanonicalUserConfigWhenLanguageChangesWithoutConfig()
    {
        QTemporaryDir user_config_directory;
        QTemporaryDir working_directory;
        QVERIFY(user_config_directory.isValid());
        QVERIFY(working_directory.isValid());
        EnvironmentVariableGuard user_config_environment(kUserConfigEnvironment);
        user_config_environment.set(user_config_directory.path().toUtf8());
        CurrentDirectoryGuard current_directory(working_directory.path());
        QVERIFY(current_directory.changed());

        const auto config_path = QDir(user_config_directory.path())
                                     .filePath(QStringLiteral("torrentcraft/torrentcraft.json"));
        MainWindow window;
        auto* config_path_widget =
            window.findChild<QLineEdit*>(QStringLiteral("editAdvancedConfigPath"));
        auto* chinese = window.findChild<QAction*>(QStringLiteral("actionLanguageChinese"));
        QVERIFY(config_path_widget != nullptr);
        QVERIFY(chinese != nullptr);
        QCOMPARE(config_path_widget->text(), QDir::toNativeSeparators(config_path));
        QVERIFY(!QFileInfo::exists(config_path));

        chinese->trigger();

        auto persisted = torrentutils::frontend::ConfigFile::load(
            std::filesystem::u8path(config_path.toUtf8().toStdString()));
        QVERIFY(persisted);
        QCOMPARE(persisted.value().gui_language(),
                 torrentutils::frontend::GuiLanguage::SimplifiedChinese);
    }

    void advancedLabelsTranslateWithLanguage()
    {
        QTemporaryDir config_directory;
        QVERIFY(config_directory.isValid());
        QFile existing_config(config_directory.filePath(QStringLiteral("torrentcraft.json")));
        QVERIFY(existing_config.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(existing_config.write(R"({"schema":"torrentcraft.config/v1"})") > 0);
        existing_config.close();
        MainWindow window;
        auto* config_path = window.findChild<QLineEdit*>(QStringLiteral("editAdvancedConfigPath"));
        QVERIFY(config_path != nullptr);
        config_path->setText(config_directory.filePath(QStringLiteral("torrentcraft.json")));
        auto* chinese = window.findChild<QAction*>(QStringLiteral("actionLanguageChinese"));
        auto* english = window.findChild<QAction*>(QStringLiteral("actionLanguageEnglish"));
        auto* default_label = window.findChild<QLabel*>(QStringLiteral("lblAdvancedDefaultPreset"));
        auto* default_combo =
            window.findChild<QComboBox*>(QStringLiteral("cmbAdvancedDefaultPreset"));
        auto* verify_label = window.findChild<QLabel*>(QStringLiteral("lblAdvancedVerifyMemory"));
        auto* physical_label =
            window.findChild<QLabel*>(QStringLiteral("lblAdvancedMemoryWorkingSetLimit"));
        QVERIFY(chinese != nullptr);
        QVERIFY(english != nullptr);
        QVERIFY(default_label != nullptr);
        QVERIFY(default_combo != nullptr);
        QVERIFY(verify_label != nullptr);
        QVERIFY(physical_label != nullptr);

        chinese->trigger();
        QCOMPARE(default_label->text(), QStringLiteral("默认预设："));
        QCOMPARE(default_combo->itemText(0), QStringLiteral("默认值"));
        QCOMPARE(verify_label->text(), QStringLiteral("校验内存（MiB）："));
        auto persisted = torrentutils::frontend::ConfigFile::load(std::filesystem::u8path(
            config_directory.filePath(QStringLiteral("torrentcraft.json")).toUtf8().toStdString()));
        QVERIFY(persisted);
        QCOMPARE(persisted.value().gui_language(),
                 torrentutils::frontend::GuiLanguage::SimplifiedChinese);
        QCOMPARE(physical_label->text(), QStringLiteral("物理内存（RAM）使用上限："));

        english->trigger();
        QCOMPARE(default_label->text(), QStringLiteral("Default preset:"));
        QCOMPARE(default_combo->itemText(0), QStringLiteral("Defaults"));
    }

    void presetTitleTracksActivePresetAndModifiedState()
    {
        QTemporaryDir config_directory;
        QVERIFY(config_directory.isValid());
        const auto config_path = config_directory.filePath(QStringLiteral("torrentcraft.json"));
        QFile config(config_path);
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(config.write(R"({
            "schema": "torrentcraft.config/v1",
            "defaults": {},
            "presets": {"Demo": {"comment": "from preset"}},
            "gui": {"default_preset": "Demo"}
        })") > 0);
        config.close();

        MainWindow window;
        auto* config_edit = window.findChild<QLineEdit*>(QStringLiteral("editAdvancedConfigPath"));
        auto* reload = window.findChild<QPushButton*>(QStringLiteral("btnAdvancedReloadConfig"));
        auto* comment = window.findChild<QPlainTextEdit*>(QStringLiteral("editCreateComment"));
        auto* preset_menu = window.findChild<QMenu*>(QStringLiteral("menuLoadPreset"));
        QVERIFY(config_edit != nullptr);
        QVERIFY(reload != nullptr);
        QVERIFY(comment != nullptr);
        QVERIFY(preset_menu != nullptr);

        config_edit->setText(config_path);
        reload->click();
        QCOMPARE(window.windowTitle(), QStringLiteral("TorrentCraft — [Demo]"));

        comment->setPlainText(QStringLiteral("changed"));
        QCOMPARE(window.windowTitle(), QStringLiteral("TorrentCraft — [Demo*]"));

        QAction* demo = nullptr;
        for (auto* action : preset_menu->actions())
        {
            if (action->data().toString() == QStringLiteral("Demo"))
            {
                demo = action;
                break;
            }
        }
        QVERIFY(demo != nullptr);
        demo->trigger();
        QCOMPARE(window.windowTitle(), QStringLiteral("TorrentCraft — [Demo]"));

        auto* clear = window.findChild<QAction*>(QStringLiteral("actionClearAll"));
        QVERIFY(clear != nullptr);
        clear->trigger();
        QCOMPARE(window.windowTitle(), QStringLiteral("TorrentCraft"));
    }

    void modifyOutputPathFollowsInputWhileAutomatic()
    {
        MainWindow window;
        auto* input = window.findChild<QLineEdit*>("editModifyTorrentPath");
        auto* output = window.findChild<QLineEdit*>("editModifyOutputPath");
        QVERIFY(input != nullptr);
        QVERIFY(output != nullptr);

        input->setText(QStringLiteral("C:/payload/first.torrent"));
        const auto first_output = output->text();
        QVERIFY(first_output.endsWith(QStringLiteral("first.torrent")));
        QVERIFY(first_output.endsWith(QStringLiteral(".torrent")));

        input->setText(QStringLiteral("C:/payload/second.torrent"));
        const auto second_output = output->text();
        QVERIFY(second_output.endsWith(QStringLiteral("second.torrent")));
        QVERIFY(second_output != first_output);

        const auto explicit_output = QStringLiteral("C:/chosen/result.torrent");
        output->setText(explicit_output);
        input->setText(QStringLiteral("C:/payload/third.torrent"));
        QCOMPARE(output->text(), explicit_output);
    }

    void modifySourceControlUsesInfoIdentityPatch()
    {
        MainWindow window;
        auto* input = window.findChild<QLineEdit*>("editModifyTorrentPath");
        auto* source = window.findChild<QLineEdit*>("editModifySource");
        auto* clear_source = window.findChild<QCheckBox*>("chkModifyClearSource");
        auto* runner = window.findChild<GuiTaskRunner*>();
        QVERIFY(input != nullptr);
        QVERIFY(source != nullptr);
        QVERIFY(clear_source != nullptr);
        QVERIFY(runner != nullptr);

        const auto fixture =
            QDir(QStringLiteral(TORRENTCRAFT_GUI_TEST_SOURCE_DIR))
                .filePath(QStringLiteral("tests/fixtures/metadata/valid-v1.torrent"));
        input->setText(fixture);
        input->setFocus();
        QSignalSpy finished(runner, &GuiTaskRunner::finished);
        QTest::keyClick(input, Qt::Key_Return);
        QTRY_COMPARE(finished.count(), 1);

        QCOMPARE(source->text(), QStringLiteral("SRC"));
        QVERIFY(!source->isReadOnly());
        QVERIFY(clear_source->isEnabled());
        source->setText(QStringLiteral("GUI-SRC"));
        QCOMPARE(source->text(), QStringLiteral("GUI-SRC"));
    }

    void createPiecePreviewUsesCoreAutomaticPieceLength()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto content_path = directory.filePath(QStringLiteral("payload.bin"));
        QFile content(content_path);
        QVERIFY(content.open(QIODevice::WriteOnly));
        QVERIFY(content.resize(687194768));
        content.close();

        MainWindow window;
        auto* input = window.findChild<QLineEdit*>("editCreateInputPath");
        auto* calculate = window.findChild<QPushButton*>("btnCreateCalcPieces");
        auto* pieces = window.findChild<QLabel*>("lblCreatePieces");
        auto* runner = window.findChild<GuiTaskRunner*>();
        QVERIFY(input != nullptr);
        QVERIFY(calculate != nullptr);
        QVERIFY(pieces != nullptr);
        QVERIFY(runner != nullptr);

        input->setText(content_path);
        QSignalSpy finished(runner, &GuiTaskRunner::finished);
        calculate->click();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(pieces->text().contains(QStringLiteral("512 KiB")));
    }

    void createUsesCurrentTimeWithoutCustomDate()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto content_path = directory.filePath(QStringLiteral("payload.bin"));
        QFile content(content_path);
        QVERIFY(content.open(QIODevice::WriteOnly));
        QVERIFY(content.write("payload") > 0);
        content.close();
        const auto output_path = directory.filePath(QStringLiteral("created.torrent"));

        MainWindow window;
        auto* input = window.findChild<QLineEdit*>("editCreateInputPath");
        auto* output = window.findChild<QLineEdit*>("editCreateOutputPath");
        auto* custom_date = window.findChild<QCheckBox*>("chkCreateDate");
        auto* create = window.findChild<QPushButton*>("btnCreateTorrent");
        auto* runner = window.findChild<GuiTaskRunner*>();
        if (input == nullptr || output == nullptr || custom_date == nullptr || create == nullptr ||
            runner == nullptr)
        {
            QFAIL("Create controls were not found.");
        }

        input->setText(content_path);
        output->setText(output_path);
        custom_date->setChecked(false);
        QSignalSpy finished(runner, &GuiTaskRunner::finished);
        QTimer dismiss_notifications;
        connect(&dismiss_notifications, &QTimer::timeout, &window, [&window] {
            for (auto* dialog : window.findChildren<QDialog*>())
            {
                dialog->accept();
            }
        });
        dismiss_notifications.start(10);
        create->click();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(QFileInfo::exists(output_path));

        torrentutils::core::FileTorrentRepository repository;
        torrentutils::core::SystemClock clock;
        torrentutils::core::TorrentService service(repository, clock);
        const auto loaded = service.load(std::filesystem::path(output_path.toStdString()));
        if (!loaded)
        {
            QFAIL(loaded.error().message.c_str());
        }
        const auto creation_time =
            loaded.value().document().metadata().creation_time_unix_seconds();
        if (!creation_time.has_value())
        {
            QFAIL("Creation time was not written.");
        }
        const auto creation_seconds = *creation_time;
        const auto now = clock.now_unix_seconds();
        QVERIFY(creation_seconds >= now - 60);
        QVERIFY(creation_seconds <= now + 60);
    }

    void createdByPresetPreservesExplicitEmptyValue()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto config_path = directory.filePath(QStringLiteral("torrentcraft.json"));
        QFile config(config_path);
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(config.write(R"({
            "schema": "torrentcraft.config/v1",
            "defaults": {"created_by": "TorrentCraft"},
            "presets": {"Empty": {"created_by": ""}}
        })") > 0);
        config.close();

        MainWindow window;
        auto* config_edit = window.findChild<QLineEdit*>("editAdvancedConfigPath");
        auto* reload = window.findChild<QPushButton*>("btnAdvancedReloadConfig");
        auto* creator = window.findChild<QLineEdit*>("editCreateCreator");
        auto* custom = window.findChild<QCheckBox*>("chkCreateCreator");
        auto* preset_menu = window.findChild<QMenu*>("menuLoadPreset");
        if (config_edit == nullptr || reload == nullptr || creator == nullptr ||
            custom == nullptr || preset_menu == nullptr)
        {
            QFAIL("Created by preset controls were not found.");
        }

        config_edit->setText(config_path);
        reload->click();
        QCOMPARE(creator->text(), QStringLiteral("TorrentCraft"));
        QVERIFY(!custom->isChecked());
        QVERIFY(!creator->isEnabled());

        QAction* empty_preset = nullptr;
        for (auto* action : preset_menu->actions())
        {
            if (action->data().toString() == QStringLiteral("Empty"))
            {
                empty_preset = action;
                break;
            }
        }
        QVERIFY(empty_preset != nullptr);
        empty_preset->trigger();
        QVERIFY(creator->text().isEmpty());
        QVERIFY(custom->isChecked());
        QVERIFY(creator->isEnabled());
    }

    void hashProgressReportsPieceRate()
    {
        MainWindow window;
        auto* runner = window.findChild<GuiTaskRunner*>();
        QVERIFY(runner != nullptr);
        emit runner->progress(QStringLiteral("hashing"), 1, 10, 1024, 10 * 1024 * 1024);
        QTest::qWait(2);
        emit runner->progress(QStringLiteral("hashing"), 2, 10, 2 * 1024 * 1024, 10 * 1024 * 1024);
        const auto status = window.statusBar()->currentMessage();
        QVERIFY(status.contains(QStringLiteral("pieces/s")));
        QVERIFY(status.contains(QStringLiteral("I/O speed")));
        QVERIFY(status.contains(QStringLiteral("MiB/s")));
        QVERIFY(status.contains(QStringLiteral(" | Progress: ")));
        QVERIFY(status.contains(QStringLiteral(" | Hash speed: ")));
        QVERIFY(status.contains(QStringLiteral(" | I/O speed: ")));
    }

    void addTrackerDialogAcceptsTierBlocks()
    {
        TrackerEditDialog dialog(1, {}, QStringLiteral("Add tracker"), nullptr, true);
        auto* editor = dialog.findChild<QPlainTextEdit*>();
        QVERIFY(editor != nullptr);
        editor->setPlainText(QStringList{QStringLiteral("http://one.example/announce"),
                                         QStringLiteral("http://two.example/announce"), QString(),
                                         QStringLiteral("http://three.example/announce")}
                                 .join(QStringLiteral("\n")));
        dialog.accept();
        QCOMPARE(dialog.result(), QDialog::Accepted);
    }

    void closeWhileTaskRunningConfirmsCancellation()
    {
        MainWindow window;
        window.show();
        auto* runner = window.findChild<GuiTaskRunner*>();
        QVERIFY(runner != nullptr);
        QVERIFY(runner->start(
            [](const torrentutils::core::CancellationToken& token) {
                while (!token.is_cancelled())
                    QThread::msleep(1);
            },
            {}));

        QTimer::singleShot(0, &window, [&window] {
            auto* dialog = window.findChild<QDialog*>("OperationCloseDialog");
            QVERIFY(dialog != nullptr);
            auto* buttons = dialog->findChild<QDialogButtonBox*>("buttonBox");
            QVERIFY(buttons != nullptr);
            buttons->button(QDialogButtonBox::Yes)->click();
        });

        QVERIFY(window.close());
        QTRY_VERIFY(!runner->is_running());
        QVERIFY(!window.isVisible());
    }

    void closeWhileTaskRunningCanBeCancelled()
    {
        MainWindow window;
        window.show();
        auto* runner = window.findChild<GuiTaskRunner*>();
        QVERIFY(runner != nullptr);
        QVERIFY(runner->start(
            [](const torrentutils::core::CancellationToken& token) {
                while (!token.is_cancelled())
                    QThread::msleep(1);
            },
            {}));

        QTimer::singleShot(0, &window, [&window] {
            auto* dialog = window.findChild<QDialog*>("OperationCloseDialog");
            QVERIFY(dialog != nullptr);
            auto* buttons = dialog->findChild<QDialogButtonBox*>("buttonBox");
            QVERIFY(buttons != nullptr);
            buttons->button(QDialogButtonBox::No)->click();
        });

        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        runner->cancel();
        QTRY_VERIFY(!runner->is_running());
        window.close();
    }

    void guiTaskCompletionKeepsRunnerAndEventLoopAlive()
    {
        GuiTaskRunner runner;
        QSignalSpy finished(&runner, &GuiTaskRunner::finished);
        bool completion_called = false;
        QVERIFY(runner.start([](const torrentutils::core::CancellationToken&) {},
                             [&completion_called] { completion_called = true; }));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!runner.is_running());
        QVERIFY(completion_called);
    }

    void guiTaskFailureStillCleansUpWithoutExiting()
    {
        GuiTaskRunner runner;
        QSignalSpy finished(&runner, &GuiTaskRunner::finished);
        QSignalSpy failed(&runner, &GuiTaskRunner::failed);
        QVERIFY(runner.start(
            [](const torrentutils::core::CancellationToken&) {
                throw std::runtime_error("expected test failure");
            },
            {}));
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(finished.count(), 1);
        QVERIFY(!runner.is_running());
    }

    void inspectSummaryUsesDenseFiveRowLayout()
    {
        MainWindow window;
        auto* grid = window.findChild<QGridLayout*>("gridInspectSummary");
        QVERIFY(grid != nullptr);
        const auto widget_name = [](QLayoutItem* item) {
            return item != nullptr && item->widget() != nullptr ? item->widget()->objectName()
                                                                : QString{};
        };
        QCOMPARE(widget_name(grid->itemAtPosition(2, 2)), QStringLiteral("lblInspectCapability"));
        QCOMPARE(widget_name(grid->itemAtPosition(2, 3)),
                 QStringLiteral("lblInspectCapabilityValue"));
        QCOMPARE(widget_name(grid->itemAtPosition(3, 2)), QStringLiteral("lblInspectPayloadBytes"));
        QCOMPARE(widget_name(grid->itemAtPosition(3, 3)),
                 QStringLiteral("lblInspectPayloadBytesValue"));
        QCOMPARE(widget_name(grid->itemAtPosition(4, 2)), QStringLiteral("lblInspectCreatedBy"));
        QCOMPARE(widget_name(grid->itemAtPosition(4, 3)),
                 QStringLiteral("lblInspectCreatedByValue"));
    }

    void inspectFileTreeShowsRecursiveHumanReadableSizes()
    {
        FileTreeModel model(nullptr, FileTreeModel::Mode::Inspect);
        model.set_files(
            {make_file({"folder", "first.bin"}, 1024), make_file({"folder", "second.bin"}, 512)});

        QCOMPARE(model.columnCount(), 2);
        QVERIFY(model.canFetchMore({}));
        model.fetchMore({});
        const auto folder = child_named(model, {}, QStringLiteral("folder"));
        QVERIFY(folder.isValid());
        QCOMPARE(model.index(folder.row(), 1).data(Qt::UserRole).toULongLong(), 1536ULL);
        QVERIFY(model.index(folder.row(), 1).data().toString().contains(QStringLiteral("KiB")));
        QVERIFY(model.index(folder.row(), 1)
                    .data(Qt::ToolTipRole)
                    .toString()
                    .contains(QStringLiteral("bytes")));

        QVERIFY(model.canFetchMore(folder));
        model.fetchMore(folder);
        QCOMPARE(model.rowCount(folder), 2);
    }
    void inspectFileTreeIndexesLargeDirectories()
    {
        constexpr std::size_t file_count = 100'000;
        std::vector<FileEntry> files;
        files.reserve(file_count);
        for (std::size_t index = 0; index < file_count; ++index)
        {
            files.push_back(
                make_file({"payload", std::string("file-") + std::to_string(index)}, 1));
        }

        FileTreeModel model(nullptr, FileTreeModel::Mode::Inspect);
        model.set_files(files);
        model.fetchMore({});
        const auto folder = child_named(model, {}, QStringLiteral("payload"));
        QVERIFY(folder.isValid());
        QCOMPARE(model.index(folder.row(), 1).data(Qt::UserRole).toULongLong(),
                 static_cast<qulonglong>(file_count));

        QElapsedTimer timer;
        timer.start();
        model.fetchMore(folder);
        QVERIFY2(timer.elapsed() < 10'000,
                 qPrintable(
                     QStringLiteral("large directory expansion took %1 ms").arg(timer.elapsed())));
        QCOMPARE(model.rowCount(folder), static_cast<int>(file_count));
    }
    void verifyDirectoryAggregatesPartialProgress()
    {
        const auto first = make_file({"folder", "first.bin"}, 1024);
        const auto second = make_file({"folder", "second.bin"}, 1024);
        FileTreeModel model(nullptr, FileTreeModel::Mode::Verify);
        model.set_files({first, second});
        model.fetchMore({});
        const auto folder = child_named(model, {}, QStringLiteral("folder"));
        QVERIFY(folder.isValid());
        const auto folder_status = model.index(folder.row(), 2);

        model.apply_verification_progress(
            VerificationProgress{1, {{first.path(), 1024, 1024, 1024, 0}}, {}});
        QCOMPARE(folder_status.data().toString(), QStringLiteral("Checking 50%"));

        model.apply_verification_progress(
            VerificationProgress{2, {{second.path(), 1024, 1024, 1024, 0}}, {}});
        QCOMPARE(folder_status.data().toString(), QStringLiteral("Checked; awaiting result"));
        const auto totals = model.verification_totals();
        QCOMPARE(totals.expected_bytes, 2048ULL);
        QCOMPARE(totals.hashed_bytes, 2048ULL);

        model.apply_verification_report(VerificationReport{
            VerificationOutcome::Verified,
            2048,
            2048,
            2048,
            0,
            {{first.path(), 1024, 1024, 1024, 0, FileVerificationFinding::None},
             {second.path(), 1024, 1024, 1024, 0, FileVerificationFinding::None}},
        });
        QCOMPARE(folder_status.data().toString(), QStringLiteral("Verified"));
    }
    void verifyFileTreeTracksProgressFindingsAndPadding()
    {
        FileAttributes padding;
        padding.padding = true;
        const auto payload = make_file({"folder", "payload.bin"}, 1536);
        const auto padding_file = make_file({"folder", ".pad"}, 512, padding);
        FileTreeModel model(nullptr, FileTreeModel::Mode::Verify);
        model.set_files({payload, padding_file}, true);
        model.fetchMore({});
        const auto folder = child_named(model, {}, QStringLiteral("folder"));
        QVERIFY(folder.isValid());
        model.fetchMore(folder);
        const auto payload_index = child_named(model, folder, QStringLiteral("payload.bin"));
        const auto padding_index = child_named(model, folder, QStringLiteral("[Padding] .pad"));
        QVERIFY(payload_index.isValid());
        QVERIFY(padding_index.isValid());
        QCOMPARE(model.columnCount(), 3);
        QCOMPARE(model.index(payload_index.row(), 2, folder).data().toString(),
                 QStringLiteral("Not checked"));
        QCOMPARE(model.index(padding_index.row(), 2, folder).data().toString(),
                 QStringLiteral("N/A (padding)"));

        model.apply_verification_progress(
            VerificationProgress{2, {{payload.path(), 1536, 768, 768, 0}}, {}});
        QCOMPARE(model.index(payload_index.row(), 2, folder).data().toString(),
                 QStringLiteral("Checking 50%"));
        model.apply_verification_progress(
            VerificationProgress{1, {{payload.path(), 1536, 1536, 1536, 0}}, {}});
        QCOMPARE(model.index(payload_index.row(), 2, folder).data().toString(),
                 QStringLiteral("Checking 50%"));
        model.mark_verification_cancelled();
        QCOMPARE(model.index(payload_index.row(), 2, folder).data().toString(),
                 QStringLiteral("Cancelled"));
        model.reset_verification();
        model.apply_verification_progress(
            VerificationProgress{3, {{payload.path(), 1536, 768, 768, 0}}, {}});
        model.mark_verification_interrupted();
        QCOMPARE(model.index(payload_index.row(), 2, folder).data().toString(),
                 QStringLiteral("Interrupted"));
        model.reset_verification();

        const auto findings =
            FileVerificationFinding::HashMismatch | FileVerificationFinding::SharedPieceMismatch;
        model.apply_verification_report(VerificationReport{
            VerificationOutcome::Mismatched,
            1536,
            1536,
            0,
            1536,
            {{payload.path(), 1536, 1536, 0, 1536, findings}},
        });
        const auto status = model.index(payload_index.row(), 2, folder);
        QCOMPARE(status.data().toString(), QStringLiteral("Multiple issues"));
        const auto tooltip = status.data(Qt::ToolTipRole).toString();
        QVERIFY(tooltip.contains(QStringLiteral("Hash mismatch")));
        QVERIFY(tooltip.contains(QStringLiteral("Shared-piece mismatch")));
    }
};
void GuiLogoTest::initMain()
{
    Q_INIT_RESOURCE(TorrentCraft);
}

QTEST_MAIN(GuiLogoTest)
#include "gui_test.moc"
