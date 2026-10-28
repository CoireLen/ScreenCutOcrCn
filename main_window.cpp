#include "main_window.h"

#include <iostream>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace {

constexpr int kImageRole = Qt::UserRole;
constexpr int kTextRole = Qt::UserRole;
constexpr int kMaxShots = 30;
constexpr int kMaxTexts = 100;
constexpr int kMaxLogBlocks = 500;
constexpr int kThumbWidth = 160;
constexpr int kThumbHeight = 90;

QString make_thumb_text(int width, int height, const QString& time) {
    return QStringLiteral("%1   %2 × %3").arg(time).arg(width).arg(height);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("ScreenCutOcrCn — 截图 OCR"));
    resize(1040, 680);
    setWindowIcon(makeAppIcon());
    buildUi();
    buildTray();
    statusBar()->showMessage(
        QStringLiteral("热键：Alt+A 截图　Alt+C 识别　关闭主界面可选择常驻托盘"));
}

void MainWindow::buildUi() {
    auto* toolbar = addToolBar(QStringLiteral("操作"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QAction* shot_action = toolbar->addAction(QStringLiteral("截图 (Alt+A)"));
    QAction* ocr_action = toolbar->addAction(QStringLiteral("识别 (Alt+C)"));
    connect(shot_action, &QAction::triggered, this, &MainWindow::screenshotRequested);
    connect(ocr_action, &QAction::triggered, this, &MainWindow::ocrRequested);

    auto* tabs = new QTabWidget(this);

    // 截图历史
    auto* shot_page = new QWidget;
    auto* shot_layout = new QVBoxLayout(shot_page);
    shot_layout->setContentsMargins(8, 8, 8, 8);
    auto* shot_bar = new QHBoxLayout;
    auto* copy_shot = new QPushButton(QStringLiteral("复制所选"));
    auto* clear_shot = new QPushButton(QStringLiteral("清空截图历史"));
    shot_bar->addWidget(copy_shot);
    shot_bar->addWidget(clear_shot);
    shot_bar->addStretch();
    shot_layout->addLayout(shot_bar);

    auto* shot_split = new QSplitter(Qt::Horizontal);
    shot_list_ = new QListWidget;
    shot_list_->setIconSize(QSize(kThumbWidth, kThumbHeight));
    shot_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    shot_list_->setAlternatingRowColors(true);
    shot_list_->setToolTip(QStringLiteral("双击复制到剪贴板"));
    shot_preview_ = new QLabel(QStringLiteral("选择左侧截图查看预览"));
    shot_preview_->setAlignment(Qt::AlignCenter);
    shot_preview_->setMinimumWidth(320);
    shot_preview_->setStyleSheet(
        QStringLiteral("background:#f6f6f6; border:1px solid #d8d8d8; color:#888;"));
    shot_preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    shot_split->addWidget(shot_list_);
    shot_split->addWidget(shot_preview_);
    shot_split->setStretchFactor(0, 0);
    shot_split->setStretchFactor(1, 1);
    shot_split->setSizes({380, 620});
    shot_layout->addWidget(shot_split, 1);

    connect(copy_shot, &QPushButton::clicked, this, &MainWindow::copySelectedScreenshot);
    connect(clear_shot, &QPushButton::clicked, this, &MainWindow::clearScreenshots);
    connect(shot_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) { updateScreenshotPreview(); });
    connect(shot_list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { copySelectedScreenshot(); });
    tabs->addTab(shot_page, QStringLiteral("截图历史"));

    // 识别历史
    auto* text_page = new QWidget;
    auto* text_layout = new QVBoxLayout(text_page);
    text_layout->setContentsMargins(8, 8, 8, 8);
    auto* text_bar = new QHBoxLayout;
    auto* copy_text = new QPushButton(QStringLiteral("复制所选"));
    auto* clear_text = new QPushButton(QStringLiteral("清空识别历史"));
    text_bar->addWidget(copy_text);
    text_bar->addWidget(clear_text);
    text_bar->addStretch();
    text_layout->addLayout(text_bar);

    auto* text_split = new QSplitter(Qt::Horizontal);
    text_list_ = new QListWidget;
    text_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    text_list_->setAlternatingRowColors(true);
    text_list_->setWordWrap(false);
    text_list_->setToolTip(QStringLiteral("双击复制到剪贴板"));
    text_preview_ = new QPlainTextEdit;
    text_preview_->setReadOnly(true);
    text_preview_->setPlaceholderText(QStringLiteral("选择左侧记录查看完整文字"));
    text_preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text_split->addWidget(text_list_);
    text_split->addWidget(text_preview_);
    text_split->setStretchFactor(0, 0);
    text_split->setStretchFactor(1, 1);
    text_split->setSizes({380, 620});
    text_layout->addWidget(text_split, 1);

    connect(copy_text, &QPushButton::clicked, this, &MainWindow::copySelectedRecognition);
    connect(clear_text, &QPushButton::clicked, this, &MainWindow::clearRecognitions);
    connect(text_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) {
                text_preview_->setPlainText(
                    current != nullptr ? current->data(kTextRole).toString() : QString());
            });
    connect(text_list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { copySelectedRecognition(); });
    tabs->addTab(text_page, QStringLiteral("识别历史"));

    // 日志
    auto* log_page = new QWidget;
    auto* log_layout = new QVBoxLayout(log_page);
    log_layout->setContentsMargins(8, 8, 8, 8);
    auto* log_bar = new QHBoxLayout;
    auto* clear_log = new QPushButton(QStringLiteral("清空日志"));
    log_bar->addWidget(clear_log);
    log_bar->addStretch();
    log_layout->addLayout(log_bar);
    log_view_ = new QPlainTextEdit;
    log_view_->setReadOnly(true);
    log_view_->setMaximumBlockCount(kMaxLogBlocks);
    log_view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_layout->addWidget(log_view_, 1);
    connect(clear_log, &QPushButton::clicked, this, [this] { log_view_->clear(); });
    tabs->addTab(log_page, QStringLiteral("日志"));

    setCentralWidget(tabs);
}

