#include "AboutDialog.hpp"

#include "LicenseDialog.hpp"
#include "Logo.hpp"
#include "TorrentCraftBuildInfo.hpp"
#include "ui_AboutDialog.h"

#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <torrentutils/core/version.hpp>

namespace {
QString license_alias(const QString& component)
{
    const auto normalized = component.toLower();
    if (normalized == QStringLiteral("nlohmann/json"))
        return QStringLiteral("nlohmann-json");
    if (normalized == QStringLiteral("qt"))
        return QStringLiteral("qt");
    if (normalized == QStringLiteral("openssl"))
        return QStringLiteral("openssl");
    if (normalized == QStringLiteral("catch2 (tests)"))
        return QStringLiteral("catch2");
    return normalized;
}

QString read_license(const QString& alias)
{
    QFile file(QStringLiteral(":/torrentcraft/licenses/") + alias + QStringLiteral(".txt"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

void add_build_info(QTreeWidget* tree, const QString& key, const char* value)
{
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, key);
    item->setText(1, QString::fromUtf8(value));
}

void add_component_info(QTreeWidget* tree, const QString& component, const char* version,
                        const QString& license)
{
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, component);
    item->setText(1, QString::fromUtf8(version));
    item->setText(2, license);
}
} // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent), ui_(new Ui::AboutDialog)
{
    ui_->setupUi(this);
    setWindowIcon(torrentcraft::gui::application_icon());
    ui_->logo->setPixmap(torrentcraft::gui::render_logo(128));
    ui_->logo->setAlignment(Qt::AlignCenter);
    ui_->logo->setScaledContents(false);
    const auto core_version = torrentutils::core::version();
    const auto core_version_text =
        QString::fromUtf8(core_version.data(), static_cast<int>(core_version.size()));
    ui_->version->setText(tr("Version %1").arg(core_version_text));

    ui_->treeBuildInfo->clear();
    add_build_info(ui_->treeBuildInfo, tr("Version"), core_version.data());
    add_build_info(ui_->treeBuildInfo, tr("Git Commit"), torrentcraft::gui::build_info::git_commit);
    add_build_info(ui_->treeBuildInfo, tr("Git Branch"), torrentcraft::gui::build_info::git_branch);
    add_build_info(ui_->treeBuildInfo, tr("Build Date"), torrentcraft::gui::build_info::build_date);
    add_build_info(ui_->treeBuildInfo, tr("Build Type"), torrentcraft::gui::build_info::build_type);
    add_build_info(ui_->treeBuildInfo, tr("Platform"), torrentcraft::gui::build_info::platform);
    add_build_info(ui_->treeBuildInfo, tr("Architecture"),
                   torrentcraft::gui::build_info::architecture);
    add_build_info(ui_->treeBuildInfo, tr("Compiler"), torrentcraft::gui::build_info::compiler);
    add_build_info(ui_->treeBuildInfo, tr("C++ Standard"), "C++17");
    add_build_info(ui_->treeBuildInfo, tr("Qt Version"), torrentcraft::gui::build_info::qt_version);
    add_build_info(ui_->treeBuildInfo, tr("libtorrent Version"),
                   torrentcraft::gui::build_info::libtorrent_version);
    add_build_info(ui_->treeBuildInfo, tr("Build System"),
                   torrentcraft::gui::build_info::build_system);
    add_build_info(ui_->treeBuildInfo, tr("Generator"), torrentcraft::gui::build_info::generator);
    add_build_info(ui_->treeBuildInfo, tr("Package Manager"),
                   torrentcraft::gui::build_info::package_manager);
    add_build_info(ui_->treeBuildInfo, tr("vcpkg Commit"),
                   torrentcraft::gui::build_info::vcpkg_commit);

    ui_->treeComponents->clear();
    add_component_info(ui_->treeComponents, tr("Qt"), torrentcraft::gui::build_info::qt_version,
                       QStringLiteral("LGPL-3.0"));
    add_component_info(ui_->treeComponents, tr("libtorrent"),
                       torrentcraft::gui::build_info::libtorrent_version,
                       QStringLiteral("BSD-3-Clause"));
    add_component_info(ui_->treeComponents, tr("nlohmann/json"),
                       torrentcraft::gui::build_info::nlohmann_json_version, QStringLiteral("MIT"));
    add_component_info(ui_->treeComponents, tr("indicators"),
                       torrentcraft::gui::build_info::indicators_version, QStringLiteral("MIT"));
    add_component_info(ui_->treeComponents, tr("OpenSSL"),
                       torrentcraft::gui::build_info::openssl_version,
                       QStringLiteral("Apache-2.0"));
    add_component_info(ui_->treeComponents, tr("Boost"),
                       torrentcraft::gui::build_info::boost_version, QStringLiteral("BSL-1.0"));
    add_component_info(ui_->treeComponents, tr("Catch2 (tests)"),
                       torrentcraft::gui::build_info::catch2_version, QStringLiteral("BSL-1.0"));

    ui_->treeLicenses->clear();
    const QList<QPair<QString, QString>> licenses = {
        {QStringLiteral("TorrentCraft"), QStringLiteral("MIT")},
        {QStringLiteral("Qt"), QStringLiteral("LGPL-3.0")},
        {QStringLiteral("libtorrent"), QStringLiteral("BSD-3-Clause")},
        {QStringLiteral("nlohmann/json"), QStringLiteral("MIT")},
        {QStringLiteral("indicators"), QStringLiteral("MIT")},
        {QStringLiteral("OpenSSL"), QStringLiteral("Apache-2.0")},
        {QStringLiteral("Boost"), QStringLiteral("BSL-1.0")},
        {QStringLiteral("Catch2 (tests)"), QStringLiteral("BSL-1.0")},
    };
    for (const auto& license : licenses)
    {
        auto* item = new QTreeWidgetItem(ui_->treeLicenses);
        const auto display_name = license.first == QStringLiteral("Catch2 (tests)")
                                      ? tr("Catch2 (tests)")
                                      : license.first;
        item->setText(0, display_name);
        item->setText(1, license.second);
        item->setData(0, Qt::UserRole, license_alias(license.first));
    }

    ui_->btnViewLicense->setEnabled(false);

    connect(ui_->treeLicenses, &QTreeWidget::itemSelectionChanged, this, [this] {
        ui_->btnViewLicense->setEnabled(ui_->treeLicenses->currentItem() != nullptr);
    });
    connect(ui_->btnViewLicense, &QPushButton::clicked, this, [this] {
        auto* item = ui_->treeLicenses->currentItem();
        if (item == nullptr)
            return;

        const auto alias = item->data(0, Qt::UserRole).toString();
        auto license_text = read_license(alias);
        if (license_text.isEmpty())
        {
            license_text = tr("License text is unavailable for this build.");
        }

        LicenseDialog dialog(tr("License: %1").arg(item->text(0)), license_text, this);
        dialog.exec();
    });
    connect(ui_->btnClose, &QPushButton::clicked, this, &QDialog::accept);
}

AboutDialog::~AboutDialog()
{
    delete ui_;
}
