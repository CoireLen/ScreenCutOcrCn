#include "main_window.h"

#include <functional>
#include <iostream>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QEvent>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
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
#include <QTimerEvent>
#include <QToolBar>
#include <QVBoxLayout>

namespace {

constexpr int kImageRole = Qt::UserRole;
constexpr int kTextRole = Qt::UserRole + 1;
constexpr int kTitleRole = Qt::UserRole + 2;
constexpr int kMaxHistory = 30;
constexpr int kMaxLogBlocks = 500;
constexpr int kThumbWidth = 160;
constexpr int kThumbHeight = 90;

QString make_thumb_text(int width, int height, const QString& time) {
    return QStringLiteral("%1   %2 × %3").arg(time).arg(width).arg(height);
}

/// 快捷键输入框右键清空（连同内部编辑框一起拦截，返回 true 抑制默认菜单）
class ClearHotkeyFilter : public QObject {
public:
    ClearHotkeyFilter(QKeySequenceEdit* edit, std::function<void()> on_cleared)
        : QObject(edit), edit_(edit), on_cleared_(std::move(on_cleared)) {}

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ContextMenu) {
            edit_->clear();
            on_cleared_();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QKeySequenceEdit* edit_;
    std::function<void()> on_cleared_;
};

/// QKeySequenceEdit 结束录制时会把占位文本重置为英文，这里回到“未设置”
class PlaceholderKeySequenceEdit : public QKeySequenceEdit {
public:
    explicit PlaceholderKeySequenceEdit(const QKeySequence& sequence, QWidget* parent = nullptr)
        : QKeySequenceEdit(sequence, parent) {
        update_placeholder();
    }

protected:
    void focusOutEvent(QFocusEvent* event) override {
        QKeySequenceEdit::focusOutEvent(event);
        update_placeholder();
    }

    void timerEvent(QTimerEvent* event) override {
        QKeySequenceEdit::timerEvent(event);
        update_placeholder();
    }

private:
    void update_placeholder() {
        if (auto* line = findChild<QLineEdit*>()) {
            line->setPlaceholderText(QStringLiteral("未设置"));
        }
    }
};

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("ScreenCutOcrCn — 截图 OCR"));
    resize(1040, 680);
    setWindowIcon(makeAppIcon());
    hotkeys_ = load_hotkeys();
    buildUi();
    buildTray();
    updateHotkeyLabels();
}