void MainWindow::buildTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        log(QStringLiteral("系统托盘不可用，关闭窗口将直接退出程序"));
        return;
    }
    tray_ = new QSystemTrayIcon(makeAppIcon(), this);
    tray_->setToolTip(QStringLiteral("ScreenCutOcrCn\nAlt+C 识别　Alt+A 截图"));

    auto* menu = new QMenu(this);
    auto* show_action = menu->addAction(QStringLiteral("显示主界面"));
    menu->addSeparator();
    auto* shot_action = menu->addAction(QStringLiteral("截图 (Alt+A)"));
    auto* ocr_action = menu->addAction(QStringLiteral("识别 (Alt+C)"));
    menu->addSeparator();
    auto* quit_action = menu->addAction(QStringLiteral("退出"));

    connect(show_action, &QAction::triggered, this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    connect(shot_action, &QAction::triggered, this, &MainWindow::screenshotRequested);
    connect(ocr_action, &QAction::triggered, this, &MainWindow::ocrRequested);
    connect(quit_action, &QAction::triggered, this, &MainWindow::quitApplication);
    tray_->setContextMenu(menu);

    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::DoubleClick) {
                    showNormal();
                    raise();
                    activateWindow();
                }
            });
    tray_->show();
}

QIcon MainWindow::makeAppIcon() const {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 174, 255));
    painter.drawRoundedRect(QRectF(2, 2, 60, 60), 14, 14);
    // 取景框图形
    painter.setPen(QPen(Qt::white, 5, Qt::SolidLine, Qt::SquareCap));
    painter.drawLine(18, 24, 18, 46);
    painter.drawLine(18, 46, 46, 46);
    painter.drawLine(24, 18, 46, 18);
    painter.drawLine(46, 18, 46, 40);
    painter.end();
    return QIcon(pixmap);
}

