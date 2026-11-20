#include "AboutDialog.hpp"
#include "FileTreeModel.hpp"
#include "FilterRulesDialog.hpp"
#include "GuiLogController.hpp"
#include "GuiTaskRunner.hpp"
#include "LineNumberPlainTextEdit.hpp"
#include "Logo.hpp"
#include "MainWindow.hpp"
#include "TemplateFieldDialog.hpp"
#include "TrackerEditDialog.hpp"
#include "TrackerRewritePanel.hpp"

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
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QModelIndex>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QWidget>
#include <QtTest>
#include <algorithm>
#include <limits>
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

    void loggerRotatesInNonAsciiFolder()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto folder = QDir(directory.path()).filePath(QStringLiteral("日志"));
        QVERIFY(QDir().mkpath(folder));
        const auto log_path = QDir(folder).filePath(QStringLiteral("会话.log"));
        GuiLogController logger;
        torrentutils::frontend::GuiPreferences preferences;
        preferences.logging_enabled = true;
        preferences.log_level = torrentutils::frontend::GuiLogLevel::Debug;
        preferences.log_path = log_path.toUtf8().toStdString();
        logger.configure(preferences, {});
        // Write past the 16 MiB limit so the log rotates to "<name>.1".
        const std::string filler(std::size_t{64} * 1024U, 'x');
        for (int index = 0; index < 270; ++index)
        {
            logger.log_event(torrentutils::core::LogLevel::Info, "gui", "test", "fill",
                             {{"data", filler}});
        }
        QVERIFY(logger.file_logging_active());
        QVERIFY(QFileInfo::exists(log_path + QStringLiteral(".1")));
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
        QCOMPARE(tree->topLevelItemCount(), 9);
        // The tracker rewrite engine and its Abseil dependency are listed with SPDX licenses.
        const auto re2 = tree->findItems(QStringLiteral("RE2"), Qt::MatchExactly);
        const auto abseil = tree->findItems(QStringLiteral("Abseil"), Qt::MatchExactly);
        QCOMPARE(re2.size(), 1);
        QCOMPARE(abseil.size(), 1);
        QCOMPARE(re2.front()->text(2), QStringLiteral("BSD-3-Clause"));
        QCOMPARE(abseil.front()->text(2), QStringLiteral("Apache-2.0"));
        const auto valid_licenses = QStringList{
            QStringLiteral("LGPL-3.0-only"), QStringLiteral("BSD-3-Clause"), QStringLiteral("MIT"),
            QStringLiteral("Apache-2.0"), QStringLiteral("BSL-1.0")};
        for (int row = 0; row < tree->topLevelItemCount(); ++row)
        {
            QVERIFY2(valid_licenses.contains(tree->topLevelItem(row)->text(2)),
                     qPrintable(tree->topLevelItem(row)->text(0)));
            const auto version = tree->topLevelItem(row)->text(1);
            QVERIFY(!version.isEmpty());
            QVERIFY(version != QStringLiteral("6.x (planned)"));
            QVERIFY(version != QStringLiteral("vcpkg baseline"));
            QVERIFY(version != QStringLiteral("transitive"));
        }

        // Every bundled component also has a license entry with the same SPDX identifier.
        auto* licenses = dialog.findChild<QTreeWidget*>("treeLicenses");
        QVERIFY(licenses != nullptr);
        for (int row = 0; row < tree->topLevelItemCount(); ++row)
        {
            const auto* component = tree->topLevelItem(row);
            const auto matches = licenses->findItems(component->text(0), Qt::MatchExactly);
            QVERIFY2(matches.size() == 1, qPrintable(component->text(0)));
            QCOMPARE(matches.front()->text(1), component->text(2));
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
        auto* runner = window.findChild<GuiTaskRunner*>();
        QVERIFY(source != nullptr);
        QVERIFY(reload != nullptr);
        QVERIFY(table != nullptr);
        QVERIFY(runner != nullptr);
        source->setText(directory.path());
        QSignalSpy finished(runner, &GuiTaskRunner::finished);
        reload->click();
        QTRY_COMPARE(finished.count(), 1);

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

    void verifyContentPickersFollowTorrentLayoutAndRejectMismatch()
    {
        MainWindow window;
        auto* file_button = window.findChild<QPushButton*>("btnVerifySelectContentFile");
        auto* folder_button = window.findChild<QPushButton*>("btnVerifySelectFolder");
        auto* torrent_input = window.findChild<QLineEdit*>("editVerifyTorrentPath");
        auto* content_input = window.findChild<QLineEdit*>("editVerifyContentPath");
        auto* content_group = window.findChild<QWidget*>("grpVerifyContent");
        auto* start_button = window.findChild<QPushButton*>("btnVerifyStart");
        auto* runner = window.findChild<GuiTaskRunner*>();
        QVERIFY(file_button != nullptr);
        QVERIFY(folder_button != nullptr);
        QVERIFY(torrent_input != nullptr);
        QVERIFY(content_input != nullptr);
        QVERIFY(content_group != nullptr);
        QVERIFY(start_button != nullptr);
        QVERIFY(runner != nullptr);

        // Unknown layout: both entry points stay available.
        QVERIFY(!file_button->isHidden());
        QVERIFY(!folder_button->isHidden());

        const auto fixtures = QDir(QStringLiteral(TORRENTCRAFT_GUI_TEST_SOURCE_DIR))
                                  .filePath(QStringLiteral("tests/fixtures/metadata"));
        const auto single_torrent = fixtures + QStringLiteral("/valid-v1.torrent");
        const auto multi_torrent = fixtures + QStringLiteral("/valid-v1-multifile.torrent");

        torrent_input->setText(single_torrent);
        QSignalSpy single_finished(runner, &GuiTaskRunner::finished);
        QTest::keyClick(torrent_input, Qt::Key_Return);
        QTRY_COMPARE(single_finished.count(), 1);
        QVERIFY(!file_button->isHidden());
        QVERIFY(folder_button->isHidden());

        // The single-file layout rejects a directory content root and notes the mismatch.
        QTemporaryDir content_directory;
        QVERIFY(content_directory.isValid());
        content_input->setText(content_directory.path());
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("single-file")));

        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [&window] {
            for (auto* dialog : window.findChildren<QDialog*>())
            {
                dialog->accept();
            }
        });
        dismiss.start(10);
        start_button->click();
        dismiss.stop();
        QVERIFY(!runner->is_running());
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("single-file")));

        // Dropping a folder sets the path exactly and never rewrites it.
        QMimeData drop;
        drop.setUrls({QUrl::fromLocalFile(content_directory.path())});
        QVERIFY(send_drop(content_group, drop));
        QCOMPARE(QDir::fromNativeSeparators(content_input->text()),
                 QDir::fromNativeSeparators(content_directory.path()));

        // A directory torrent switches the picker to the folder entry point.
        torrent_input->setText(multi_torrent);
        QSignalSpy multi_finished(runner, &GuiTaskRunner::finished);
        QTest::keyClick(torrent_input, Qt::Key_Return);
        QTRY_COMPARE(multi_finished.count(), 1);
        QVERIFY(file_button->isHidden());
        QVERIFY(!folder_button->isHidden());
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

    void multilineFieldsScaleWithTheActiveFont()
    {
        MainWindow window;
        window.show();
        QCoreApplication::processEvents();

        const auto verify_height = [&window](const char* name, const int visible_lines) {
            const auto* editor = window.findChild<QPlainTextEdit*>(name);
            QVERIFY(editor != nullptr);
            QVERIFY(editor->minimumHeight() >= editor->fontMetrics().lineSpacing() * visible_lines);
            QVERIFY(editor->height() >= editor->minimumHeight());
        };

        verify_height("editCreateComment", 1);
        verify_height("editModifyComment", 1);
        verify_height("editCreateWebSeeds", 1);
        verify_height("editModifyWebSeeds", 1);
        verify_height("editModifyDhtNodes", 1);
        verify_height("editAdvancedDefaultTrackerTiers", 3);
        verify_height("editAdvancedDefaultWebSeeds", 3);
    }

    void constrainedHeightUsesTabScrollAreas()
    {
        MainWindow window;
        window.resize(700, 600);
        window.show();
        QCoreApplication::processEvents();
        QCOMPARE(window.size(), QSize(700, 600));

        auto* tabs = window.findChild<QTabWidget*>("tabMain");
        QVERIFY(tabs != nullptr);
        struct TabLayout
        {
            const char* tab_name;
            const char* scroll_name;
            const char* group_name;
        };
        for (const auto& [tab_name, scroll_name, group_name] : std::initializer_list<TabLayout>{
                 {"Create", "scrollCreate", "grpCreateSettings"},
                 {"Modify", "scrollModify", "grpModifyFields"},
                 {"Advanced", "scrollAdvanced", "grpAdvancedDefaults"}})
        {
            auto* tab = window.findChild<QWidget*>(tab_name);
            auto* scroll = window.findChild<QScrollArea*>(scroll_name);
            auto* content_group = window.findChild<QGroupBox*>(group_name);
            QVERIFY(tab != nullptr);
            QVERIFY(scroll != nullptr);
            QVERIFY(content_group != nullptr);
            tabs->setCurrentWidget(tab);
            QCoreApplication::processEvents();
            QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
            QVERIFY(content_group->height() >= content_group->minimumSizeHint().height());
        }
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

    void templateFieldDialogSelectsDefaultsAndDisablesUnavailableFields()
    {
        namespace frontend = torrentutils::frontend;
        const auto fixture = QDir(QStringLiteral(TORRENTCRAFT_GUI_TEST_SOURCE_DIR))
                                 .filePath(QStringLiteral(
                                     "tests/fixtures/metadata/unsupported-piece-size-v1.torrent"));
        auto loaded = frontend::load_torrent_template(
            std::filesystem::u8path(fixture.toUtf8().toStdString()));
        QVERIFY(loaded);
        TemplateFieldDialog dialog(QStringLiteral("unsupported-piece-size-v1"), loaded.value());

        auto* trackers = dialog.checkbox(frontend::TemplateField::Trackers);
        auto* piece_size = dialog.checkbox(frontend::TemplateField::PieceSize);
        auto* comment = dialog.checkbox(frontend::TemplateField::Comment);
        auto* source = dialog.checkbox(frontend::TemplateField::Source);
        QVERIFY(trackers != nullptr);
        QVERIFY(piece_size != nullptr);
        QVERIFY(comment != nullptr);
        QVERIFY(source != nullptr);
        QCOMPARE(trackers->objectName(), QStringLiteral("chkTemplateField_trackers"));
        QVERIFY(trackers->isEnabled() && trackers->isChecked());
        QVERIFY(!piece_size->isEnabled() && !piece_size->isChecked());
        QVERIFY(piece_size->toolTip().contains(QStringLiteral("33554432")));
        QVERIFY(!source->isEnabled());
        QVERIFY(comment->isEnabled() && !comment->isChecked());
        QVERIFY(dialog.findChildren<QLabel*>().size() > 0);

        const auto defaults = dialog.selected_fields();
        QVERIFY(std::find(defaults.begin(), defaults.end(), frontend::TemplateField::Trackers) !=
                defaults.end());
        QVERIFY(std::find(defaults.begin(), defaults.end(), frontend::TemplateField::PieceSize) ==
                defaults.end());

        comment->setChecked(true);
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(buttons != nullptr);
        for (auto* box : dialog.findChildren<QCheckBox*>())
        {
            box->setChecked(false);
        }
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        comment->setChecked(true);
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QCOMPARE(dialog.selected_fields(),
                 std::vector<frontend::TemplateField>{frontend::TemplateField::Comment});
    }

    void templateFieldDialogShowsLoadWarningsAndUnreadableFields()
    {
        namespace frontend = torrentutils::frontend;
        const auto fixture =
            QDir(QStringLiteral(TORRENTCRAFT_GUI_TEST_SOURCE_DIR))
                .filePath(QStringLiteral(
                    "tests/fixtures/metadata/partially-invalid-template-v1.torrent"));
        auto loaded = frontend::load_torrent_template(
            std::filesystem::u8path(fixture.toUtf8().toStdString()));
        QVERIFY(loaded);
        TemplateFieldDialog dialog(QStringLiteral("partially-invalid-template-v1"), loaded.value());

        auto* warnings = dialog.findChild<QLabel*>(QStringLiteral("lblTemplateWarnings"));
        QVERIFY(warnings != nullptr);
        QVERIFY(warnings->text().contains(QStringLiteral("Trackers")));
        QVERIFY(warnings->text().contains(QStringLiteral("Web seeds")));
        auto* trackers = dialog.checkbox(frontend::TemplateField::Trackers);
        auto* source = dialog.checkbox(frontend::TemplateField::Source);
        QVERIFY(trackers != nullptr && source != nullptr);
        QVERIFY(trackers->isEnabled() && trackers->isChecked());
        QVERIFY(!source->isEnabled());
        QVERIFY(source->toolTip().contains(QStringLiteral("unreadable")));
    }

    void templateFieldDialogTranslatesWithLanguage()
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
        QVERIFY(chinese != nullptr);
        QVERIFY(english != nullptr);

        chinese->trigger();
        QCOMPARE(TemplateFieldDialog::field_label(torrentutils::frontend::TemplateField::WebSeeds),
                 QStringLiteral("Web Seed"));
        english->trigger();
        QCOMPARE(TemplateFieldDialog::field_label(torrentutils::frontend::TemplateField::WebSeeds),
                 QStringLiteral("Web seeds"));
    }

    void trackerRewritePanelPreviewsAndReportsRuleErrors()
    {
        namespace core = torrentutils::core;
        TrackerRewritePanel panel;
        std::vector<core::TrackerTier> tiers;
        tiers.push_back(
            core::TrackerTier::create(
                {core::TrackerUrl::parse("https://tracker.old.example/announce/KEY").value(),
                 core::TrackerUrl::parse("udp://other.example:6969/announce").value()})
                .value());
        panel.set_preview_source(QStringLiteral("sample.torrent"),
                                 core::TrackerList::create(std::move(tiers)).value());
        QVERIFY(!panel.rules().has_value());

        QSignalSpy changed(&panel, &TrackerRewritePanel::rulesChanged);
        QCOMPARE(panel.rule_count(), 1);
        QVERIFY(panel.match_case_button(0)->isChecked());
        QVERIFY(!panel.regex_button(0)->isChecked());
        QVERIFY(!panel.remove_button(0)->isEnabled());
        QCOMPARE(panel.find_label(0)->buddy(), panel.find_edit(0));
        // Rules and preview share a splitter so either can be given more room.
        auto* splitter = panel.findChild<QSplitter*>(QStringLiteral("splitTrackerRewrite"));
        QVERIFY(splitter != nullptr);
        QCOMPARE(splitter->count(), 2);
        QVERIFY(splitter->widget(0)->isAncestorOf(panel.find_edit(0)));
        QVERIFY(splitter->widget(1)->isAncestorOf(panel.preview_table()));

        // Adding a rule grows the rules pane; removing it gives the room back to the preview.
        panel.resize(720, 600);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        // The single rule is fitted on first show.
        QTRY_COMPARE(splitter->sizes().at(0), splitter->widget(0)->sizeHint().height());
        const auto one_rule = splitter->sizes();
        QCOMPARE(panel.add_rule(), 1);
        QTRY_VERIFY(splitter->sizes().at(0) > one_rule.at(0));
        panel.remove_button(1)->click();
        QTRY_COMPARE(splitter->sizes(), one_rule);
        // Plain text by default: the dots are literal.
        panel.find_edit(0)->setText(QStringLiteral("tracker.old.example"));
        panel.replace_edit(0)->setText(QStringLiteral("tracker.new.example"));
        panel.refresh();
        QVERIFY(panel.rules().has_value());
        QVERIFY(!changed.isEmpty());
        QCOMPARE(changed.back().at(0).toBool(), true);
        QVERIFY(panel.error_label()->isHidden());

        const auto* model = panel.preview_table()->model();
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("1"));
        QCOMPARE(model->index(0, 2).data().toString(),
                 QStringLiteral("https://tracker.new.example/announce/KEY"));
        QCOMPARE(model->index(0, 3).data().toString(), QStringLiteral("Changed"));
        QCOMPARE(model->index(1, 3).data().toString(), QStringLiteral("Unchanged"));

        // Match case is on, so a differently cased find leaves the URL alone.
        panel.find_edit(0)->setText(QStringLiteral("TRACKER.old.example"));
        panel.refresh();
        QCOMPARE(model->index(0, 3).data().toString(), QStringLiteral("Unchanged"));
        panel.match_case_button(0)->click();
        panel.refresh();
        QCOMPARE(model->index(0, 3).data().toString(), QStringLiteral("Changed"));

        // Rows apply top to bottom; an empty row is ignored and an invalid one is named by number.
        QCOMPARE(panel.add_rule(), 1);
        QCOMPARE(panel.add_rule(), 2);
        QVERIFY(panel.remove_button(0)->isEnabled());
        panel.find_edit(2)->setText(QStringLiteral("(passkey"));
        panel.regex_button(2)->click();
        panel.refresh();
        QVERIFY(!panel.rules().has_value());
        QCOMPARE(changed.back().at(0).toBool(), false);
        QVERIFY(!panel.error_label()->isHidden());
        QVERIFY(panel.error_label()->text().startsWith(QStringLiteral("Rule 3:")));
        QVERIFY(!panel.error_label()->text().contains(QStringLiteral("passkey")));
        QVERIFY(!panel.find_edit(2)->styleSheet().isEmpty());

        // Removing the bad row restores the rules; regex rows use $1 for groups.
        panel.remove_button(2)->click();
        QCOMPARE(panel.rule_count(), 2);
        panel.find_edit(1)->setText(QStringLiteral("^udp://(other\\.example):6969"));
        panel.replace_edit(1)->setText(QStringLiteral("https://$1"));
        panel.regex_button(1)->click();
        panel.refresh();
        QVERIFY(panel.rules().has_value());
        QVERIFY(panel.error_label()->isHidden());
        QVERIFY(panel.find_edit(0)->styleSheet().isEmpty());
        QCOMPARE(model->index(1, 2).data().toString(),
                 QStringLiteral("https://other.example/announce"));

        panel.find_edit(1)->setText(QStringLiteral("^udp:"));
        panel.replace_edit(1)->setText(QStringLiteral("ftp:"));
        panel.refresh();
        QCOMPARE(model->index(1, 3).data().toString().left(8), QStringLiteral("Invalid:"));

        // Clearing leaves one empty row with default toggles.
        panel.clear();
        QCOMPARE(panel.rule_count(), 1);
        QVERIFY(panel.find_edit(0)->text().isEmpty());
        QVERIFY(panel.match_case_button(0)->isChecked());
        QVERIFY(!panel.regex_button(0)->isChecked());
        QVERIFY(!panel.rules().has_value());
    }

    void trackerRewriteModeBatchRewritesSubfolders()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto fixtures = QDir(QStringLiteral(TORRENTCRAFT_GUI_TEST_SOURCE_DIR))
                                  .filePath(QStringLiteral("tests/fixtures/metadata"));
        QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("in/sub")));
        QVERIFY(QFile::copy(fixtures + QStringLiteral("/valid-v1.torrent"),
                            directory.filePath(QStringLiteral("in/a.torrent"))));
        QVERIFY(QFile::copy(fixtures + QStringLiteral("/valid-v1-multifile.torrent"),
                            directory.filePath(QStringLiteral("in/sub/b.torrent"))));

        MainWindow window;
        auto* source = window.findChild<QLineEdit*>("editTrackerSourcePath");
        auto* output = window.findChild<QLineEdit*>("editTrackerOutputPath");
        auto* recursive = window.findChild<QCheckBox*>("chkTrackerRecursive");
        auto* reload = window.findChild<QPushButton*>("btnTrackerReloadFolder");
        auto* convert = window.findChild<QPushButton*>("btnTrackerBatchConvert");
        auto* rewrite = window.findChild<QRadioButton*>("radTrackerRewrite");
        auto* stack = window.findChild<QStackedWidget*>("stackTrackerMode");
        auto* fields_splitter = window.findChild<QSplitter*>("splitTrackerFields");
        auto* table = window.findChild<QTableView*>("tblTrackerTorrents");
        auto* panel = window.findChild<TrackerRewritePanel*>();
        auto* runner = window.findChild<GuiTaskRunner*>();
        QVERIFY(source && output && recursive && reload && convert && rewrite && stack && table &&
                panel && runner && fields_splitter);
        // The tracker editor and the torrent list share a splitter.
        QCOMPARE(fields_splitter->widget(0), stack);
        QVERIFY(fields_splitter->widget(1)->isAncestorOf(table));
        QVERIFY(!source->placeholderText().isEmpty());

        QSignalSpy finished(runner, &GuiTaskRunner::finished);
        recursive->setChecked(true);
        source->setText(directory.filePath(QStringLiteral("in")));
        reload->click();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(table->model()->rowCount(), 2);
        QCOMPARE(table->model()->index(1, 0).data().toString(), QStringLiteral("sub/b.torrent"));

        rewrite->setChecked(true);
        QCOMPARE(stack->currentIndex(), 1);
        QVERIFY(!convert->isEnabled());
        panel->find_edit(0)->setText(QStringLiteral("tracker.example"));
        panel->replace_edit(0)->setText(QStringLiteral("tracker.new.example"));
        panel->refresh();
        QVERIFY(convert->isEnabled());

        // Selecting a torrent previews it without leaving the folder batch.
        table->clicked(table->model()->index(1, 0));
        QTRY_COMPARE(finished.count(), 2);
        QCOMPARE(panel->preview_table()->model()->index(0, 2).data().toString(),
                 QStringLiteral("https://tracker.new.example/announce"));

        panel->preview_all_button()->click();
        QTRY_COMPARE(finished.count(), 3);
        QVERIFY(table->model()->index(0, 2).data().toString().startsWith(
            QStringLiteral("Will change")));

        output->setText(directory.filePath(QStringLiteral("out")));
        QTimer dismiss;
        QObject::connect(&dismiss, &QTimer::timeout, [] {
            if (auto* modal = QApplication::activeModalWidget())
            {
                modal->close();
            }
        });
        dismiss.start(20);
        // Convert straight after an edit must use the edited rule, not the one the debounced
        // preview compiled earlier.
        panel->replace_edit(0)->setText(QStringLiteral("tracker.newer.example"));
        convert->click();
        QTRY_COMPARE(finished.count(), 4);
        QTRY_VERIFY(QApplication::activeModalWidget() == nullptr);
        dismiss.stop();
        QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("out/a.torrent"))));
        QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("out/sub/b.torrent"))));
        QCOMPARE(table->model()->index(1, 2).data().toString(), QStringLiteral("Updated"));

        torrentutils::core::FileTorrentRepository repository;
        torrentutils::core::SystemClock clock;
        const torrentutils::core::TorrentService service(repository, clock);
        const auto reloaded = service.load(std::filesystem::u8path(
            directory.filePath(QStringLiteral("out/sub/b.torrent")).toUtf8().toStdString()));
        QVERIFY(reloaded);
        QCOMPARE(QString::fromStdString(
                     reloaded.value().document().trackers().tiers()[0].trackers()[0].value()),
                 QStringLiteral("https://tracker.newer.example/announce"));
    }

    void trackerFileListReloadsTranslatesAndCapsFailures()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("in")));
        constexpr int kBrokenFiles = 55;
        for (int index = 0; index < kBrokenFiles; ++index)
        {
            QFile broken(directory.filePath(QStringLiteral("in/broken-%1.torrent").arg(index)));
            QVERIFY(broken.open(QIODevice::WriteOnly));
            broken.write("not bencode");
        }

        MainWindow window;
        auto* config_path = window.findChild<QLineEdit*>("editAdvancedConfigPath");
        auto* source = window.findChild<QLineEdit*>("editTrackerSourcePath");
        auto* output = window.findChild<QLineEdit*>("editTrackerOutputPath");
        auto* reload = window.findChild<QPushButton*>("btnTrackerReloadFolder");
        auto* convert = window.findChild<QPushButton*>("btnTrackerBatchConvert");
        auto* rewrite = window.findChild<QRadioButton*>("radTrackerRewrite");
        auto* table = window.findChild<QTableView*>("tblTrackerTorrents");
        auto* panel = window.findChild<TrackerRewritePanel*>();
        auto* runner = window.findChild<GuiTaskRunner*>();
        auto* chinese = window.findChild<QAction*>("actionLanguageChinese");
        auto* english = window.findChild<QAction*>("actionLanguageEnglish");
        QVERIFY(config_path && source && output && reload && convert && rewrite && table && panel &&
                runner && chinese && english);
        config_path->setText(directory.filePath(QStringLiteral("torrentcraft.json")));
        QSignalSpy finished(runner, &GuiTaskRunner::finished);

        // Reloading replaces the list model; the old one must be freed, not kept as a child.
        source->setText(directory.filePath(QStringLiteral("in")));
        reload->click();
        QTRY_COMPARE(finished.count(), 1);
        const auto* first_model = table->model();
        QPointer<QAbstractItemModel> first_model_guard(table->model());
        reload->click();
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY(table->model() != first_model);
        QTRY_VERIFY(first_model_guard.isNull());
        QCOMPARE(table->model()->rowCount(), kBrokenFiles);

        // The list's headers and statuses follow the language.
        chinese->trigger();
        QCOMPARE(table->model()->headerData(0, Qt::Horizontal).toString(),
                 QStringLiteral("文件名"));
        QCOMPARE(table->model()->index(0, 2).data().toString(), QStringLiteral("就绪"));
        english->trigger();
        QCOMPARE(table->model()->index(0, 2).data().toString(), QStringLiteral("Ready"));

        // Every file fails to load; the summary lists a bounded number of them.
        rewrite->setChecked(true);
        panel->find_edit(0)->setText(QStringLiteral("a"));
        panel->replace_edit(0)->setText(QStringLiteral("b"));
        panel->refresh();
        output->setText(directory.filePath(QStringLiteral("out")));
        QString summary;
        QTimer dismiss;
        QObject::connect(&dismiss, &QTimer::timeout, [&summary] {
            if (auto* modal = QApplication::activeModalWidget())
            {
                if (auto* text = modal->findChild<QPlainTextEdit*>("editNotificationMessage"))
                {
                    summary = text->toPlainText();
                }
                modal->close();
            }
        });
        dismiss.start(20);
        convert->click();
        // While the batch runs, the source, recursion and mode cannot change underneath it.
        QVERIFY(!source->isEnabled());
        QVERIFY(!rewrite->isEnabled());
        QVERIFY(!window.findChild<QCheckBox*>("chkTrackerRecursive")->isEnabled());
        QTRY_COMPARE(finished.count(), 3);
        QTRY_VERIFY(!summary.isEmpty());
        QVERIFY(source->isEnabled());
        dismiss.stop();
        QCOMPARE(summary.count(QStringLiteral("broken-")), 50);
        QVERIFY2(summary.contains(QStringLiteral("and 5 more")), qPrintable(summary));
        QCOMPARE(table->model()->index(0, 2).data().toString(), QStringLiteral("Failed"));
        chinese->trigger();
        QCOMPARE(table->model()->index(0, 2).data().toString(), QStringLiteral("失败"));
        english->trigger();
    }

    void failedAdvancedApplyLeavesNoPartialSettingsToSaveLater()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto config_file = directory.filePath(QStringLiteral("torrentcraft.json"));
        {
            QFile config(config_file);
            QVERIFY(config.open(QIODevice::WriteOnly));
            config.write(R"({"schema": "torrentcraft.config/v1"})");
        }
        MainWindow window;
        auto* config_path = window.findChild<QLineEdit*>("editAdvancedConfigPath");
        auto* comment = window.findChild<QLineEdit*>("editAdvancedDefaultComment");
        auto* log_path = window.findChild<QLineEdit*>("editAdvancedLogPath");
        auto* apply = window.findChild<QPushButton*>("btnAdvancedApply");
        auto* chinese = window.findChild<QAction*>("actionLanguageChinese");
        auto* english = window.findChild<QAction*>("actionLanguageEnglish");
        QVERIFY(config_path && comment && log_path && apply && chinese && english);
        config_path->setText(config_file);
        // Switching language loads the config file into the window.
        chinese->trigger();
        english->trigger();

        // The defaults apply first and succeed; the oversized log path then fails the Apply.
        comment->setText(QStringLiteral("partial-marker"));
        log_path->setMaxLength(std::numeric_limits<int>::max());
        log_path->setText(QString(qsizetype{2} * 1024 * 1024, QLatin1Char('x')));
        QTimer dismiss;
        QObject::connect(&dismiss, &QTimer::timeout, [] {
            if (auto* modal = QApplication::activeModalWidget())
            {
                modal->close();
            }
        });
        dismiss.start(20);
        apply->click();
        dismiss.stop();

        // Switching language saves the config; it must not carry the failed Apply's defaults.
        chinese->trigger();
        english->trigger();
        QFile saved(config_file);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto saved_text = saved.readAll();
        QVERIFY(!saved_text.contains("partial-marker"));
    }

    void resetButtonsLeavePagesAloneWhileATaskRuns()
    {
        MainWindow window;
        window.show();
        auto* runner = window.findChild<GuiTaskRunner*>();
        QVERIFY(runner != nullptr);
        const std::pair<const char*, const char*> pages[] = {
            {"btnCreateReset", "editCreateInputPath"},
            {"btnModifyReset", "editModifyTorrentPath"},
            {"btnInspectReset", "editInspectTorrentPath"},
            {"btnVerifyReset", "editVerifyTorrentPath"},
            {"btnTrackerReset", "editTrackerSourcePath"},
        };
        for (const auto& [button_name, edit_name] : pages)
        {
            auto* edit = window.findChild<QLineEdit*>(edit_name);
            QVERIFY2(edit != nullptr, edit_name);
            const QSignalBlocker block(edit);
            edit->setText(QStringLiteral("kept"));
        }
        QVERIFY(runner->start(
            [](const torrentutils::core::CancellationToken& token) {
                while (!token.is_cancelled())
                    QThread::msleep(1);
            },
            {}));

        // A worker may be reading the page's loaded torrent, so Reset must not clear it.
        for (const auto& [button_name, edit_name] : pages)
        {
            auto* button = window.findChild<QPushButton*>(button_name);
            QVERIFY2(button != nullptr, button_name);
            button->click();
            QCOMPARE(window.findChild<QLineEdit*>(edit_name)->text(), QStringLiteral("kept"));
        }
        runner->cancel();
        QTRY_VERIFY(!runner->is_running());
    }

    void everyPageScrollsInsteadOfSqueezingGroupsInSmallWindows()
    {
        // Large fonts (HiDPI, accessibility settings) make layouts need more than fixed sizes.
        const auto original_font = QApplication::font();
        auto large_font = original_font;
        if (original_font.pointSizeF() > 0)
        {
            large_font.setPointSizeF(original_font.pointSizeF() * 1.6);
        }
        else
        {
            large_font.setPixelSize(original_font.pixelSize() * 8 / 5);
        }
        QApplication::setFont(large_font);
        const auto restore_font = qScopeGuard([&] { QApplication::setFont(original_font); });
        MainWindow window;
        auto* tabs = window.findChild<QTabWidget*>("tabMain");
        QVERIFY(tabs != nullptr);
        window.resize(window.minimumSize());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        // A fixed minimumSize on a group overrides what its layout needs, so a short window
        // squeezed the group and its widgets overlapped instead of the page scrolling.
        for (int tab = 0; tab < tabs->count(); ++tab)
        {
            tabs->setCurrentIndex(tab);
            QCoreApplication::processEvents();
            for (auto* group : tabs->widget(tab)->findChildren<QGroupBox*>())
            {
                if (!group->isVisible())
                {
                    continue;
                }
                const auto message = group->objectName().toUtf8();
                QVERIFY2(group->height() >= group->minimumSizeHint().height(), message.constData());
            }
        }
    }

    void noPageScrollsSidewaysAtTheMinimumWindowSize()
    {
        // Long option labels in a row of action buttons once widened the Modify page past the
        // minimum window width, so the page needed a horizontal scroll bar.
        MainWindow window;
        auto* tabs = window.findChild<QTabWidget*>("tabMain");
        QVERIFY(tabs != nullptr);
        window.resize(window.minimumSize());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (int tab = 0; tab < tabs->count(); ++tab)
        {
            tabs->setCurrentIndex(tab);
            QCoreApplication::processEvents();
            for (auto* area : tabs->widget(tab)->findChildren<QScrollArea*>())
            {
                if (!area->isVisible() || area->widget() == nullptr)
                {
                    continue;
                }
                // Compare what the page needs, not its current width: the scroll area may not have
                // resized the page to the final viewport yet (seen under xvfb with Qt 6.11).
                const int needed = area->widget()->minimumSizeHint().width();
                const int available = area->viewport()->width();
                if (needed <= available)
                {
                    continue;
                }
                // Name the widest rows and widgets so a failure on another Qt version or font shows
                // what needs the width.
                QList<QPair<int, QString>> widest;
                const auto add = [&widest](const QObject* object, int width) {
                    widest.append(
                        {width, QStringLiteral("%1 %2 (%3)")
                                    .arg(QString::fromLatin1(object->metaObject()->className()),
                                         object->objectName())
                                    .arg(width)});
                };
                for (auto* layout : area->widget()->findChildren<QLayout*>())
                {
                    add(layout, layout->minimumSize().width());
                }
                for (auto* child : area->widget()->findChildren<QWidget*>())
                {
                    if (child->isVisible())
                    {
                        add(child, child->minimumSizeHint().width());
                    }
                }
                std::sort(widest.begin(), widest.end(), [](const auto& left, const auto& right) {
                    return left.first > right.first;
                });
                QStringList names;
                for (qsizetype index = 0; index < widest.size() && index < 8; ++index)
                {
                    names.append(widest.at(index).second);
                }
                const auto message = QStringLiteral("%1 needs %2 px, viewport %3 px; widest: %4")
                                         .arg(tabs->tabText(tab))
                                         .arg(needed)
                                         .arg(available)
                                         .arg(names.join(QStringLiteral(", ")))
                                         .toUtf8();
                QFAIL(message.constData());
            }
        }
    }

    void trackerPageScrollsInsteadOfOverlappingInSmallWindows()
    {
        MainWindow window;
        auto* tabs = window.findChild<QTabWidget*>("tabMain");
        auto* page = window.findChild<QWidget*>("Tracker");
        auto* rewrite = window.findChild<QRadioButton*>("radTrackerRewrite");
        auto* fields = window.findChild<QGroupBox*>("grpTrackerFields");
        auto* rewrite_splitter = window.findChild<QSplitter*>("splitTrackerRewrite");
        auto* add_rule = window.findChild<QPushButton*>("btnTrackerAddRule");
        auto* preview = window.findChild<QTableView*>("tblTrackerRewritePreview");
        auto* panel = window.findChild<TrackerRewritePanel*>();
        QVERIFY(tabs && page && rewrite && fields && rewrite_splitter && add_rule && preview &&
                panel);
        tabs->setCurrentWidget(page);
        rewrite->setChecked(true);
        panel->add_rule();
        window.resize(window.minimumSize());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCoreApplication::processEvents();

        // A fixed minimum on the Fields group once let the page squeeze it below what its
        // contents need, so the rule footer overlapped the rules and preview.
        QVERIFY(fields->height() >= fields->minimumSizeHint().height());
        for (int index = 0; index < rewrite_splitter->count(); ++index)
        {
            auto* pane = rewrite_splitter->widget(index);
            QVERIFY(pane->height() >= pane->minimumSizeHint().height());
        }
        const auto add_bottom = add_rule->mapTo(panel, QPoint(0, add_rule->height())).y();
        QVERIFY(add_bottom <= preview->mapTo(panel, QPoint(0, 0)).y());
    }

    void trackerRewritePanelTranslatesWithLanguage()
    {
        QTemporaryDir config_directory;
        QVERIFY(config_directory.isValid());
        QFile existing_config(config_directory.filePath(QStringLiteral("torrentcraft.json")));
        QVERIFY(existing_config.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(existing_config.write(R"({"schema":"torrentcraft.config/v1"})") > 0);
        existing_config.close();
        MainWindow window;
        auto* config_path = window.findChild<QLineEdit*>(QStringLiteral("editAdvancedConfigPath"));
        auto* panel = window.findChild<TrackerRewritePanel*>();
        auto* rewrite = window.findChild<QRadioButton*>("radTrackerRewrite");
        QVERIFY(config_path && panel && rewrite);
        config_path->setText(config_directory.filePath(QStringLiteral("torrentcraft.json")));
        auto* chinese = window.findChild<QAction*>(QStringLiteral("actionLanguageChinese"));
        auto* english = window.findChild<QAction*>(QStringLiteral("actionLanguageEnglish"));
        QVERIFY(chinese && english);

        chinese->trigger();
        // Language changes reach child widgets through posted events.
        QTRY_COMPARE(panel->preview_all_button()->text(), QStringLiteral("全部预览"));
        QCOMPARE(rewrite->text(), QStringLiteral("改写（查找替换）"));
        QCOMPARE(panel->add_rule_button()->text(), QStringLiteral("+ 添加规则"));
        QCOMPARE(panel->find_label(0)->text(), QStringLiteral("查找"));
        QCOMPARE(panel->match_case_button(0)->toolTip(), QStringLiteral("区分大小写"));
        english->trigger();
        QTRY_COMPARE(panel->preview_all_button()->text(), QStringLiteral("Preview all"));
        QCOMPARE(panel->find_label(0)->text(), QStringLiteral("Find"));
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

    void advancedFilterDefaultsRemainSeparateFromPresetEffectiveValues()
    {
        QTemporaryDir config_directory;
        QVERIFY(config_directory.isValid());
        const auto config_path = config_directory.filePath(QStringLiteral("torrentcraft.json"));
        QFile config(config_path);
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(config.write(R"({
            "schema": "torrentcraft.config/v1",
            "defaults": {
                "file_filter": {
                    "mode": "common-artifacts",
                    "case_sensitive": false,
                    "patterns": ["*.tmp"]
                }
            },
            "presets": {
                "Custom": {
                    "file_filter": {
                        "mode": "custom-rules",
                        "case_sensitive": true,
                        "patterns": ["cache/"]
                    }
                }
            },
            "default_preset": "Custom"
        })") > 0);
        config.close();

        MainWindow window;
        auto* config_edit = window.findChild<QLineEdit*>(QStringLiteral("editAdvancedConfigPath"));
        auto* reload = window.findChild<QPushButton*>(QStringLiteral("btnAdvancedReloadConfig"));
        auto* advanced_mode =
            window.findChild<QComboBox*>(QStringLiteral("cmbAdvancedDefaultFilterMode"));
        auto* advanced_case =
            window.findChild<QCheckBox*>(QStringLiteral("chkAdvancedDefaultFilterCaseSensitive"));
        auto* create_mode = window.findChild<QComboBox*>(QStringLiteral("cmbCreateFilterMode"));
        auto* create_case =
            window.findChild<QCheckBox*>(QStringLiteral("chkCreateFilterCaseSensitive"));
        auto* advanced_edit =
            window.findChild<QPushButton*>(QStringLiteral("btnAdvancedDefaultEditFilterRules"));
        QVERIFY(config_edit != nullptr);
        QVERIFY(reload != nullptr);
        QVERIFY(advanced_mode != nullptr);
        QVERIFY(advanced_case != nullptr);
        QVERIFY(create_mode != nullptr);
        QVERIFY(create_case != nullptr);
        QVERIFY(advanced_edit != nullptr);

        config_edit->setText(config_path);
        reload->click();

        QCOMPARE(advanced_mode->currentIndex(), 1);
        QVERIFY(!advanced_case->isChecked());
        QCOMPARE(create_mode->currentIndex(), 2);
        QVERIFY(create_case->isChecked());
        QVERIFY(!advanced_edit->isEnabled());
    }

    void advancedFilterDefaultsPersistAsCompleteObject()
    {
        QTemporaryDir config_directory;
        QVERIFY(config_directory.isValid());
        const auto config_path = config_directory.filePath(QStringLiteral("torrentcraft.json"));
        QFile config(config_path);
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
        QVERIFY(config.write(R"({"schema":"torrentcraft.config/v1"})") > 0);
        config.close();

        MainWindow window;
        auto* config_edit = window.findChild<QLineEdit*>(QStringLiteral("editAdvancedConfigPath"));
        auto* reload = window.findChild<QPushButton*>(QStringLiteral("btnAdvancedReloadConfig"));
        auto* advanced_mode =
            window.findChild<QComboBox*>(QStringLiteral("cmbAdvancedDefaultFilterMode"));
        auto* advanced_case =
            window.findChild<QCheckBox*>(QStringLiteral("chkAdvancedDefaultFilterCaseSensitive"));
        auto* apply = window.findChild<QPushButton*>(QStringLiteral("btnAdvancedApply"));
        QVERIFY(config_edit != nullptr);
        QVERIFY(reload != nullptr);
        QVERIFY(advanced_mode != nullptr);
        QVERIFY(advanced_case != nullptr);
        QVERIFY(apply != nullptr);

        config_edit->setText(config_path);
        reload->click();
        advanced_mode->setCurrentIndex(1);
        advanced_case->setChecked(true);
        apply->click();

        auto persisted = torrentutils::frontend::ConfigFile::load(
            std::filesystem::u8path(config_path.toUtf8().toStdString()));
        QVERIFY(persisted);
        const auto& filter_setting = persisted.value().parsed().defaults.file_filter;
        QVERIFY(filter_setting.has_value());
        if (!filter_setting)
        {
            return;
        }
        const auto& filter = *filter_setting;
        QCOMPARE(static_cast<int>(filter.mode), static_cast<int>(FileFilterMode::CommonArtifacts));
        QVERIFY(filter.case_sensitive);
        QVERIFY(filter.patterns.empty());
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

    void pieceSizeChoicesRoundTripThroughDefaultsAndPresets()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto config_path = directory.filePath(QStringLiteral("torrentcraft.json"));
        QFile config(config_path);
        QVERIFY(config.open(QIODevice::WriteOnly));
        QVERIFY(config.write(R"({"schema":"torrentcraft.config/v1"})") > 0);
        config.close();
        MainWindow window;
        auto* path = window.findChild<QLineEdit*>("editAdvancedConfigPath");
        auto* reload = window.findChild<QPushButton*>("btnAdvancedReloadConfig");
        auto* apply = window.findChild<QPushButton*>("btnAdvancedApply");
        auto* defaults = window.findChild<QComboBox*>("cmbAdvancedDefaultPieceSize");
        auto* create = window.findChild<QComboBox*>("cmbCreatePieceSize");
        auto* save = window.findChild<QAction*>("actionSavePreset");
        auto* menu = window.findChild<QMenu*>("menuLoadPreset");
        if (!path || !reload || !apply || !defaults || !create || !save || !menu)
        {
            QFAIL("Piece size controls were not found.");
        }
        path->setText(config_path);
        reload->click();
        QVERIFY(!defaults->isEditable());
        QVERIFY(!create->isEditable());
        QCOMPARE(create->count(), 12);
        QCOMPARE(defaults->count(), create->count());

        const auto native_path = std::filesystem::u8path(config_path.toUtf8().toStdString());
        for (int index = 0; index < create->count(); ++index)
        {
            QCOMPARE(defaults->itemText(index), create->itemText(index));
            const auto expected = index == 0 ? std::optional<std::uint32_t>{}
                                             : std::optional<std::uint32_t>{16U << (index - 1)};
            defaults->setCurrentIndex(index);
            apply->click();
            auto persisted = torrentutils::frontend::ConfigFile::load(native_path);
            QVERIFY(persisted);
            const auto& default_piece = persisted.value().parsed().defaults.piece_size;
            if (!default_piece.has_value())
            {
                QFAIL("Default piece size was not persisted.");
            }
            QCOMPARE(default_piece->fixed_kib, expected);
            reload->click();
            QCOMPARE(defaults->currentIndex(), index);
            QCOMPARE(create->currentIndex(), index);

            // Save against a different default, including Automatic over a fixed default.
            defaults->setCurrentIndex(index == 0 ? 1 : 0);
            apply->click();
            create->setCurrentIndex(index);
            const auto name = QStringLiteral("Size%1").arg(index);
            QTimer dismiss_dialogs;
            connect(&dismiss_dialogs, &QTimer::timeout, &window, [&window, &name] {
                for (auto* dialog : window.findChildren<QDialog*>())
                {
                    if (auto* input = qobject_cast<QInputDialog*>(dialog))
                    {
                        input->setTextValue(name);
                    }
                    dialog->accept();
                }
            });
            dismiss_dialogs.start(10);
            save->trigger();
            dismiss_dialogs.stop();
            persisted = torrentutils::frontend::ConfigFile::load(native_path);
            QVERIFY(persisted);
            const auto& presets = persisted.value().parsed().presets;
            const auto saved = presets.find(name.toStdString());
            if (saved == presets.end())
            {
                QFAIL("Preset was not persisted.");
            }
            const auto& saved_piece = saved->second.piece_size;
            if (!saved_piece.has_value())
            {
                QFAIL("Preset piece size was not persisted.");
            }
            QCOMPARE(saved_piece->fixed_kib, expected);
            reload->click();
            QAction* load = nullptr;
            for (auto* action : menu->actions())
            {
                if (action->text() == name)
                {
                    load = action;
                    break;
                }
            }
            if (!load)
            {
                QFAIL("Saved preset was not available to load.");
            }
            load->trigger();
            QCOMPARE(create->currentIndex(), index);
            QCOMPARE(defaults->currentIndex(), index == 0 ? 1 : 0);
        }
    }

    void inspectPieceSizeUsesIecUnits()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto torrent_path = directory.filePath(QStringLiteral("eight-mib.torrent"));
        QFile torrent(torrent_path);
        QVERIFY(torrent.open(QIODevice::WriteOnly));
        QVERIFY(torrent.write("d4:infod6:lengthi1e4:name1:x12:piece lengthi8388608e"
                              "6:pieces20:12345678901234567890ee") > 0);
        torrent.close();
        MainWindow window;
        auto* path = window.findChild<QLineEdit*>("editInspectTorrentPath");
        auto* inspect = window.findChild<QPushButton*>("btnInspectLoad");
        auto* length = window.findChild<QLabel*>("lblInspectPieceLengthValue");
        auto* runner = window.findChild<GuiTaskRunner*>();
        if (!path || !inspect || !length || !runner)
        {
            QFAIL("Inspect controls were not found.");
        }
        path->setText(torrent_path);
        QSignalSpy finished(runner, &GuiTaskRunner::finished);
        inspect->click();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(length->text(), QStringLiteral("8 MiB"));
        QCOMPARE(length->toolTip(), QStringLiteral("8388608 bytes"));
    }

    void createPiecePreviewUsesCorePieceLength_data()
    {
        QTest::addColumn<int>("choice");
        QTest::addColumn<QString>("expected_length");
        QTest::newRow("automatic") << 0 << QStringLiteral("512 KiB");
        QTest::newRow("fixed-eight-mib") << 10 << QStringLiteral("8 MiB");
    }

    void createPiecePreviewUsesCorePieceLength()
    {
        QFETCH(int, choice);
        QFETCH(QString, expected_length);
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
        auto* size = window.findChild<QComboBox*>("cmbCreatePieceSize");
        auto* runner = window.findChild<GuiTaskRunner*>();
        if (!input || !calculate || !pieces || !size || !runner)
        {
            QFAIL("Piece preview controls were not found.");
        }

        input->setText(content_path);
        size->setCurrentIndex(choice);
        QSignalSpy finished(runner, &GuiTaskRunner::finished);
        calculate->click();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(pieces->text().contains(expected_length));
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
            "defaults": {"created_by": "ConfigAuthor"},
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
        QCOMPARE(creator->text(), QStringLiteral("ConfigAuthor"));
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
        custom->setChecked(false);
        QCOMPARE(creator->text(), QStringLiteral("ConfigAuthor"));
        QVERIFY(!custom->isChecked());
        QVERIFY(!creator->isEnabled());
        custom->setChecked(true);
        QVERIFY(creator->text().isEmpty());
        QVERIFY(creator->isEnabled());
    }

    void hashProgressReportsPieceRate()
    {
        MainWindow window;
        auto* runner = window.findChild<GuiTaskRunner*>();
        QVERIFY(runner != nullptr);
        emit runner->progress(QStringLiteral("hashing"), 1, 10, 1024, qulonglong{10} * 1024 * 1024);
        QTest::qWait(2);
        emit runner->progress(QStringLiteral("hashing"), 2, 10, qulonglong{2} * 1024 * 1024,
                              qulonglong{10} * 1024 * 1024);
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

    void addTrackerDialogShowsLineNumbersForMultiLineInput()
    {
        TrackerEditDialog dialog(1, {}, QStringLiteral("Add tracker"), nullptr, true);
        auto* editor = dialog.findChild<QPlainTextEdit*>(QStringLiteral("trackers"));
        QVERIFY(editor != nullptr);
        QVERIFY(dynamic_cast<LineNumberPlainTextEdit*>(editor) != nullptr);

        editor->setPlainText(QStringList{QStringLiteral("http://one.example/announce"), QString(),
                                         QStringLiteral("http://two.example/announce")}
                                 .join(QStringLiteral("\n")));
        QCoreApplication::processEvents();
        auto* line_number_area = editor->findChild<QWidget*>(QStringLiteral("lineNumberArea"));
        QVERIFY(line_number_area != nullptr);
        QVERIFY(line_number_area->width() > 0);
    }

    void filterRulesDialogShowsLineNumbersForMultiLineInput()
    {
        FilterRulesDialog dialog(
            QStringList{QStringLiteral("*.tmp"), QString(), QStringLiteral("cache/")}.join(
                QStringLiteral("\n")));
        auto* editor = dialog.findChild<QPlainTextEdit*>(QStringLiteral("rules"));
        QVERIFY(editor != nullptr);
        QVERIFY(dynamic_cast<LineNumberPlainTextEdit*>(editor) != nullptr);

        QCoreApplication::processEvents();
        auto* line_number_area = editor->findChild<QWidget*>(QStringLiteral("lineNumberArea"));
        QVERIFY(line_number_area != nullptr);
        QVERIFY(line_number_area->width() > 0);
        QCOMPARE(dialog.rules(), QStringLiteral("*.tmp\n\ncache/"));
    }

    void editTrackerDialogAdaptsToSingleLinePage()
    {
        TrackerEditDialog edit_dialog(1, QStringLiteral("http://tracker.example/announce"),
                                      QStringLiteral("Edit tracker"));
        TrackerEditDialog add_dialog(1, {}, QStringLiteral("Add tracker"), nullptr, true);
        QVERIFY(edit_dialog.height() < add_dialog.height());
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

        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        QTRY_VERIFY(!runner->is_running());
        QTRY_VERIFY(!window.isVisible());
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

    void fileTreeShowsTorrentRootFolderForDirectoryLayout()
    {
        auto directory_hashes =
            InfoHashes::create(TorrentFormat::V1, Sha1Digest::from_bytes({}), std::nullopt);
        Q_ASSERT(directory_hashes);
        auto directory_pieces = PieceInfo::create(TorrentFormat::V1, std::uint64_t{16U} * 1024U, 8,
                                                  {Sha1Digest::from_bytes({})});
        Q_ASSERT(directory_pieces);
        auto directory = TorrentInfo::create(
            "iconengines", TorrentFormat::V1, std::move(directory_hashes).value(),
            std::move(directory_pieces).value(), {make_file({"qsvgicon.dll"}, 8)});
        Q_ASSERT(directory);

        FileTreeModel model(nullptr, FileTreeModel::Mode::Inspect);
        model.set_files(directory.value());
        QVERIFY(model.canFetchMore({}));
        model.fetchMore({});
        QCOMPARE(model.rowCount(), 1);
        const auto root = model.index(0, 0);
        QCOMPARE(root.data().toString(), QStringLiteral("iconengines"));
        QVERIFY(root.data(Qt::UserRole).toBool());
        model.fetchMore(root);
        QVERIFY(child_named(model, root, QStringLiteral("qsvgicon.dll")).isValid());

        auto single_hashes =
            InfoHashes::create(TorrentFormat::V1, Sha1Digest::from_bytes({}), std::nullopt);
        Q_ASSERT(single_hashes);
        auto single_pieces = PieceInfo::create(TorrentFormat::V1, std::uint64_t{16U} * 1024U, 8,
                                               {Sha1Digest::from_bytes({})});
        Q_ASSERT(single_pieces);
        auto single =
            TorrentInfo::create("payload.bin", TorrentFormat::V1, std::move(single_hashes).value(),
                                std::move(single_pieces).value(), {make_file({"payload.bin"}, 8)});
        Q_ASSERT(single);

        FileTreeModel single_model(nullptr, FileTreeModel::Mode::Inspect);
        single_model.set_files(single.value());
        single_model.fetchMore({});
        QCOMPARE(single_model.rowCount(), 1);
        QCOMPARE(single_model.index(0, 0).data().toString(), QStringLiteral("payload.bin"));
        QVERIFY(!single_model.index(0, 0).data(Qt::UserRole).toBool());
    }

    void pageResetButtonsClearFilledForms()
    {
        MainWindow window;
        auto* create_reset = window.findChild<QPushButton*>("btnCreateReset");
        auto* modify_reset = window.findChild<QPushButton*>("btnModifyReset");
        auto* inspect_reset = window.findChild<QPushButton*>("btnInspectReset");
        auto* verify_reset = window.findChild<QPushButton*>("btnVerifyReset");
        auto* tracker_reset = window.findChild<QPushButton*>("btnTrackerReset");
        QVERIFY(create_reset != nullptr);
        QVERIFY(modify_reset != nullptr);
        QVERIFY(inspect_reset != nullptr);
        QVERIFY(verify_reset != nullptr);
        QVERIFY(tracker_reset != nullptr);

        auto* create_input = window.findChild<QLineEdit*>("editCreateInputPath");
        auto* create_comment = window.findChild<QPlainTextEdit*>("editCreateComment");
        auto* create_private = window.findChild<QCheckBox*>("chkCreatePrivate");
        auto* modify_input = window.findChild<QLineEdit*>("editModifyTorrentPath");
        auto* modify_comment = window.findChild<QPlainTextEdit*>("editModifyComment");
        auto* inspect_input = window.findChild<QLineEdit*>("editInspectTorrentPath");
        auto* inspect_name = window.findChild<QLabel*>("lblInspectNameValue");
        auto* verify_torrent = window.findChild<QLineEdit*>("editVerifyTorrentPath");
        auto* verify_content = window.findChild<QLineEdit*>("editVerifyContentPath");
        auto* tracker_source = window.findChild<QLineEdit*>("editTrackerSourcePath");
        auto* select_content_file = window.findChild<QPushButton*>("btnVerifySelectContentFile");
        auto* select_folder = window.findChild<QPushButton*>("btnVerifySelectFolder");
        QVERIFY(create_input != nullptr);
        QVERIFY(create_comment != nullptr);
        QVERIFY(create_private != nullptr);
        QVERIFY(modify_input != nullptr);
        QVERIFY(modify_comment != nullptr);
        QVERIFY(inspect_input != nullptr);
        QVERIFY(inspect_name != nullptr);
        QVERIFY(verify_torrent != nullptr);
        QVERIFY(verify_content != nullptr);
        QVERIFY(tracker_source != nullptr);
        QVERIFY(select_content_file != nullptr);
        QVERIFY(select_folder != nullptr);

        create_input->setText(QStringLiteral("/tmp/torrentcraft-reset-demo"));
        create_comment->setPlainText(QStringLiteral("draft comment"));
        create_private->setChecked(true);
        modify_input->setText(QStringLiteral("/tmp/torrentcraft-reset-demo.torrent"));
        modify_comment->setPlainText(QStringLiteral("draft comment"));
        inspect_input->setText(QStringLiteral("/tmp/torrentcraft-reset-demo.torrent"));
        inspect_name->setText(QStringLiteral("stale name"));
        verify_torrent->setText(QStringLiteral("/tmp/torrentcraft-reset-demo.torrent"));
        verify_content->setText(QStringLiteral("/tmp/torrentcraft-reset-demo-content"));
        tracker_source->setText(QStringLiteral("/tmp/torrentcraft-reset-demo"));

        create_reset->click();
        QCOMPARE(create_input->text(), QString());
        QCOMPARE(create_comment->toPlainText(), QString());
        QVERIFY(!create_private->isChecked());

        modify_reset->click();
        QCOMPARE(modify_input->text(), QString());
        QCOMPARE(modify_comment->toPlainText(), QString());

        inspect_reset->click();
        QCOMPARE(inspect_input->text(), QString());
        QCOMPARE(inspect_name->text(), QString());

        verify_reset->click();
        QCOMPARE(verify_torrent->text(), QString());
        QCOMPARE(verify_content->text(), QString());
        QVERIFY(!select_content_file->isHidden());
        QVERIFY(!select_folder->isHidden());

        tracker_reset->click();
        QCOMPARE(tracker_source->text(), QString());
    }

    void createResetRestoresPresetValuesAndKeepsPresetSelected()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto config_path = directory.filePath(QStringLiteral("torrentcraft.json"));
        QFile config(config_path);
        QVERIFY(config.open(QIODevice::WriteOnly));
        QVERIFY(config.write(R"({"schema":"torrentcraft.config/v1","default_preset":"release",)"
                             R"("presets":{"release":{"piece_size":4096,"private":true}}})") > 0);
        config.close();

        MainWindow window;
        auto* config_edit = window.findChild<QLineEdit*>("editAdvancedConfigPath");
        auto* reload = window.findChild<QPushButton*>("btnAdvancedReloadConfig");
        auto* create_reset = window.findChild<QPushButton*>("btnCreateReset");
        auto* piece = window.findChild<QComboBox*>("cmbCreatePieceSize");
        auto* private_box = window.findChild<QCheckBox*>("chkCreatePrivate");
        auto* comment = window.findChild<QPlainTextEdit*>("editCreateComment");
        auto* input = window.findChild<QLineEdit*>("editCreateInputPath");
        QVERIFY(config_edit != nullptr);
        QVERIFY(reload != nullptr);
        QVERIFY(create_reset != nullptr);
        QVERIFY(piece != nullptr);
        QVERIFY(private_box != nullptr);
        QVERIFY(comment != nullptr);
        QVERIFY(input != nullptr);

        config_edit->setText(config_path);
        reload->click();
        QVERIFY(private_box->isChecked());
        const auto preset_piece_index = piece->currentIndex();
        QVERIFY(preset_piece_index > 0);
        QVERIFY(window.windowTitle().contains(QStringLiteral("[release]")));

        input->setText(QStringLiteral("/tmp/torrentcraft-reset-demo"));
        comment->setPlainText(QStringLiteral("draft comment"));
        piece->setCurrentIndex(0);
        private_box->setChecked(false);
        QVERIFY(window.windowTitle().contains(QStringLiteral("[release*]")));

        create_reset->click();

        QCOMPARE(input->text(), QString());
        QCOMPARE(comment->toPlainText(), QString());
        QVERIFY(private_box->isChecked());
        QCOMPARE(piece->currentIndex(), preset_piece_index);
        QVERIFY(window.windowTitle().contains(QStringLiteral("[release]")));
        QVERIFY(!window.windowTitle().contains(QStringLiteral("*")));
    }
};
void GuiLogoTest::initMain()
{
    Q_INIT_RESOURCE(TorrentCraft);
}

QTEST_MAIN(GuiLogoTest)
#include "gui_test.moc"