void MainWindow::buildUi() {
    auto* toolbar = addToolBar(QStringLiteral("操作"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    shot_action_ = toolbar->addAction(QStringLiteral("截图"));
    ocr_action_ = toolbar->addAction(QStringLiteral("识别"));
    toolbar->addSeparator();
    long_vertical_action_ = toolbar->addAction(QStringLiteral("竖向长截图"));
    long_horizontal_action_ = toolbar->addAction(QStringLiteral("横向长截图"));
    toolbar->addSeparator();
    QAction* hotkey_action = toolbar->addAction(QStringLiteral("快捷键设置"));

    connect(shot_action_, &QAction::triggered, this, &MainWindow::screenshotRequested);
    connect(ocr_action_, &QAction::triggered, this, &MainWindow::ocrRequested);
    connect(long_vertical_action_, &QAction::triggered, this, &MainWindow::longVerticalRequested);
    connect(long_horizontal_action_, &QAction::triggered, this, &MainWindow::longHorizontalRequested);
    connect(hotkey_action, &QAction::triggered, this, &MainWindow::editHotkeys);

    auto* tabs = new QTabWidget(this);

    // 历史：截图列表 + 右侧图片预览与识别文字
    auto* history_page = new QWidget;
    auto* history_layout = new QVBoxLayout(history_page);
    history_layout->setContentsMargins(8, 8, 8, 8);
    auto* history_bar = new QHBoxLayout;
    auto* copy_image = new QPushButton(QStringLiteral("复制图片"));
    auto* copy_text = new QPushButton(QStringLiteral("复制文字"));
    auto* recognize = new QPushButton(QStringLiteral("文字识别"));
    auto* clear_history = new QPushButton(QStringLiteral("清空历史"));
    history_bar->addWidget(copy_image);
    history_bar->addWidget(copy_text);
    history_bar->addWidget(recognize);
    history_bar->addStretch();
    history_bar->addWidget(clear_history);
    history_layout->addLayout(history_bar);

    auto* split = new QSplitter(Qt::Horizontal);
    history_list_ = new QListWidget;
    history_list_->setIconSize(QSize(kThumbWidth, kThumbHeight));
    history_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    history_list_->setAlternatingRowColors(true);
    history_list_->setToolTip(QStringLiteral("双击复制图片，右键可文字识别或复制文字"));
    history_list_->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* right_split = new QSplitter(Qt::Vertical);
    image_preview_ = new QLabel(QStringLiteral("选择左侧记录查看预览"));
    image_preview_->setAlignment(Qt::AlignCenter);
    image_preview_->setMinimumSize(320, 160);
    image_preview_->setFrameShape(QFrame::StyledPanel);
    // 预览底色跟随主题，避免深色界面里出现整块白底
    image_preview_->setAutoFillBackground(true);
    image_preview_->setBackgroundRole(QPalette::Base);
    image_preview_->setForegroundRole(QPalette::Text);
    image_preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    auto* text_panel = new QWidget;
    auto* text_layout = new QVBoxLayout(text_panel);
    text_layout->setContentsMargins(0, 6, 0, 0);
    text_layout->setSpacing(4);
    auto* text_title = new QLabel(QStringLiteral("识别文字"), text_panel);
    text_title->setStyleSheet(QStringLiteral("color:#666;"));
    text_preview_ = new QPlainTextEdit(text_panel);
    text_preview_->setReadOnly(true);
    text_preview_->setPlaceholderText(
        QStringLiteral("该记录尚未识别文字，右键或点击“文字识别”"));
    text_preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text_layout->addWidget(text_title);
    text_layout->addWidget(text_preview_, 1);

    right_split->addWidget(image_preview_);
    right_split->addWidget(text_panel);
    right_split->setStretchFactor(0, 3);
    right_split->setStretchFactor(1, 2);
    right_split->setSizes({380, 260});

    split->addWidget(history_list_);
    split->addWidget(right_split);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({380, 620});
    history_layout->addWidget(split, 1);

    connect(copy_image, &QPushButton::clicked, this, &MainWindow::copySelectedImage);
    connect(copy_text, &QPushButton::clicked, this, &MainWindow::copySelectedText);
    connect(recognize, &QPushButton::clicked, this, &MainWindow::recognizeSelectedImage);
    connect(clear_history, &QPushButton::clicked, this, &MainWindow::clearHistory);
    connect(history_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) { updatePreview(); });
    connect(history_list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { copySelectedImage(); });
    connect(history_list_, &QListWidget::customContextMenuRequested, this,
            &MainWindow::showHistoryMenu);
    tabs->addTab(history_page, QStringLiteral("历史"));

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
    auto* shot_action = menu->addAction(QStringLiteral("截图"));
    auto* ocr_action = menu->addAction(QStringLiteral("识别"));
    auto* long_vertical = menu->addAction(QStringLiteral("竖向长截图"));
    auto* long_horizontal = menu->addAction(QStringLiteral("横向长截图"));
    menu->addSeparator();
    auto* hotkey_action = menu->addAction(QStringLiteral("快捷键设置..."));
    menu->addSeparator();
    auto* quit_action = menu->addAction(QStringLiteral("退出"));

    connect(show_action, &QAction::triggered, this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    connect(shot_action, &QAction::triggered, this, &MainWindow::screenshotRequested);
    connect(ocr_action, &QAction::triggered, this, &MainWindow::ocrRequested);
    connect(long_vertical, &QAction::triggered, this, &MainWindow::longVerticalRequested);
    connect(long_horizontal, &QAction::triggered, this, &MainWindow::longHorizontalRequested);
    connect(hotkey_action, &QAction::triggered, this, &MainWindow::editHotkeys);
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
    const QString title = make_thumb_text(image.width(), image.height(), time);
    auto* item = new QListWidgetItem(title);
    item->setIcon(QIcon(QPixmap::fromImage(
        image.scaled(kThumbWidth, kThumbHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
    item->setData(kImageRole, image);
    item->setData(kTitleRole, title);
    item->setSizeHint(QSize(0, kThumbHeight + 6));
    item->setToolTip(QStringLiteral("双击复制图片，右键可文字识别或复制文字"));
    history_list_->insertItem(0, item);
    while (history_list_->count() > kMaxHistory) {
        delete history_list_->takeItem(history_list_->count() - 1);
    }
    history_list_->setCurrentItem(item);
    log(QStringLiteral("记录截图 %1 × %2").arg(image.width()).arg(image.height()));
}

void MainWindow::addRecognition(const QString& text, const QImage& source) {
    const QString content = text.trimmed();
    QListWidgetItem* item = findItemForImage(source);
    if (item == nullptr) {
        if (content.isEmpty() || source.isNull()) {
            log(content.isEmpty() ? QStringLiteral("识别结果为空")
                                  : QStringLiteral("识别结果未找到对应截图，未保存"));
            return;
        }
        // 识别期间历史可能被清空，结果仍按来源截图保留
        addScreenshot(source);
        item = history_list_->item(0);
        log(QStringLiteral("识别结果对应的截图已不在历史中，已重新加入"));
    }
    applyRecognitionToItem(item, content);
    if (history_list_->currentItem() == item) {
        updatePreview();
    }
    log(content.isEmpty() ? QStringLiteral("识别结果为空")
                          : QStringLiteral("记录识别结果 %1 字符").arg(content.size()));
}

void MainWindow::applyRecognitionToItem(QListWidgetItem* item, const QString& text) {
    item->setData(kTextRole, text);
    const QString title = item->data(kTitleRole).toString();
    if (text.isEmpty()) {
        item->setText(title);
        item->setToolTip(QStringLiteral("未识别到文字。双击复制图片，右键可重新识别"));
        return;
    }
    item->setText(QStringLiteral("%1   · 识别 %2 字").arg(title).arg(text.size()));
    QString tip = text;
    if (tip.size() > 600) {
        tip = tip.left(600) + QStringLiteral("…");
    }
    item->setToolTip(tip);
}

QListWidgetItem* MainWindow::findItemForImage(const QImage& image) const {
    if (image.isNull()) {
        return nullptr;
    }
    // QImage 为隐式共享，历史记录与识别任务持有同一份像素，cacheKey 可直接作为身份
    const qint64 key = image.cacheKey();
    for (int i = 0; i < history_list_->count(); ++i) {
        QListWidgetItem* item = history_list_->item(i);
        const QImage stored = item->data(kImageRole).value<QImage>();
        if (!stored.isNull() && stored.cacheKey() == key) {
            return item;
        }
    }
    return nullptr;
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

void MainWindow::copySelectedImage() {
    auto* item = history_list_->currentItem();
    if (item == nullptr) {
        statusBar()->showMessage(QStringLiteral("请先选择一条记录"), 2500);
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

void MainWindow::copySelectedText() {
    auto* item = history_list_->currentItem();
    if (item == nullptr) {
        statusBar()->showMessage(QStringLiteral("请先选择一条记录"), 2500);
        return;
    }
    const QString text = item->data(kTextRole).toString();
    if (text.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("该记录还没有识别文字"), 2500);
        return;
    }
    QGuiApplication::clipboard()->setText(text);
    statusBar()->showMessage(QStringLiteral("识别文字已复制到剪贴板"), 2500);
    log(QStringLiteral("从历史复制识别文字 %1 字符").arg(text.size()));
}

void MainWindow::recognizeSelectedImage() {
    auto* item = history_list_->currentItem();
    if (item == nullptr) {
        statusBar()->showMessage(QStringLiteral("请先选择一条记录"), 2500);
        return;
    }
    const QImage image = item->data(kImageRole).value<QImage>();
    if (image.isNull()) {
        statusBar()->showMessage(QStringLiteral("该截图数据已失效"), 2500);
        return;
    }
    emit screenshotOcrRequested(image);
}

void MainWindow::showHistoryMenu(const QPoint& pos) {
    auto* item = history_list_->itemAt(pos);
    if (item == nullptr) {
        return;
    }
    history_list_->setCurrentItem(item);
    QMenu menu(this);
    QAction* recognize = menu.addAction(QStringLiteral("文字识别"));
    menu.addSeparator();
    QAction* copy_image = menu.addAction(QStringLiteral("复制图片"));
    QAction* copy_text = menu.addAction(QStringLiteral("复制文字"));
    copy_text->setEnabled(!item->data(kTextRole).toString().isEmpty());
    QAction* chosen = menu.exec(history_list_->viewport()->mapToGlobal(pos));
    if (chosen == recognize) {
        recognizeSelectedImage();
    } else if (chosen == copy_image) {
        copySelectedImage();
    } else if (chosen == copy_text) {
        copySelectedText();
    }
}

void MainWindow::clearHistory() {
    if (history_list_->count() == 0) {
        return;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("清空历史"));
    box.setText(QStringLiteral("确定清空全部 %1 条记录（截图与识别文字）？").arg(history_list_->count()));
    auto* clear = box.addButton(QStringLiteral("清空"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != clear) {
        return;
    }
    history_list_->clear();
    preview_image_ = QImage();
    image_preview_->setPixmap(QPixmap());
    image_preview_->setText(QStringLiteral("选择左侧记录查看预览"));
    text_preview_->clear();
    log(QStringLiteral("历史已清空"));
}

void MainWindow::updatePreview() {
    auto* item = history_list_->currentItem();
    preview_image_ = item != nullptr ? item->data(kImageRole).value<QImage>() : QImage();
    text_preview_->setPlainText(item != nullptr ? item->data(kTextRole).toString() : QString());
    if (preview_image_.isNull()) {
        image_preview_->setPixmap(QPixmap());
        image_preview_->setText(item != nullptr ? QStringLiteral("该截图数据已失效")
                                                : QStringLiteral("选择左侧记录查看预览"));
        return;
    }
    image_preview_->setText(QString());
    image_preview_->setPixmap(QPixmap::fromImage(preview_image_)
                                  .scaled(image_preview_->contentsRect().size(),
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (!preview_image_.isNull()) {
        updatePreview();
    }
}

void MainWindow::editHotkeys() {
    emit hotkeysEditingStarted();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("快捷键设置"));
    dialog.setMinimumWidth(480);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);

    QVector<HotkeyBinding> pending = hotkeys_;
    QVector<QKeySequenceEdit*> edits;
    edits.reserve(pending.size());

    auto* feedback = new QLabel(&dialog);
    feedback->setWordWrap(true);

    bool reverting = false;
    // 每次修改立即校验并写入配置文件；非法输入回退到上一次的有效值
    const auto apply_edit = [&](int index) {
        if (reverting) {
            return;
        }
        const QKeySequence sequence = edits[index]->keySequence();
        if (sequence == pending[index].sequence) {
            return;
        }
        if (!sequence.isEmpty()) {
            QString reason;
            if (!validate_hotkey(sequence, &reason)) {
                reverting = true;
                edits[index]->setKeySequence(pending[index].sequence);
                reverting = false;
                feedback->setStyleSheet(QStringLiteral("color:#d9534f;"));
                feedback->setText(QStringLiteral("未保存：%1").arg(reason));
                return;
            }
            const QString identity = sequence.toString(QKeySequence::PortableText);
            for (int i = 0; i < pending.size(); ++i) {
                if (i == index || pending[i].sequence.isEmpty()) {
                    continue;
                }
                if (pending[i].sequence.toString(QKeySequence::PortableText) == identity) {
                    reverting = true;
                    edits[index]->setKeySequence(pending[index].sequence);
                    reverting = false;
                    feedback->setStyleSheet(QStringLiteral("color:#d9534f;"));
                    feedback->setText(QStringLiteral("未保存：%1 已被“%2”占用")
                                          .arg(hotkey_text(sequence), pending[i].title));
                    return;
                }
            }
        }

        pending[index].sequence = sequence;
        save_hotkeys(pending);
        hotkeys_ = pending;
        updateHotkeyLabels();
        feedback->setStyleSheet(QString());
        feedback->setText(sequence.isEmpty()
                              ? QStringLiteral("已清空“%1”，配置已保存").arg(pending[index].title)
                              : QStringLiteral("已保存“%1”：%2")
                                    .arg(pending[index].title, hotkey_text(sequence)));
    };

    for (int i = 0; i < pending.size(); ++i) {
        auto* edit = new PlaceholderKeySequenceEdit(pending[i].sequence, &dialog);
        edit->setMaximumSequenceLength(1);
        edit->setToolTip(QStringLiteral("按下新的组合键即可保存；右键可清空该快捷键"));
        form->addRow(pending[i].title, edit);
        edits.append(edit);

        connect(edit, &QKeySequenceEdit::editingFinished, edit, [apply_edit, i] { apply_edit(i); });
        auto* filter = new ClearHotkeyFilter(edit, [apply_edit, i] { apply_edit(i); });
        edit->installEventFilter(filter);
        // 右键事件落在内部编辑框上，需要一并拦截
        for (QObject* child : edit->findChildren<QObject*>()) {
            child->installEventFilter(filter);
        }
    }
    layout->addLayout(form);

    auto* note = new QLabel(
        QStringLiteral("修改后立即自动保存并持久化到配置文件，无需手动保存；留空表示禁用，"
                       "右键按键框可直接清空该项。\n"
                       "快捷键需包含 Ctrl、Alt 或 Win，键盘仅支持字母、数字、F1–F24 与常用功能键；"
                       "关闭对话框后按新配置重新注册。"),
        &dialog);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#888;"));
    layout->addWidget(note);
    layout->addWidget(feedback);

    auto* buttons = new QHBoxLayout;
    auto* close = new QPushButton(QStringLiteral("关闭"), &dialog);
    close->setDefault(true);
    buttons->addStretch();
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.exec();
    // 关闭时可能仍在录制（焦点未离开输入框），补一次提交避免漏保存
    for (int i = 0; i < edits.size(); ++i) {
        apply_edit(i);
    }
    emit hotkeysEditingFinished();
}

void MainWindow::updateHotkeyLabels() {
    const auto sequence_of = [this](const QString& id) {
        for (const HotkeyBinding& binding : hotkeys_) {
            if (binding.id == id) {
                return binding.sequence;
            }
        }
        return QKeySequence();
    };
    const auto apply = [](QAction* action, const QString& title, const QKeySequence& sequence) {
        if (action == nullptr) {
            return;
        }
        action->setText(QStringLiteral("%1 (%2)").arg(title, hotkey_text(sequence)));
        action->setToolTip(QStringLiteral("%1，快捷键：%2").arg(title, hotkey_text(sequence)));
    };
    apply(shot_action_, QStringLiteral("截图"), sequence_of(QStringLiteral("shot")));
    apply(ocr_action_, QStringLiteral("识别"), sequence_of(QStringLiteral("ocr")));
    apply(long_vertical_action_, QStringLiteral("竖向长截图"),
          sequence_of(QStringLiteral("long_vertical")));
    apply(long_horizontal_action_, QStringLiteral("横向长截图"),
          sequence_of(QStringLiteral("long_horizontal")));

    const QString shot = hotkey_text(sequence_of(QStringLiteral("shot")));
    const QString ocr = hotkey_text(sequence_of(QStringLiteral("ocr")));
    const QString vertical = hotkey_text(sequence_of(QStringLiteral("long_vertical")));
    const QString horizontal = hotkey_text(sequence_of(QStringLiteral("long_horizontal")));
    if (tray_ != nullptr) {
        tray_->setToolTip(QStringLiteral("ScreenCutOcrCn\n%1 截图　%2 识别").arg(shot, ocr));
    }
    if (statusBar() != nullptr) {
        statusBar()->showMessage(QStringLiteral("热键：%1 截图　%2 识别　%3 竖向长截图　%4 横向长截图　（关闭主界面可常驻托盘）")
                                     .arg(shot, ocr, vertical, horizontal));
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

    const auto sequence_text = [this](const QString& id) {
        for (const HotkeyBinding& binding : hotkeys_) {
            if (binding.id == id) {
                return hotkey_text(binding.sequence);
            }
        }
        return QStringLiteral("未设置");
    };

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("关闭 ScreenCutOcrCn"));
    box.setText(QStringLiteral("关闭主界面后，程序仍可在后台运行。"));
    box.setInformativeText(
        QStringLiteral("常驻托盘时仍可用 %1 截图、%2 识别或托盘菜单操作；\n"
                       "选择“退出程序”将结束进程。")
            .arg(sequence_text(QStringLiteral("shot")), sequence_text(QStringLiteral("ocr"))));
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
                           QStringLiteral("已常驻托盘：%1 截图，%2 识别")
                               .arg(sequence_text(QStringLiteral("shot")),
                                    sequence_text(QStringLiteral("ocr"))),
                           QSystemTrayIcon::Information, 2500);
        log(QStringLiteral("主界面已隐藏，程序常驻托盘"));
    } else {
        event->ignore();
    }
}