void MainWindow::addScreenshot(const QImage& image) {
    if (image.isNull()) {
        log(QStringLiteral("截图记录失败：图像为空"));
        return;
    }
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    auto* item = new QListWidgetItem(make_thumb_text(image.width(), image.height(), time));
    item->setIcon(QIcon(QPixmap::fromImage(
        image.scaled(kThumbWidth, kThumbHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
    item->setData(kImageRole, image);
    item->setSizeHint(QSize(0, kThumbHeight + 6));
    item->setToolTip(QStringLiteral("双击复制到剪贴板"));
    shot_list_->insertItem(0, item);
    while (shot_list_->count() > kMaxShots) {
        delete shot_list_->takeItem(shot_list_->count() - 1);
    }
    shot_list_->setCurrentItem(item);
    log(QStringLiteral("记录截图 %1 × %2").arg(image.width()).arg(image.height()));
}

void MainWindow::addRecognition(const QString& text) {
    const QString content = text.trimmed();
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    QString preview = content.simplified();
    if (preview.isEmpty()) {
        preview = QStringLiteral("（未识别到文字）");
    } else if (preview.size() > 48) {
        preview = preview.left(48) + QStringLiteral("…");
    }
    auto* item = new QListWidgetItem(QStringLiteral("%1   %2").arg(time, preview));
    item->setData(kTextRole, content);
    QString tooltip = content;
    if (tooltip.size() > 600) {
        tooltip = tooltip.left(600) + QStringLiteral("…");
    }
    item->setToolTip(tooltip);
    text_list_->insertItem(0, item);
    while (text_list_->count() > kMaxTexts) {
        delete text_list_->takeItem(text_list_->count() - 1);
    }
    text_list_->setCurrentItem(item);
    log(content.isEmpty() ? QStringLiteral("识别结果为空")
                          : QStringLiteral("记录识别结果 %1 字符").arg(content.size()));
}

void MainWindow::log(const QString& message) {
    const QString line = QStringLiteral("[%1] %2")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                                  message);
    if (log_view_ != nullptr) {
        log_view_->appendPlainText(line);
        if (QScrollBar* bar = log_view_->verticalScrollBar()) {
            bar->setValue(bar->maximum());
        }
    }
    std::cerr << line.toStdString() << std::endl;
}

void MainWindow::copySelectedScreenshot() {
    auto* item = shot_list_->currentItem();
    if (item == nullptr) {
        statusBar()->showMessage(QStringLiteral("请先选择一条截图记录"), 2500);
        return;
    }
    const QImage image = item->data(kImageRole).value<QImage>();
    if (image.isNull()) {
        statusBar()->showMessage(QStringLiteral("该截图数据已失效"), 2500);
        return;
    }
    QGuiApplication::clipboard()->setImage(image);
    statusBar()->showMessage(QStringLiteral("截图已复制到剪贴板"), 2500);
    log(QStringLiteral("从历史复制截图 %1 × %2").arg(image.width()).arg(image.height()));
}

void MainWindow::copySelectedRecognition() {
    auto* item = text_list_->currentItem();
    if (item == nullptr) {
        statusBar()->showMessage(QStringLiteral("请先选择一条识别记录"), 2500);
        return;
    }
    const QString text = item->data(kTextRole).toString();
    QGuiApplication::clipboard()->setText(text);
    statusBar()->showMessage(QStringLiteral("识别文字已复制到剪贴板"), 2500);
    log(QStringLiteral("从历史复制识别文字 %1 字符").arg(text.size()));
}

void MainWindow::clearScreenshots() {
    if (shot_list_->count() == 0) {
        return;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("清空截图历史"));
    box.setText(QStringLiteral("确定清空全部 %1 条截图记录？").arg(shot_list_->count()));
    auto* clear = box.addButton(QStringLiteral("清空"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != clear) {
        return;
    }
    shot_list_->clear();
    preview_image_ = QImage();
    shot_preview_->setPixmap(QPixmap());
    shot_preview_->setText(QStringLiteral("选择左侧截图查看预览"));
    log(QStringLiteral("截图历史已清空"));
}

void MainWindow::clearRecognitions() {
    if (text_list_->count() == 0) {
        return;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("清空识别历史"));
    box.setText(QStringLiteral("确定清空全部 %1 条识别记录？").arg(text_list_->count()));
    auto* clear = box.addButton(QStringLiteral("清空"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != clear) {
        return;
    }
    text_list_->clear();
    text_preview_->clear();
    log(QStringLiteral("识别历史已清空"));
}

void MainWindow::updateScreenshotPreview() {
    auto* item = shot_list_->currentItem();
    if (item == nullptr) {
        preview_image_ = QImage();
        shot_preview_->setPixmap(QPixmap());
        shot_preview_->setText(QStringLiteral("选择左侧截图查看预览"));
        return;
    }
    preview_image_ = item->data(kImageRole).value<QImage>();
    if (preview_image_.isNull()) {
        shot_preview_->setText(QStringLiteral("该截图数据已失效"));
        return;
    }
    shot_preview_->setText(QString());
    shot_preview_->setPixmap(QPixmap::fromImage(preview_image_)
                                 .scaled(shot_preview_->contentsRect().size(),
                                         Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (!preview_image_.isNull()) {
        updateScreenshotPreview();
    }
}

void MainWindow::quitApplication() {
    quitting_ = true;
    qApp->quit();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (quitting_) {
        event->accept();
        return;
    }
    if (tray_ == nullptr) {
        event->accept();
        qApp->quit();
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("关闭 ScreenCutOcrCn"));
    box.setText(QStringLiteral("关闭主界面后，程序仍可在后台运行。"));
    box.setInformativeText(
        QStringLiteral("常驻托盘时仍可用 Alt+C / Alt+A 或托盘菜单操作；\n"
                       "选择“退出程序”将结束进程。"));
    auto* resident = box.addButton(QStringLiteral("常驻托盘"), QMessageBox::AcceptRole);
    auto* quit = box.addButton(QStringLiteral("退出程序"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(resident);
    box.exec();

    if (box.clickedButton() == quit) {
        quitting_ = true;
        event->accept();
        qApp->quit();
    } else if (box.clickedButton() == resident) {
        event->ignore();
        hide();
        tray_->showMessage(QStringLiteral("ScreenCutOcrCn"),
                           QStringLiteral("已常驻托盘：Alt+C 识别，Alt+A 截图"),
                           QSystemTrayIcon::Information, 2500);
        log(QStringLiteral("主界面已隐藏，程序常驻托盘"));
    } else {
        event->ignore();
    }
}
